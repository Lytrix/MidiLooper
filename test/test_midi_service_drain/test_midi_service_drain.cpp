#include <unity.h>

#include "Utils/MidiServiceDrain.h"

void setUp() {}
void tearDown() {}

void test_around_idle_only_when_post_overdub_playing_drain() {
  TEST_ASSERT_FALSE(MidiServiceDrain::aroundIdleMaintenance(false));
  TEST_ASSERT_TRUE(MidiServiceDrain::aroundIdleMaintenance(true));
}

void test_after_display_keeps_capture_and_adds_post_overdub() {
  TEST_ASSERT_FALSE(MidiServiceDrain::afterDeferredDisplay(false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::afterDeferredDisplay(true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::afterDeferredDisplay(false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::afterDeferredDisplay(true, true));
}

void test_post_stop_persist_snapshot_lane_alternates_when_both_pending() {
  TEST_ASSERT_TRUE(MidiServiceDrain::shouldRunPersistOnPostStopLane(true, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRunSnapshotOnPostStopLane(true, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRunPersistOnPostStopLane(true, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::shouldRunSnapshotOnPostStopLane(true, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::shouldRunPersistOnPostStopLane(false, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::shouldRunSnapshotOnPostStopLane(true, false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRunPersistOnPostStopLane(true, false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRunSnapshotOnPostStopLane(false, true, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_around_idle_only_when_post_overdub_playing_drain);
  RUN_TEST(test_after_display_keeps_capture_and_adds_post_overdub);
  RUN_TEST(test_post_stop_persist_snapshot_lane_alternates_when_both_pending);
  return UNITY_END();
}
