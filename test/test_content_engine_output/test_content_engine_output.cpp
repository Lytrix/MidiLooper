//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine CE-C1: physical output port. Owner review of e85fe039 (2026-11-04):
//   * NoteOffs need no FIFO slot: they are owed by the port's sounding table (<= 8 tracks x 16), so long held notes never
//     occupy the queue; a NoteOff whose NoteOn is still queued cancels both (never a NoteOff for an unsent note);
//   * essential state (Bank Select, Program Change, sustain-family pedals, channel mode) and RPN/NRPN messages go through
//     the ordered queue with notes and are never dropped by the automation quota; state coalesces only where no queued
//     NoteOn on that channel depends on the earlier value;
//   * continuous automation (other CCs, pitch bend, channel pressure, poly aftertouch) coalesces per port/channel/
//     controller to its latest pending value, unchanged values are not re-sent, the final value always goes out;
//   * the transmit scheduler is priority-aware and causal: due NoteOffs first (after their NoteOn), then the ordered
//     queue (state before dependent NoteOns), with continuous automation guaranteed one transmission in four while
//     both are pending. Shares are of actual transmissions; lateness is measured separately for NoteOn and NoteOff.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/OutputPort.h"
#include "ContentEngine/TerminalScheduler.h"

static unsigned long gHeapAllocations = 0;
static bool gTrap = false;
void* operator new(std::size_t size) {
  if (gTrap) ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ContentEngine;

using Port = OutputPort<>;  // 128 sounding, 64 ordered, 16 essential reserve
static Port gPort;
static Port gUsb;
static TerminalScheduler<Config::kMaxSoundingPerTrack> gSched;
static std::vector<Port::Sent> gSent;

constexpr uint32_t kTick = 1041;  // 300 BPM

static void drainUntil(Port& p, uint32_t until) {
  Port::Sent buf[64];
  for (uint32_t t = 0; t <= until; t += kTick) {
    const uint8_t n = p.transmit(t, buf, 64);
    gSent.insert(gSent.end(), buf, buf + n);
  }
}

void setUp() {
  gPort.init(kDinMessageMicros);
  gUsb.init(kUsbMessageMicros);
  gSched.init();
  gSent.clear();
  gSent.reserve(200000);
}
void tearDown() {}

// A NoteOff whose NoteOn is still queued cancels both; nothing is sent for that note.
void test_noteoff_before_its_noteon_is_sent_cancels_both() {
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOn(0, 0x90, 60, 100, 1));
  TEST_ASSERT_EQUAL(PortResult::CancelledUnsent, gPort.noteOff(0, 1));
  drainUntil(gPort, 10000);
  TEST_ASSERT_EQUAL_UINT32(0, gSent.size());
  TEST_ASSERT_EQUAL(PortResult::NotAdmitted, gPort.noteOff(20000, 2));
}

// RED 1: a saturated CC sweep ends at its final value; unchanged values are not re-sent.
void test_saturated_cc_sweep_reaches_final_value() {
  uint32_t key = 1;
  for (uint32_t t = 0; t < 400; ++t) {
    const uint32_t now = t * kTick;
    for (int k = 0; k < 3; ++k) gPort.noteOn(now, 0x90, static_cast<uint8_t>(40 + (key % 40)), 100, key++);
    if (t < 128) gPort.control(now, 0xB0, 74, static_cast<uint8_t>(t));
    Port::Sent buf[64];
    const uint8_t n = gPort.transmit(now, buf, 64);
    gSent.insert(gSent.end(), buf, buf + n);
  }
  drainUntil(gPort, 2000000);
  int last = -1;
  uint32_t sweepSent = 0;
  for (const auto& s : gSent)
    if (s.status == 0xB0 && s.data1 == 74) {
      last = s.data2;
      ++sweepSent;
    }
  TEST_ASSERT_EQUAL_INT(127, last);
  TEST_ASSERT_TRUE(sweepSent < 128);  // coalesced under saturation
  TEST_ASSERT_EQUAL(PortResult::Deduplicated, gPort.control(3000000, 0xB0, 74, 127));
}

// RED 2: Bank Select -> Program Change -> NoteOn keep their order on a saturated port; a later Program Change does not
// overtake the NoteOn that depends on the earlier one.
void test_bank_program_noteon_order_under_saturation() {
  uint32_t key = 1;
  for (int k = 0; k < 40; ++k) gPort.noteOn(0, 0x90, static_cast<uint8_t>(40 + k), 100, key++);  // ch 1 backlog
  for (int k = 0; k < 200; ++k) gPort.control(0, 0xB0, static_cast<uint8_t>(10 + (k % 20)), static_cast<uint8_t>(k & 0x7F));
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.control(0, 0xB2, 0, 1));    // ch 3 Bank MSB
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.control(0, 0xB2, 32, 2));   // ch 3 Bank LSB
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.control(0, 0xC2, 5, 0));    // ch 3 Program 5
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOn(0, 0x92, 60, 100, 999));
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.control(0, 0xC2, 6, 0));    // ch 3 Program 6, after the NoteOn
  drainUntil(gPort, 500000);
  std::vector<uint8_t> ch3;
  for (const auto& s : gSent)
    if ((s.status & 0x0F) == 2) ch3.push_back(s.status == 0xB2 ? s.data1 : s.status);
  const uint8_t want[] = {0, 32, 0xC2, 0x92, 0xC2};
  TEST_ASSERT_EQUAL_UINT32(5, ch3.size());
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_UINT8(want[i], ch3[i]);
  TEST_ASSERT_EQUAL_UINT32(0, gPort.counters().parameterDropped);
}

