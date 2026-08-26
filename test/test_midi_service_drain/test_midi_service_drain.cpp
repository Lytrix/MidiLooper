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

void test_snapshot_round_robin_applies_only_to_unpublished_active_timing() {
  TEST_ASSERT_TRUE(MidiServiceDrain::shouldRoundRobinSnapshotIdle(
      false, true, false, true, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::shouldRoundRobinSnapshotIdle(
      true, false, false, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRoundRobinSnapshotIdle(
      false, false, false, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRoundRobinSnapshotIdle(
      true, false, true, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRoundRobinSnapshotIdle(
      true, false, false, false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::shouldRoundRobinSnapshotIdle(
      true, false, false, true, false));
}

void test_live_record_snapshot_starts_after_first_bar() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowLiveRecordSnapshotStart(false, 0, 768));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowLiveRecordSnapshotStart(true, 0, 768));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowLiveRecordSnapshotStart(true, 767, 768));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowLiveRecordSnapshotStart(true, 768, 768));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowLiveRecordSnapshotStart(true, 769, 768));
  TEST_ASSERT_EQUAL(0u, MidiServiceDrain::liveRecordCompletedTicks(767, 768));
  TEST_ASSERT_EQUAL(768u, MidiServiceDrain::liveRecordCompletedTicks(768, 768));
  TEST_ASSERT_EQUAL(768u, MidiServiceDrain::liveRecordCompletedTicks(1535, 768));
  TEST_ASSERT_EQUAL(1536u, MidiServiceDrain::liveRecordCompletedTicks(1536, 768));
}

void test_first_record_snapshot_idle_matches_overdub_playing_start() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      true, false, false, false, false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, false, false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, false, true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, true, true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      true, false, false, true, false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, false, false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, false, true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, true, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, true, false, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_around_idle_only_when_post_overdub_playing_drain);
  RUN_TEST(test_after_display_keeps_capture_and_adds_post_overdub);
  RUN_TEST(test_post_stop_persist_snapshot_lane_alternates_when_both_pending);
  RUN_TEST(test_snapshot_round_robin_applies_only_to_unpublished_active_timing);
  RUN_TEST(test_live_record_snapshot_starts_after_first_bar);
  RUN_TEST(test_first_record_snapshot_idle_matches_overdub_playing_start);
  return UNITY_END();
}
