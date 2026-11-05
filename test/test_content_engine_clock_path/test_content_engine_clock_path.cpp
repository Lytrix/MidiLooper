//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.3: the Clock path at production capacity — every track's playback events through
// `ClockOutput` into the output hub and out of its three ports (DIN, USB device, USB host), as the firmware will drive
// it: `clockTick` per Clock tick, each port's `transmit` after it. Checked: the take's notes, velocities and controller
// values on every port, in time on USB and in order on DIN; per-track port routing with no stuck note when it changes;
// a re-strike of one instance is a new note in the hub; Undo to the empty slot silences every port; a congested DIN
// port cancels NoteOns whose end passed (scheduled ends reach the hub); no allocation.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <new>
#include <vector>

#include "ContentEngine/EngineRoot.h"

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
using R = ProductionTransport::Result;
using Sent = ProductionHub::Sent;

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;

constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kLoop = 768 * 4;
constexpr uint32_t kS = 1000;

struct Wire {
  uint32_t clock;  // the Clock tick it was transmitted after
  Sent s;
};
static std::vector<Wire> gWire[3];
static uint32_t gClock = 0;

void setUp() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  for (auto& w : gWire) {
    w.clear();
    w.reserve(20000);
  }
  gClock = 0;
}
void tearDown() {}

static uint32_t us(uint32_t clock) { return clock * kUsPerTick; }