// RED 3: sustain release during saturation is never dropped and follows the press.
void test_sustain_release_survives_saturation() {
  uint32_t key = 1;
  gPort.control(0, 0xB0, 64, 127);
  for (uint32_t t = 0; t < 300; ++t) {
    const uint32_t now = t * kTick;
    for (int k = 0; k < 3; ++k) gPort.noteOn(now, 0x90, static_cast<uint8_t>(40 + (key % 40)), 100, key++);
    gPort.control(now, 0xB0, 1, static_cast<uint8_t>(t & 0x7F));  // mod wheel flood
    if (t == 150) TEST_ASSERT_NOT_EQUAL(PortResult::Suppressed, gPort.control(now, 0xB0, 64, 0));
    Port::Sent buf[64];
    const uint8_t n = gPort.transmit(now, buf, 64);
    gSent.insert(gSent.end(), buf, buf + n);
  }
  drainUntil(gPort, 3000000);
  int press = -1, release = -1;
  for (size_t i = 0; i < gSent.size(); ++i)
    if (gSent[i].status == 0xB0 && gSent[i].data1 == 64) (gSent[i].data2 ? press : release) = static_cast<int>(i);
  TEST_ASSERT_TRUE(press >= 0);
  TEST_ASSERT_TRUE(release > press);
}

// RED 4: 8 tracks x 16 long held notes on one DIN port. Sounding notes hold no FIFO entry; the ordered queue is free
// for state and new traffic while they sound; all 128 NoteOffs go out when they come due.
void test_128_held_notes_share_one_port_without_holding_fifo() {
  uint32_t admitted = 0;
  for (uint32_t t = 0; admitted < 128 && t < 2000; ++t) {
    const uint32_t now = t * kTick;
    while (admitted < 128 && gPort.noteOn(now, static_cast<uint8_t>(0x90 | (admitted / 16)), static_cast<uint8_t>(30 + admitted % 16 * 4), 90,
                                          1000 + admitted) == PortResult::Queued)
      ++admitted;
    Port::Sent buf[64];
    const uint8_t n = gPort.transmit(now, buf, 64);
    gSent.insert(gSent.end(), buf, buf + n);
  }
  TEST_ASSERT_EQUAL_UINT32(128, admitted);
  drainUntil(gPort, 400000);
  TEST_ASSERT_EQUAL_UINT16(128, gPort.soundingCount());
  TEST_ASSERT_EQUAL_UINT16(0, gPort.orderedQueued());  // nothing held in the FIFO while the notes sound
  TEST_ASSERT_EQUAL(PortResult::Queued, gPort.control(500000, 0xC0, 3, 0));
  TEST_ASSERT_EQUAL(PortResult::Suppressed, gPort.noteOn(500000, 0x90, 99, 1, 5000));  // 129th sounding: refused
  gSent.clear();
  for (uint32_t i = 0; i < 128; ++i) TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOff(10000000, 1000 + i));
  Port::Sent buf[64];
  for (uint32_t t = 10000000; t < 10300000; t += kTick) {
    const uint8_t n = gPort.transmit(t, buf, 64);
    gSent.insert(gSent.end(), buf, buf + n);
  }
  uint32_t offs = 0;
  for (const auto& s : gSent) offs += s.cls == OutClass::NoteOff ? 1u : 0u;
  TEST_ASSERT_EQUAL_UINT32(128, offs);
  TEST_ASSERT_TRUE(gPort.counters().maxNoteOffLateMicros <= 129u * kDinMessageMicros);
  TEST_ASSERT_TRUE(gPort.check());
}

// RED 5: a note burst arriving behind an already-full automation table does not wait for the automation to drain.
void test_note_burst_behind_full_automation() {
  for (uint8_t ch = 0; ch < 16; ++ch)
    for (uint8_t cc = 0; cc < 128; ++cc)
      if (cc != 0 && cc != 6 && cc != 32 && cc != 38 && (cc < 64 || cc > 69) && cc < 96)  // automation only
        gPort.control(0, static_cast<uint8_t>(0xB0 | ch), cc, 1);  // ~1,300 pending automation values
  for (uint32_t i = 0; i < 32; ++i) TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOn(0, 0x90, static_cast<uint8_t>(40 + i), 100, 1 + i));
  drainUntil(gPort, 100000);
  uint32_t ons = 0;
  for (const auto& s : gSent) ons += s.cls == OutClass::NoteOn ? 1u : 0u;
  TEST_ASSERT_EQUAL_UINT32(32, ons);  // within 100 ms although 1.3 s of automation is pending
  TEST_ASSERT_TRUE(gPort.counters().maxNoteOnLateMicros <= (32u * 4u / 3u + 2u) * kDinMessageMicros);
}

