//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Multi-slot clear / arm transport invariants (native predicates).

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPool.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/TrackStateMachine.cpp"
#include "../../src/TrackDisplayState.cpp"

#include "Loop.h"
#include "TrackDisplayState.h"
#include "TrackState.h"

namespace {

bool anySlotHasLoopData(const LoopPool& pool) {
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (pool.at(s).hasData()) {
      return true;
    }
  }
  return false;
}

TrackState transportAfterCancelArm(bool anySlotHasData) {
  return anySlotHasData ? TRACK_STOPPED : TRACK_EMPTY;
}

TrackState transportAfterEmptyRecordStop(bool anySlotHasData) {
  return anySlotHasData ? TRACK_STOPPED : TRACK_EMPTY;
}

TrackState transportAfterClearActiveSlot(bool anySlotHasData, TrackState priorTransport,
                                         bool activeSlotHasData) {
  if (anySlotHasData) {
    if (priorTransport == TRACK_PLAYING && !activeSlotHasData) {
      return TRACK_STOPPED;
    }
    if (priorTransport == TRACK_PLAYING || priorTransport == TRACK_OVERDUBBING) {
      return priorTransport;
    }
    if (priorTransport == TRACK_EMPTY || priorTransport == TRACK_ARMED) {
      return TRACK_STOPPED;
    }
    return priorTransport;
  }
  return TRACK_EMPTY;
}

}  // namespace

void test_any_slot_has_data_when_sibling_filled() {
  LoopPool pool;
  pool.ensureInitialized();
  pool.at(2).loopLengthTicks = 0;
  pool.at(1).loopLengthTicks = 768;
  TEST_ASSERT_FALSE(pool.at(2).hasData());
  TEST_ASSERT_TRUE(pool.at(1).hasData());
  TEST_ASSERT_TRUE(anySlotHasLoopData(pool));
}

void test_any_slot_has_data_false_when_all_empty() {
  LoopPool pool;
  pool.ensureInitialized();
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    pool.at(s).loopLengthTicks = 0;
  }
  TEST_ASSERT_FALSE(anySlotHasLoopData(pool));
}

void test_cancel_arm_with_sibling_data_goes_stopped_not_empty() {
  TEST_ASSERT_EQUAL(TRACK_STOPPED, transportAfterCancelArm(true));
  TEST_ASSERT_EQUAL(TRACK_EMPTY, transportAfterCancelArm(false));
}

void test_empty_record_stop_with_sibling_data_stays_stopped() {
  TEST_ASSERT_EQUAL(TRACK_STOPPED, transportAfterEmptyRecordStop(true));
  TEST_ASSERT_EQUAL(TRACK_EMPTY, transportAfterEmptyRecordStop(false));
}

void test_clear_active_slot_stops_playing_when_active_empty() {
  // Sibling still has data, but cleared active has none — must leave PLAYING (re-arm).
  TEST_ASSERT_EQUAL(TRACK_STOPPED,
                    transportAfterClearActiveSlot(true, TRACK_PLAYING, false));
  TEST_ASSERT_EQUAL(TRACK_PLAYING,
                    transportAfterClearActiveSlot(true, TRACK_PLAYING, true));
  TEST_ASSERT_EQUAL(TRACK_STOPPED,
                    transportAfterClearActiveSlot(true, TRACK_STOPPED, false));
  TEST_ASSERT_EQUAL(TRACK_EMPTY,
                    transportAfterClearActiveSlot(false, TRACK_PLAYING, false));
}

void test_display_shows_empty_when_selected_slot_cleared_but_transport_stopped() {
  TEST_ASSERT_EQUAL(TRACK_EMPTY,
                    resolveDisplayTrackState(TRACK_STOPPED, SlotOpState::SLOT_OP_IDLE, false, false,
                                             false, false));
  TEST_ASSERT_EQUAL(TRACK_STOPPED,
                    resolveDisplayTrackState(TRACK_STOPPED, SlotOpState::SLOT_OP_IDLE, true, false,
                                             false, false));
}

void test_armed_display_when_record_queued_on_empty_selected_slot() {
  TEST_ASSERT_EQUAL(TRACK_ARMED, resolveDisplayTrackState(TRACK_STOPPED, SlotOpState::SLOT_OP_IDLE,
                                                          false, false, false, true));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_any_slot_has_data_when_sibling_filled);
  RUN_TEST(test_any_slot_has_data_false_when_all_empty);
  RUN_TEST(test_cancel_arm_with_sibling_data_goes_stopped_not_empty);
  RUN_TEST(test_empty_record_stop_with_sibling_data_stays_stopped);
  RUN_TEST(test_clear_active_slot_stops_playing_when_active_empty);
  RUN_TEST(test_display_shows_empty_when_selected_slot_cleared_but_transport_stopped);
  RUN_TEST(test_armed_display_when_record_queued_on_empty_selected_slot);
  return UNITY_END();
}