struct Live {
  uint32_t on, off;
  uint8_t pitch, velocity;
};
struct Cc {
  uint32_t tick;
  uint8_t status, d1, d2;
};
/// Clock ticks up to `to`: compose work, the Clock tick, each port's transmissions within the tick, then live input.
static void run(uint32_t to, const std::vector<Live>& live = {}, const std::vector<Cc>& cc = {}) {
  Sent buf[32];
  for (; gClock < to; ++gClock) {
    gE.submitJobs(gClock, us(gClock), kUsPerTick);
    while (gE.runGrain(us(gClock))) {
    }
    gE.clockTick(gClock, us(gClock), kUsPerTick);
    for (uint32_t step = 0; step < kUsPerTick; step += 125)  // the ports drain through the tick
      for (uint8_t p = 0; p < 3; ++p) {
        const uint8_t n = gE.transmit(p, us(gClock) + step, buf, 32);
        for (uint8_t i = 0; i < n; ++i) gWire[p].push_back({gClock, buf[i]});
      }
    for (const Live& l : live) {
      if (l.off == gClock) gE.transport().noteOff(gClock, 1, l.pitch, 0);
      if (l.on == gClock) gE.noteOn(gClock, 1, l.pitch, l.velocity, us(gClock), kUsPerTick);
    }
    for (const Cc& c : cc)
      if (c.tick == gClock) gE.transport().channelEvent(gClock, c.status, c.d1, c.d2);
  }
}
static void recordTake(uint8_t track, uint8_t slot, uint32_t start, uint32_t loop, const std::vector<Live>& live,
                       const std::vector<Cc>& cc = {}) {
  gClock = start;
  TEST_ASSERT_EQUAL(R::Ok, gE.transport().startRecord(track, slot, gClock));
  run(start + loop, live, cc);
  TEST_ASSERT_EQUAL(R::Ok, gE.stop(gClock, us(gClock), kUsPerTick));
}
/// Every NoteOn on the port is followed by a NoteOff of its pitch and channel, and nothing sounds at the end.
static void expectBalanced(uint8_t p) {
  std::map<uint16_t, int> open;
  for (const Wire& w : gWire[p]) {
    const uint8_t type = w.s.status & 0xF0;
    const uint16_t k = static_cast<uint16_t>(((w.s.status & 0x0F) << 8) | w.s.data1);
    if (type == 0x90 && w.s.data2 != 0) ++open[k];
    if (type == 0x80) {
      TEST_ASSERT_TRUE(open[k] > 0);  // never a NoteOff before its NoteOn
      --open[k];
    }
  }
  for (const auto& o : open) TEST_ASSERT_EQUAL_INT(0, o.second);
  TEST_ASSERT_EQUAL_UINT16(0, gE.hub().soundingCount(p));
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
static const std::vector<Live> kTake = {
    {kS + 0, kS + 50, 60, 100}, {kS + 400, kS + 500, 64, 80}, {kS + 1600, kS + 1700, 67, 60}, {kS + 2500, kS + 2900, 72, 127}};
static const std::vector<Cc> kCc = {{kS + 300, 0xB0, 7, 90}, {kS + 2000, 0xE0, 0, 70}, {kS + 2100, 0xB0, 64, 127},
                                    {kS + 2200, 0xB0, 64, 0}};

void test_every_port_carries_the_take() {
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  recordTake(0, 0, kS, kLoop, kTake, kCc);
  run(kS + 4 * kLoop + 2600);  // inside a note (72: 2500..2900)
  TEST_ASSERT_EQUAL_UINT16(1, gE.hub().soundingCount(kPortUsbDevice));
  gE.undo(0, gClock, us(gClock), kUsPerTick);  // ends the take mid-note: its NoteOff reaches every port
  run(gClock + 200);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  for (uint8_t p = 0; p < 3; ++p) {
    // Every cycle: every note with its velocity. Controllers: all four in cycle 1; from cycle 2 only the essential
    // ones (CC64 on / off) — a continuous lane does not resend the value it already holds (C.4).
    for (uint32_t c = 1; c < 4; ++c) {
      std::map<uint8_t, uint8_t> ons;  // pitch -> velocity
      std::vector<std::pair<uint8_t, uint8_t>> controls;
      for (const Wire& w : gWire[p]) {
        if (w.clock < kS + c * kLoop || w.clock >= kS + (c + 1) * kLoop) continue;
        if ((w.s.status & 0xF0) == 0x90) ons[w.s.data1] = w.s.data2;
        if ((w.s.status & 0xF0) == 0xB0 || (w.s.status & 0xF0) == 0xE0) controls.push_back({w.s.status, w.s.data2});
      }
      TEST_ASSERT_EQUAL_UINT32(kTake.size(), ons.size());
      for (const Live& l : kTake) TEST_ASSERT_EQUAL_UINT8(l.velocity, ons[l.pitch]);
      const size_t first = c == 1 ? 0 : 2;
      TEST_ASSERT_EQUAL_UINT32(kCc.size() - first, controls.size());
      for (size_t i = first; i < kCc.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(kCc[i].status, controls[i - first].first);
        TEST_ASSERT_EQUAL_UINT8(kCc[i].d2, controls[i - first].second);
      }
    }
    expectBalanced(p);
  }
  // In time: on USB each NoteOn of cycle 2 leaves within its own tick; DIN serialises but keeps order.
  for (const Wire& w : gWire[kPortUsbDevice])
    if ((w.s.status & 0xF0) == 0x90 && w.clock >= kS + 2 * kLoop && w.clock < kS + 3 * kLoop) {
      bool found = false;
      for (const Live& l : kTake) found |= l.pitch == w.s.data1 && w.clock == kS + 2 * kLoop + (l.on - kS);
      TEST_ASSERT_TRUE(found);
    }
  for (size_t i = 1; i < gWire[kPortDin].size(); ++i)
    TEST_ASSERT_TRUE(gWire[kPortDin][i].s.micros >= gWire[kPortDin][i - 1].s.micros + kDinMessageMicros);
}

void test_routing_changes_never_strand_a_note() {
  gE.output().setPorts(0, 1u << kPortDin);      // track 0: DIN only
  gE.output().setPorts(1, 1u << kPortUsbHost);  // track 1: USB host only
  recordTake(0, 0, kS, kLoop, kTake);
  const uint32_t s1 = gClock + 100;
  run(s1);
  recordTake(1, 8, s1, kLoop, {{s1 + 10, s1 + 3000, 50, 70}}, {{s1 + 20, 0xB0, 7, 33}});  // one long note, a CC
  run(kS + 3 * kLoop + 2600);  // inside track 0's last note (2500..2900)
  TEST_ASSERT_EQUAL_UINT32(0, gWire[kPortUsbDevice].size());
  TEST_ASSERT_EQUAL_UINT16(1, gE.hub().soundingCount(kPortDin));      // track 0 only
  TEST_ASSERT_EQUAL_UINT16(1, gE.hub().soundingCount(kPortUsbHost));  // track 1 only
  gE.output().setPorts(0, 1u << kPortUsbDevice);  // moved while a note sounds on DIN
  run(kS + 5 * kLoop);
  gE.undo(8, gClock, us(gClock), kUsPerTick);  // track 1 mid-note: its NoteOff reaches USB host
  gE.undo(0, gClock, us(gClock), kUsPerTick);
  run(gClock + 200);
  for (uint8_t p = 0; p < 3; ++p) expectBalanced(p);  // DIN got its NoteOff; USB device plays from the move on
  TEST_ASSERT_TRUE(gWire[kPortUsbDevice].size() > 0);
  bool cc = false;
  for (const Wire& w : gWire[kPortUsbHost]) {  // only track 1's note and controller
    if ((w.s.status & 0xF0) == 0xB0) cc = true;
    else TEST_ASSERT_EQUAL_UINT8(50, w.s.data1);
  }
  TEST_ASSERT_TRUE(cc);
  for (uint8_t p : {kPortDin, kPortUsbDevice})
    for (const Wire& w : gWire[p]) TEST_ASSERT_NOT_EQUAL(0xB0, w.s.status & 0xF0);
}

void test_a_live_note_ends_the_composed_note_on_the_ports() {
  recordTake(0, 0, kS, kLoop, kTake);
  run(kS + 2 * kLoop + 10);
  TEST_ASSERT_EQUAL(R::Ok, gE.transport().startOverdub(0, gClock));
  const uint32_t c2 = kS + 2 * kLoop;
  run(c2 + 2800, {{c2 + 2600, c2 + 2700, 72, 110}});  // played over the composed 72 (2500..2900)
  // The composed 72 ended at the live NoteOn (live occupancy), on every port, before the take's own end: input lands
  // after a tick's drain, so its NoteOff leaves with the next one.
  for (uint8_t p = 0; p < 3; ++p) {
    uint32_t offAt = 0;
    for (const Wire& w : gWire[p])
      if ((w.s.status & 0xF0) == 0x80 && w.s.data1 == 72 && w.clock >= c2 && w.clock < c2 + kLoop) offAt = w.clock;
    TEST_ASSERT_EQUAL_UINT32(c2 + 2601, offAt);
  }
  TEST_ASSERT_EQUAL(R::Ok, gE.stop(gClock, us(gClock), kUsPerTick));
  gE.undo(0, gClock, us(gClock), kUsPerTick);
  gE.undo(0, gClock, us(gClock), kUsPerTick);
  run(gClock + 200);
  for (uint8_t p = 0; p < 3; ++p) expectBalanced(p);
}

void test_a_track_channel_rewrites_every_channel_message() {
  // The legacy playback rule: with a track channel set, notes and controllers go out on it, whatever was recorded.
  gE.output().setChannel(0, 5);
  recordTake(0, 0, kS, kLoop, {{kS + 100, kS + 200, 60, 90}}, {{kS + 50, 0xB0, 7, 77}});
  run(kS + 2 * kLoop + 10);
  uint32_t on = 0, cc = 0;
  for (const Wire& w : gWire[kPortUsbDevice]) {
    if (w.clock < kS + kLoop) continue;  // the take, played back
    if ((w.s.status & 0xF0) == 0x90) {
      TEST_ASSERT_EQUAL_HEX8(0x94, w.s.status);
      ++on;
    }
    if ((w.s.status & 0xF0) == 0xB0) {
      TEST_ASSERT_EQUAL_HEX8(0xB4, w.s.status);
      ++cc;
    }
    if ((w.s.status & 0xF0) == 0x80) TEST_ASSERT_EQUAL_HEX8(0x84, w.s.status);
  }
  TEST_ASSERT_EQUAL_UINT32(1, on);
  TEST_ASSERT_EQUAL_UINT32(1, cc);
}

void test_a_restrike_is_a_new_note_and_keys_are_bounded() {
  ProductionEmit on{100, 1, 5, makeRunKey(3, 0), 1, 60, 0x90, 99, 2, 200};
  ProductionEmit off{100, 0, 5, makeRunKey(3, 0), 1, 60, 0x80, 0, 2, 100};
  // One instance sounding (its NoteOn on the wire), then ended and started again on one tick.
  Sent buf[8];
  gE.output().route(&on, 1, us(100), kUsPerTick);
  uint8_t n = gE.transmit(kPortUsbDevice, us(100), buf, 8);
  const ProductionEmit seq[2] = {off, on};
  gE.output().route(seq, 2, us(100) + 200, kUsPerTick);
  for (uint32_t t = 200; t < 1200; t += 125)  // one USB message per 125 µs
    n = static_cast<uint8_t>(n + gE.transmit(kPortUsbDevice, us(100) + t, buf + n, static_cast<uint8_t>(8 - n)));
  TEST_ASSERT_EQUAL_UINT8(3, n);
  TEST_ASSERT_EQUAL_HEX8(0x90, buf[0].status);
  TEST_ASSERT_EQUAL_HEX8(0x80, buf[1].status);
  TEST_ASSERT_EQUAL_HEX8(0x90, buf[2].status);
  // Two instances sharing a note id in different runs (an edit replacement and the note it replaces): each NoteOff
  // ends its own instance.
  ProductionEmit a = on, b = on;
  a.track = b.track = 3;
  b.runKey = makeRunKey(4, 1);
  b.pitch = 61;
  gE.output().route(&a, 1, us(110), kUsPerTick);
  gE.output().route(&b, 1, us(110), kUsPerTick);
  for (uint32_t t = 0; t < 1000; t += 125) gE.transmit(kPortUsbDevice, us(110) + t, buf, 8);
  ProductionEmit offB = off;
  offB.track = 3;
  offB.runKey = b.runKey;
  offB.pitch = 61;
  gE.output().route(&offB, 1, us(112), kUsPerTick);
  TEST_ASSERT_EQUAL_UINT8(1, gE.transmit(kPortUsbDevice, us(112), buf, 8));
  TEST_ASSERT_EQUAL_HEX8(0x80, buf[0].status);
  TEST_ASSERT_EQUAL_UINT8(61, buf[0].data1);
  // A track's key slots are its sounding bound: the 17th open instance is refused, its NoteOff dropped.
  for (uint32_t i = 0; i < Config::kMaxSoundingPerTrack; ++i) {  // 15 more beside the restruck one
    ProductionEmit e = on;
    e.noteId = 1000 + i;
    gE.output().route(&e, 1, us(101), kUsPerTick);
  }
  TEST_ASSERT_EQUAL_UINT32(1, gE.output().stats().refusedNoKey);
  ProductionEmit lost = off;
  lost.noteId = 1000 + Config::kMaxSoundingPerTrack - 1;
  gE.output().route(&lost, 1, us(102), kUsPerTick);
  TEST_ASSERT_EQUAL_UINT32(1, gE.output().stats().unmatchedOff);
  TEST_ASSERT_EQUAL_UINT8(Config::kMaxSoundingPerTrack, gE.output().open(2));
}

void test_a_congested_din_port_cancels_notes_whose_end_passed() {
  // Sixteen 3-tick notes on one tick: DIN needs ~15 ms for them, so the later ones end before they could start.
  std::vector<Live> chord;
  for (uint8_t i = 0; i < 16; ++i) chord.push_back({kS + 200, kS + 203, static_cast<uint8_t>(40 + i), 90});
  recordTake(0, 0, kS, kLoop, chord);
  run(kS + 3 * kLoop);
  gE.undo(0, gClock, us(gClock), kUsPerTick);
  run(gClock + 200);
  ProductionHub& hub = const_cast<ProductionHub&>(gE.hub());
  TEST_ASSERT_TRUE(hub.port(kPortDin).counters().expiredBeforeTransmit > 0);
  TEST_ASSERT_EQUAL_UINT32(0, hub.port(kPortUsbDevice).counters().expiredBeforeTransmit);  // USB keeps up
  for (uint8_t p = 0; p < 3; ++p) expectBalanced(p);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_port_carries_the_take);
  RUN_TEST(test_routing_changes_never_strand_a_note);
  RUN_TEST(test_a_live_note_ends_the_composed_note_on_the_ports);
  RUN_TEST(test_a_track_channel_rewrites_every_channel_message);
  RUN_TEST(test_a_restrike_is_a_new_note_and_keys_are_bounded);
  RUN_TEST(test_a_congested_din_port_cancels_notes_whose_end_passed);
  return UNITY_END();
}