// Regression (e85fe039 scenario) with the corrected policy: 10 s, 3 notes/tick (20 ticks long) and 2 CCs/tick through the
// track's terminal scheduler. Every transmitted NoteOn gets exactly one NoteOff after it; none for unsent notes;
// continuous automation takes at most ~1/4 of transmissions while notes are pending; lateness reported per class.
void test_sustained_din_saturation_regression() {
  constexpr uint32_t kTicks = 9600;
  static uint32_t onAt[40000], offAt[40000];
  TerminalScheduler<Config::kMaxSoundingPerTrack>::Out offs[Config::kMaxSoundingPerTrack];
  uint32_t nextId = 1;
  Port::Sent buf[64];
  gTrap = true;
  const unsigned long heapBefore = gHeapAllocations;
  for (uint32_t t = 0; t < kTicks + 3000; ++t) {
    const uint32_t now = t * kTick;
    const uint8_t d = gSched.due(t, offs, Config::kMaxSoundingPerTrack);
    for (uint8_t i = 0; i < d; ++i) gPort.noteOff(now, offs[i].noteId);
    if (t < kTicks) {
      for (int k = 0; k < 3; ++k) {
        const uint32_t id = nextId++;
        const uint8_t pitch = static_cast<uint8_t>(36 + id % 48);
        if (!gPort.canAdmitNoteOn()) continue;
        if (gSched.start(t, id, 0, 0, 1, pitch, t + 20) != StartResult::Started) continue;
        TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOn(now, 0x90, pitch, 100, id));
      }
      gPort.control(now, 0xB0, 74, static_cast<uint8_t>(t & 0x7F));
      gPort.control(now, 0xB0, 1, static_cast<uint8_t>((t * 3) & 0x7F));
    }
    const uint8_t n = gPort.transmit(now, buf, 64);
    gSent.insert(gSent.end(), buf, buf + n);
  }
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
  uint32_t on = 0, off = 0, cont = 0, total = 0;
  for (const auto& s : gSent) {
    ++total;
    if (s.cls == OutClass::NoteOn) {
      ++on;
      onAt[s.key] = s.micros + 1;  // +1: a NoteOn may start at time 0
    } else if (s.cls == OutClass::NoteOff) {
      ++off;
      TEST_ASSERT_TRUE(onAt[s.key] != 0 && s.micros + 1 > onAt[s.key]);
      TEST_ASSERT_EQUAL_UINT32(0, offAt[s.key]);  // exactly one NoteOff
      offAt[s.key] = s.micros;
    } else if (s.cls == OutClass::Continuous) {
      ++cont;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(on, off);
  TEST_ASSERT_TRUE(cont * 100u <= total * 27u);
  char line[240];
  std::snprintf(line, sizeof line,
                "DIN saturation: sent on=%u off=%u continuous=%u of %u (%.1f%%); coalesced=%u; cancelled-unsent=%u; "
                "NoteOn late max=%u us mean=%u us; NoteOff late max=%u us mean=%u us",
                on, off, cont, total, 100.0 * cont / total, gPort.counters().coalesced, gPort.counters().cancelledUnsent,
                gPort.counters().maxNoteOnLateMicros,
                on ? static_cast<unsigned>(gPort.counters().sumNoteOnLateMicros / on) : 0u,
                gPort.counters().maxNoteOffLateMicros,
                off ? static_cast<unsigned>(gPort.counters().sumNoteOffLateMicros / off) : 0u);
  TEST_MESSAGE(line);
}

// Per-port independence: a saturated DIN port does not delay the USB port.
void test_ports_are_independent() {
  for (uint32_t i = 0; i < 60; ++i) gPort.noteOn(0, 0x90, static_cast<uint8_t>(i), 100, 1 + i);
  TEST_ASSERT_EQUAL(PortResult::Queued, gUsb.noteOn(0, 0x90, 60, 100, 77));
  Port::Sent buf[64];
  TEST_ASSERT_EQUAL(1, gUsb.transmit(0, buf, 64));
  TEST_ASSERT_EQUAL_UINT32(0, buf[0].micros);
  char line[80];
  std::snprintf(line, sizeof line, "OutputPort<> fixed size: %u B per port", static_cast<unsigned>(sizeof(Port)));
  TEST_MESSAGE(line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_noteoff_before_its_noteon_is_sent_cancels_both);
  RUN_TEST(test_saturated_cc_sweep_reaches_final_value);
  RUN_TEST(test_bank_program_noteon_order_under_saturation);
  RUN_TEST(test_sustain_release_survives_saturation);
  RUN_TEST(test_128_held_notes_share_one_port_without_holding_fifo);
  RUN_TEST(test_note_burst_behind_full_automation);
  RUN_TEST(test_sustained_din_saturation_regression);
  RUN_TEST(test_ports_are_independent);
  return UNITY_END();
}
