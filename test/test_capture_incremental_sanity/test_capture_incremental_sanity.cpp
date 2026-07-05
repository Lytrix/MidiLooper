//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/CaptureIncrementalSanity.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

#include "Globals.h"
#include "Utils/CaptureIncrementalSanity.h"
#include "Utils/LoopEventValidation.h"
#include "MidiEvent.h"

namespace {

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 2;

void setupStore() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
}

}  // namespace

void test_repair_completed_pair_removes_orphan_off() {
  setupStore();
  LoopEventStore store;
  const MidiEvent off = MidiEvent::NoteOff(100, 1, 60, 0);
  TEST_ASSERT_TRUE(store.append(off));

  const auto result =
      CaptureIncrementalSanity::repairCompletedPair(store, off, kLoopLen, 12, true);
  TEST_ASSERT_EQUAL(1u, result.eventsRemoved);
  TEST_ASSERT_TRUE(store.empty());
}

void test_repair_completed_pair_keeps_short_pair_during_capture() {
  setupStore();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  const MidiEvent off = MidiEvent::NoteOff(111, 1, 60, 0);
  TEST_ASSERT_TRUE(store.append(off));

  const auto result =
      CaptureIncrementalSanity::repairCompletedPair(store, off, kLoopLen, 12, true);
  TEST_ASSERT_EQUAL(0u, result.eventsRemoved);
  TEST_ASSERT_EQUAL(2u, store.size());
}

void test_repair_completed_pair_keeps_short_pair_when_disabled() {
  setupStore();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  const MidiEvent off = MidiEvent::NoteOff(111, 1, 60, 0);
  TEST_ASSERT_TRUE(store.append(off));

  const auto result =
      CaptureIncrementalSanity::repairCompletedPair(store, off, kLoopLen, 12, false);
  TEST_ASSERT_EQUAL(0u, result.eventsRemoved);
  TEST_ASSERT_EQUAL(2u, store.size());
}

void test_repair_orphan_note_events_removes_duplicate_on() {
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(10, 1, 60, 100));
  events.push_back(MidiEvent::NoteOn(20, 1, 60, 90));
  events.push_back(MidiEvent::NoteOff(30, 1, 60, 0));

  const auto result = LoopEventValidation::repairOrphanNoteEvents(events, kLoopLen);
  TEST_ASSERT_EQUAL(1u, result.orphanedRemoved);
  TEST_ASSERT_EQUAL(2u, events.size());
  TEST_ASSERT_TRUE(events[0].isNoteOn());
  TEST_ASSERT_EQUAL(20u, events[0].tick);
}

void test_repair_wrap_window_slice_removes_orphan_off_in_tail() {
  setupStore();
  LoopEventStore store;
  const uint32_t tailOn = kLoopLen - 100;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tailOn + 10, 1, 60, 0)));

  const auto result =
      CaptureIncrementalSanity::repairWrapWindowSlice(store, kLoopLen, Config::TICKS_PER_BAR);
  TEST_ASSERT_EQUAL(1u, result.eventsRemoved);
  TEST_ASSERT_TRUE(store.empty());
}

void test_process_budget_slice_repairs_prefix_orphans() {
  setupStore();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(5, 1, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(100, 1, 64, 90)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(200, 1, 64, 0)));

  size_t cursor = 0;
  const auto result =
      CaptureIncrementalSanity::processBudgetSlice(store, kLoopLen, cursor, 2);
  TEST_ASSERT_EQUAL(1u, result.eventsRemoved);
  TEST_ASSERT_EQUAL(2u, store.size());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_repair_completed_pair_removes_orphan_off);
  RUN_TEST(test_repair_completed_pair_keeps_short_pair_during_capture);
  RUN_TEST(test_repair_completed_pair_keeps_short_pair_when_disabled);
  RUN_TEST(test_repair_orphan_note_events_removes_duplicate_on);
  RUN_TEST(test_repair_wrap_window_slice_removes_orphan_off_in_tail);
  RUN_TEST(test_process_budget_slice_repairs_prefix_orphans);
  return UNITY_END();
}
