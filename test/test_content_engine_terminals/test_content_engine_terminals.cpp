//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine CE-B1 / DEC-148: the fixed terminal scheduler of one playback track. Sounding instances are
// started from composed START entries; their NoteOff is owned either by the composed source (an inline end, a TERMINAL
// entry) or by the live writer (a still-held note whose NoteOff has not been recorded when its window was composed).
// Whichever closes first sends the NoteOff; every later terminal for that instance is suppressed. Nothing is sent into
// the past: a terminal whose tick has already gone fires at the current tick. Live occupancy is a count per pitch.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/Playback/TerminalScheduler.h"

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
using Sched = TerminalScheduler<8>;  // small T so the full-set case is cheap to reach; production is Config::kMaxSoundingPerTrack

static Sched gS;
static Sched::Out gOut[16];

static uint8_t fire(uint32_t tick) { return gS.due(tick, gOut, 16); }

void setUp() { gS.init(); }
void tearDown() {}

// A still-held live writer note: its START window was composed with an open end. The writer's NoteOff closes it at the
// current tick; the TERMINAL that a later recomposition carries for the same note is suppressed.
void test_live_noteoff_closes_open_instance_and_suppresses_later_terminal() {
  TEST_ASSERT_EQUAL(StartResult::Started, gS.start(100, 7, 0, 0, 1, 60, kOpenEnd));
  TEST_ASSERT_EQUAL(0, fire(500));
  TEST_ASSERT_EQUAL(CloseResult::Closed, gS.liveNoteOff(7, 0, 900));
  TEST_ASSERT_EQUAL(1, fire(900));
  TEST_ASSERT_EQUAL_UINT32(900, gOut[0].tick);
  TEST_ASSERT_EQUAL_UINT32(7, gOut[0].noteId);
  TEST_ASSERT_EQUAL_UINT8(1, gOut[0].channel);
  TEST_ASSERT_EQUAL(CloseResult::NotSounding, gS.composedEnd(7, 0, 905));
  TEST_ASSERT_EQUAL(0, fire(905));
  TEST_ASSERT_EQUAL_UINT32(1, gS.counters().suppressedTerminals);
}

// Cross-window closure: START with an open end, TERMINAL two windows later.
void test_composed_terminal_closes_across_windows() {
  gS.start(100, 1, 0, 0, 2, 61, kOpenEnd);
  TEST_ASSERT_EQUAL(0, fire(1600));
  TEST_ASSERT_EQUAL(CloseResult::EndSet, gS.composedEnd(1, 0, 3100));
  TEST_ASSERT_EQUAL(0, fire(3099));
  TEST_ASSERT_EQUAL(1, fire(3100));
  TEST_ASSERT_EQUAL(0, gS.sounding());
}

// Duplicate terminals (the old and the new revision both carry one): the first closes, the second is suppressed.
void test_duplicate_terminal_suppressed() {
  gS.start(10, 2, 0, 0, 1, 62, kOpenEnd);
  gS.composedEnd(2, 0, 200);
  TEST_ASSERT_EQUAL(1, fire(200));
  TEST_ASSERT_EQUAL(CloseResult::NotSounding, gS.composedEnd(2, 0, 210));
  TEST_ASSERT_EQUAL(0, fire(210));
}

// A terminal that arrives after its tick has passed fires now, never in the past.
void test_late_terminal_fires_at_current_tick() {
  gS.start(10, 3, 0, 0, 1, 63, kOpenEnd);
  TEST_ASSERT_EQUAL(0, fire(400));
  gS.composedEnd(3, 0, 300);  // logically 300, learned at 401
  TEST_ASSERT_EQUAL(1, fire(401));
  TEST_ASSERT_EQUAL_UINT32(401, gOut[0].tick);
  TEST_ASSERT_EQUAL_UINT32(1, gS.counters().lateTerminals);
}

