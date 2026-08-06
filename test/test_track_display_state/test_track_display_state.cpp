//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Track/TrackDisplayState.cpp"
#include "TrackDisplayState.h"

void test_applyLoadedTrackState_stopped_becomes_empty_when_no_slots() {
  TEST_ASSERT_EQUAL(TRACK_EMPTY, normalizeLoadedTrackState(TRACK_STOPPED, false));
}

void test_applyLoadedTrackState_stopped_kept_when_slots_have_events() {
  TEST_ASSERT_EQUAL(TRACK_STOPPED, normalizeLoadedTrackState(TRACK_STOPPED, true));
}

void test_getTrackState_empty_selected_slot() {
  TEST_ASSERT_EQUAL(TRACK_EMPTY,
                    resolveDisplayTrackState(TRACK_STOPPED, SlotOpState::SLOT_OP_IDLE, false, false,
                                             false, false));
}

void test_getTrackState_recording_on_selected_slot() {
  TEST_ASSERT_EQUAL(TRACK_RECORDING,
                    resolveDisplayTrackState(TRACK_STOPPED, SlotOpState::SLOT_OP_RECORDING, false,
                                             false, false, false));
}

void test_getTrackState_armed_when_record_queued_on_empty_selected_slot() {
  TEST_ASSERT_EQUAL(TRACK_ARMED, resolveDisplayTrackState(TRACK_PLAYING, SlotOpState::SLOT_OP_IDLE,
                                                          false, false, false, true));
}

void test_getTrackState_not_armed_when_record_queued_on_slot_with_published_midi() {
  TEST_ASSERT_EQUAL(TRACK_PLAYING,
                    resolveDisplayTrackState(TRACK_PLAYING, SlotOpState::SLOT_OP_IDLE, true, true,
                                             false, true));
  TEST_ASSERT_EQUAL(TRACK_STOPPED,
                    resolveDisplayTrackState(TRACK_ARMED, SlotOpState::SLOT_OP_IDLE, true, true,
                                             false, true));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_applyLoadedTrackState_stopped_becomes_empty_when_no_slots);
  RUN_TEST(test_applyLoadedTrackState_stopped_kept_when_slots_have_events);
  RUN_TEST(test_getTrackState_empty_selected_slot);
  RUN_TEST(test_getTrackState_recording_on_selected_slot);
  RUN_TEST(test_getTrackState_armed_when_record_queued_on_empty_selected_slot);
  RUN_TEST(test_getTrackState_not_armed_when_record_queued_on_slot_with_published_midi);
  return UNITY_END();
}
