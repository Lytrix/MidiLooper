//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine CE-C1: physical output port admission. A bounded FIFO per port with *protected NoteOff capacity*:
// every NoteOn the port admits reserves one slot for its NoteOff, so a NoteOff for a transmitted (or queued) note can
// always be queued; new NoteOns and other messages are suppressed by name before their admission would threaten that.
// FIFO order is kept (a NoteOff never overtakes its own NoteOn). Residence is bounded by capacity x message time, and
// lateness is measured, not promised away: DIN carries at most ~1,042 three-byte messages per second.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/OutputPort.h"
#include "ContentEngine/TerminalScheduler.h"

static unsigned long gHeapAllocations = 0;
void* operator new(std::size_t size) {
  ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ContentEngine;

using Port = OutputPort<64>;
static Port gPort;
static TerminalScheduler<Config::kMaxSoundingPerTrack> gSched;

void setUp() {
  gPort.init(kDinMessageMicros);
  gSched.init();
}
void tearDown() {}

void test_noteon_reserves_its_noteoff_slot() {
  for (uint32_t i = 0; i < 32; ++i) TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOn(0, 0x90, 60, 100, i));
  // 32 queued + 32 reserved = 64: no room for another NoteOn or any other message, but every NoteOff fits.
  TEST_ASSERT_EQUAL(PortResult::Suppressed, gPort.noteOn(0, 0x90, 61, 100, 99));
  TEST_ASSERT_EQUAL(PortResult::Suppressed, gPort.other(0, 0xB0, 1, 2));
  for (uint32_t i = 0; i < 32; ++i) TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOff(0, 0x80, 60, 0, i));
  TEST_ASSERT_TRUE(gPort.check());
  TEST_ASSERT_EQUAL(PortResult::NotReserved, gPort.noteOff(0, 0x80, 60, 0, 999));  // never for an unadmitted note
}

void test_fifo_keeps_noteoff_after_its_noteon_and_bounds_residence() {
  gPort.noteOn(0, 0x90, 60, 100, 1);
  gPort.noteOff(10, 0x80, 60, 0, 1);
  Port::Sent sent[8];
  const uint8_t n = gPort.drain(5000, sent, 8);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_UINT8(0x90, sent[0].status);
  TEST_ASSERT_EQUAL_UINT8(0x80, sent[1].status);
  TEST_ASSERT_TRUE(sent[1].transmitMicros >= sent[0].transmitMicros + kDinMessageMicros);
}

// Sustained DIN saturation for 10 s at 300 BPM: every tick offers 3 new notes (20 ticks long) and 2 CCs, about 5x what
// DIN can carry. NoteOffs of admitted notes are never refused and always transmitted, after their own NoteOn; no NoteOff
// is sent for a suppressed note; the queue never exceeds capacity; residence (lateness) stays within capacity x message
// time; throughput is what DIN can carry, not more.
void test_sustained_din_saturation_protects_noteoffs() {
  const unsigned long heapBefore = gHeapAllocations;
  constexpr uint32_t kTickMicros = 1041;  // 300 BPM x 192 PPQN = 960 ticks/s
  constexpr uint32_t kTicks = 9600;
  static uint32_t onAt[200000];
  static uint32_t offAt[200000];
  static bool admitted[200000];
  uint32_t nextId = 1, noteOnsSent = 0, noteOffsSent = 0, otherSent = 0;
  uint32_t suppressedOn = 0, suppressedOther = 0;
  TerminalScheduler<Config::kMaxSoundingPerTrack>::Out offs[Config::kMaxSoundingPerTrack];
  Port::Sent sent[64];
  for (uint32_t t = 0; t < kTicks + 2000; ++t) {
    const uint32_t now = t * kTickMicros;
    // NoteOffs due: the scheduler owns them; the port's reservation guarantees they queue.
    const uint8_t d = gSched.due(t, offs, Config::kMaxSoundingPerTrack);
    for (uint8_t i = 0; i < d; ++i)
      TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOff(now, 0x80, offs[i].pitch, 0, offs[i].noteId));
    if (t < kTicks) {
      for (uint8_t k = 0; k < 3; ++k) {
        const uint32_t id = nextId++;
        const uint8_t pitch = static_cast<uint8_t>(36 + (id % 48));
        // Admission order: the port must have room for the NoteOn and its NoteOff, then the scheduler (OUT-POLY).
        if (!gPort.canAdmitNoteOn()) {
          ++suppressedOn;
          continue;
        }
        if (gSched.start(t, id, 0, 0, 1, pitch, t + 20) != StartResult::Started) {
          ++suppressedOn;
          continue;
        }
        TEST_ASSERT_EQUAL(PortResult::Queued, gPort.noteOn(now, 0x90, pitch, 100, id));
        admitted[id] = true;
      }
      for (uint8_t k = 0; k < 2; ++k)
        if (gPort.other(now, 0xB0, 1, static_cast<uint8_t>(t & 0x7F)) != PortResult::Queued) ++suppressedOther;
    }
    TEST_ASSERT_TRUE(gPort.check());
    const uint8_t n = gPort.drain(now + kTickMicros, sent, 64);
    for (uint8_t i = 0; i < n; ++i) {
      const auto& s = sent[i];
      if ((s.status & 0xF0) == 0x90) {
        ++noteOnsSent;
        onAt[s.noteKey] = s.transmitMicros;
      } else if ((s.status & 0xF0) == 0x80) {
        ++noteOffsSent;
        TEST_ASSERT_TRUE(admitted[s.noteKey]);
        offAt[s.noteKey] = s.transmitMicros;
        TEST_ASSERT_TRUE(offAt[s.noteKey] > onAt[s.noteKey]);
      } else {
        ++otherSent;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT32(noteOnsSent, noteOffsSent);  // every transmitted note was closed
  // Owner ruling 2026-11-04 (guarantee NoteOns): non-note messages hold at most their share of the queue, so note
  // traffic (NoteOn + NoteOff) gets at least the rest of the port's throughput under saturation.
  TEST_ASSERT_TRUE(gPort.counters().maxOtherQueued <= Port::kOtherShare);
  TEST_ASSERT_TRUE(noteOnsSent + noteOffsSent >= 2u * otherSent);
  TEST_ASSERT_TRUE(suppressedOn > 0);
  TEST_ASSERT_TRUE(suppressedOther > 0);
  TEST_ASSERT_TRUE(gPort.counters().maxResidenceMicros <= 64u * kDinMessageMicros);
  const uint32_t seconds = (kTicks + 2000) * kTickMicros / 1000000u + 1;
  TEST_ASSERT_TRUE(noteOnsSent + noteOffsSent + otherSent <= seconds * (1000000u / kDinMessageMicros));
  char line[200];
  std::snprintf(line, sizeof line,
                "DIN saturation: sent on=%u off=%u other=%u; suppressed on=%u other=%u; max residence=%u us (bound %u)",
                static_cast<unsigned>(noteOnsSent), static_cast<unsigned>(noteOffsSent), static_cast<unsigned>(otherSent),
                static_cast<unsigned>(suppressedOn), static_cast<unsigned>(suppressedOther),
                static_cast<unsigned>(gPort.counters().maxResidenceMicros), 64u * kDinMessageMicros);
  TEST_MESSAGE(line);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_noteon_reserves_its_noteoff_slot);
  RUN_TEST(test_fifo_keeps_noteoff_after_its_noteon_and_bounds_residence);
  RUN_TEST(test_sustained_din_saturation_protects_noteoffs);
  return UNITY_END();
}