// Revision transition (undo, redo, commit): instances the new source carries keep sounding; instances it does not carry
// are closed now (DEC-148 closure); carried entries for notes never sounded are ignored.
void test_revision_transition_reconciles_carry_set() {
  gS.start(10, 11, 0, 0, 1, 60, 2000);  // carried by the new revision
  gS.start(20, 12, 0, 0, 1, 64, 2000);  // undone: not carried
  const Sched::Carry carry[] = {{11, 0, 2000}, {99, 0, 1800}};
  TEST_ASSERT_EQUAL(1, gS.transition(1536, carry, 2, gOut, 16));
  TEST_ASSERT_EQUAL_UINT32(12, gOut[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1536, gOut[0].tick);
  TEST_ASSERT_EQUAL(1, gS.sounding());
  TEST_ASSERT_EQUAL(0, fire(1800));  // 99 was never sounded
  TEST_ASSERT_EQUAL(1, fire(2000));
  TEST_ASSERT_EQUAL_UINT32(11, gOut[0].noteId);
}

// Live overlap: a live NoteOn on pitch 60 while playback note A (60) sounds. Logically A ends one tick earlier; in real
// time A's NoteOff is sent now, before the live NoteOn the caller passes through. Composed NoteOns on 60 are suppressed
// while the pitch is occupied; two live notes on two channels hold the pitch until both are released.
void test_live_occupy_closes_now_and_suppresses_until_released() {
  gS.start(100, 21, 0, 0, 3, 60, 900);
  gS.start(110, 22, 1, 1, 4, 60, 950);  // a second instance on 60, other channel: also ended (DEC-033)
  TEST_ASSERT_EQUAL(2, gS.occupy(60, 300, gOut, 16));
  TEST_ASSERT_EQUAL_UINT32(300, gOut[0].tick);
  TEST_ASSERT_EQUAL_UINT8(3, gOut[0].channel);  // run order 0 before run order 1
  TEST_ASSERT_EQUAL_UINT8(4, gOut[1].channel);
  TEST_ASSERT_EQUAL(0, fire(900));  // A's composed end is gone with the instance
  TEST_ASSERT_EQUAL(2, gS.occupy(60, 310, gOut, 16) + 2);  // second live note: nothing sounding to close
  TEST_ASSERT_EQUAL(StartResult::SuppressedOccupied, gS.start(400, 23, 0, 0, 1, 60, 500));
  gS.release(60);
  TEST_ASSERT_EQUAL(StartResult::SuppressedOccupied, gS.start(420, 24, 0, 0, 1, 60, 500));
  gS.release(60);
  TEST_ASSERT_EQUAL(StartResult::Started, gS.start(440, 25, 0, 0, 1, 60, 500));
  TEST_ASSERT_EQUAL_UINT32(2, gS.counters().suppressedOccupied);
}

// Capacity: a full set refuses a new instance by name; every started instance still gets its NoteOff.
void test_full_set_refuses_new_start_and_never_drops_a_noteoff() {
  for (uint32_t i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(StartResult::Started, gS.start(10, 100 + i, 0, 0, 1, 40 + i, 50 + i));
  TEST_ASSERT_EQUAL(StartResult::RefusedFull, gS.start(11, 200, 0, 0, 1, 70, 60));
  uint32_t sent = 0;
  for (uint32_t t = 50; t < 60; ++t) sent += fire(t);
  TEST_ASSERT_EQUAL_UINT32(8, sent);
  TEST_ASSERT_EQUAL_UINT32(1, gS.counters().refusedFull);
}

// DEC-244 at one tick: NoteOffs in pass then id order.
void test_equal_tick_noteoffs_in_pass_then_id_order() {
  gS.start(10, 9, 2, 2, 1, 50, 100);
  gS.start(10, 8, 2, 2, 1, 51, 100);
  gS.start(10, 30, 1, 1, 1, 52, 100);
  TEST_ASSERT_EQUAL(3, fire(100));
  TEST_ASSERT_EQUAL_UINT32(30, gOut[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(8, gOut[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(9, gOut[2].noteId);
}

// The same note cannot sound twice: a second START for a sounding id is a duplicate, not a second instance.
void test_duplicate_start_is_not_a_second_instance() {
  gS.start(10, 5, 0, 0, 1, 50, 100);
  TEST_ASSERT_EQUAL(StartResult::Duplicate, gS.start(10, 5, 0, 0, 1, 50, 100));
  TEST_ASSERT_EQUAL(1, gS.sounding());
}

void test_no_heap() {
  const unsigned long before = gHeapAllocations;
  for (uint32_t i = 0; i < 1000; ++i) {
    gS.start(i, i, 0, 0, 1, i % 128, i + 3);
    fire(i);
  }
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

// Physical-port backpressure: the port accepts fewer NoteOffs than are due at this tick. The rest stay owned by the
// scheduler and go out on the next ticks (counted late); none is dropped, none is sent for an unsounded note.
void test_port_backpressure_delays_noteoffs_and_drops_none() {
  for (uint32_t i = 0; i < 6; ++i) gS.start(10, 300 + i, 0, 0, 1, 50 + i, 100);
  TEST_ASSERT_EQUAL(2, gS.due(100, gOut, 2));   // port room for 2
  TEST_ASSERT_EQUAL(4, gS.sounding());
  TEST_ASSERT_EQUAL(StartResult::Started, gS.start(100, 400, 0, 0, 1, 70, 900));  // a NoteOn the port admitted
  uint32_t sent = 2;
  for (uint32_t t = 101; t < 110; ++t) sent += gS.due(t, gOut, 1);  // one per tick
  TEST_ASSERT_EQUAL_UINT32(6, sent);
  TEST_ASSERT_EQUAL_UINT32(4, gS.counters().lateTerminals);
  TEST_ASSERT_EQUAL(1, gS.sounding());  // only the instance ending at 900
}

// Production size: OUT-POLY T = 16 per track, 12-byte instances (owner budget 8 x 16 x 12 = 1,536 B of instances).
void test_production_terminal_set_size() {
  using TrackTerminals = TerminalScheduler<Config::kMaxSoundingPerTrack>;
  static TrackTerminals t;
  t.init();
  for (uint32_t i = 0; i < 20; ++i) t.start(10, i + 1, 0, 0, static_cast<uint8_t>(1 + (i % 16)), static_cast<uint8_t>(i), 50);
  TEST_ASSERT_EQUAL(16, t.sounding());
  TEST_ASSERT_EQUAL_UINT32(4, t.counters().refusedFull);
  char line[120];
  std::snprintf(line, sizeof line, "TerminalScheduler<16>: %u B per track (instances 192 B + occupancy 128 B + counters)",
                static_cast<unsigned>(sizeof(TrackTerminals)));
  TEST_MESSAGE(line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_live_noteoff_closes_open_instance_and_suppresses_later_terminal);
  RUN_TEST(test_composed_terminal_closes_across_windows);
  RUN_TEST(test_duplicate_terminal_suppressed);
  RUN_TEST(test_late_terminal_fires_at_current_tick);
  RUN_TEST(test_revision_transition_reconciles_carry_set);
  RUN_TEST(test_live_occupy_closes_now_and_suppresses_until_released);
  RUN_TEST(test_full_set_refuses_new_start_and_never_drops_a_noteoff);
  RUN_TEST(test_equal_tick_noteoffs_in_pass_then_id_order);
  RUN_TEST(test_duplicate_start_is_not_a_second_instance);
  RUN_TEST(test_port_backpressure_delays_noteoffs_and_drops_none);
  RUN_TEST(test_production_terminal_set_size);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
