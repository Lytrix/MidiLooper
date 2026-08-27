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

void test_around_load_frame_only_when_post_overdub_playing_drain() {
  TEST_ASSERT_FALSE(MidiServiceDrain::aroundDeferredLoadFrame(false));
  TEST_ASSERT_TRUE(MidiServiceDrain::aroundDeferredLoadFrame(true));
}

void test_deferred_idle_owner_rotates_unconditionally() {
  MidiServiceDrain::DeferredIdleOwner owner =
      MidiServiceDrain::DeferredIdleOwner::Snapshot;
  const MidiServiceDrain::DeferredIdleOwner expected[] = {
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
  };
  for (const MidiServiceDrain::DeferredIdleOwner next : expected) {
    owner = MidiServiceDrain::nextDeferredIdleOwner(owner);
    TEST_ASSERT_EQUAL(static_cast<int>(next), static_cast<int>(owner));
  }
}

void test_deferred_idle_owner_skips_only_visual_dependents_for_pending_wrap() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::Snapshot, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::VisualCache, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::Persistence, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay, false));
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
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, true, true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, true, false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, true, false, false, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, true, false, true, false, false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      true, false, false, true, false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, false, false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, false, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, true, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, true, true, false, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, true, false, false));
}

void test_stopped_pending_snapshot_wins_over_first_paint() {
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, true, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, true, false, true, false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, true, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleStep(
      false, false, false, false, false, true, true, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_around_idle_only_when_post_overdub_playing_drain);
  RUN_TEST(test_after_display_keeps_capture_and_adds_post_overdub);
  RUN_TEST(test_around_load_frame_only_when_post_overdub_playing_drain);
  RUN_TEST(test_deferred_idle_owner_rotates_unconditionally);
  RUN_TEST(test_deferred_idle_owner_skips_only_visual_dependents_for_pending_wrap);
  RUN_TEST(test_live_record_snapshot_starts_after_first_bar);
  RUN_TEST(test_first_record_snapshot_idle_matches_overdub_playing_start);
  RUN_TEST(test_stopped_pending_snapshot_wins_over_first_paint);
  return UNITY_END();
}
