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

void test_select_deferred_idle_owner_absent_matches_five_owner_cycle() {
  MidiServiceDrain::DeferredIdleOwnerCursor cursor;
  const MidiServiceDrain::DeferredIdleOwner expected[] = {
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
  };
  for (const MidiServiceDrain::DeferredIdleOwner next : expected) {
    TEST_ASSERT_EQUAL(static_cast<int>(next),
                      static_cast<int>(MidiServiceDrain::selectDeferredIdleOwner(cursor, false)));
  }
}

void test_select_deferred_idle_owner_session_exit_drain_extra_snapshot() {
  MidiServiceDrain::DeferredIdleOwnerCursor cursor;
  const MidiServiceDrain::DeferredIdleOwner expected[] = {
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
  };
  for (const MidiServiceDrain::DeferredIdleOwner next : expected) {
    TEST_ASSERT_EQUAL(static_cast<int>(next),
                      static_cast<int>(MidiServiceDrain::selectDeferredIdleOwner(cursor, true, true)));
  }
}

void test_select_deferred_idle_owner_backlog_snapshot_two_of_six() {
  MidiServiceDrain::DeferredIdleOwnerCursor cursor;
  const MidiServiceDrain::DeferredIdleOwner expected[] = {
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
  };
  uint8_t snapshotTurns = 0;
  uint8_t visualCacheTurns = 0;
  uint8_t wrapAppendTurns = 0;
  uint8_t persistenceTurns = 0;
  uint8_t loadTurns = 0;
  for (uint8_t i = 0; i < 12; ++i) {
    const MidiServiceDrain::DeferredIdleOwner selected =
        MidiServiceDrain::selectDeferredIdleOwner(cursor, true);
    TEST_ASSERT_EQUAL(static_cast<int>(expected[i]), static_cast<int>(selected));
    switch (selected) {
      case MidiServiceDrain::DeferredIdleOwner::Snapshot:
        ++snapshotTurns;
        break;
      case MidiServiceDrain::DeferredIdleOwner::VisualCache:
        ++visualCacheTurns;
        break;
      case MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend:
        ++wrapAppendTurns;
        break;
      case MidiServiceDrain::DeferredIdleOwner::Persistence:
        ++persistenceTurns;
        break;
      case MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay:
        ++loadTurns;
        break;
    }
    if ((i + 1) % 6 == 0) {
      TEST_ASSERT_EQUAL_UINT8(2, snapshotTurns);
      TEST_ASSERT_EQUAL_UINT8(1, visualCacheTurns);
      TEST_ASSERT_EQUAL_UINT8(1, wrapAppendTurns);
      TEST_ASSERT_EQUAL_UINT8(1, persistenceTurns);
      TEST_ASSERT_EQUAL_UINT8(1, loadTurns);
      snapshotTurns = 0;
      visualCacheTurns = 0;
      wrapAppendTurns = 0;
      persistenceTurns = 0;
      loadTurns = 0;
    }
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

void test_stopped_track_snapshot_pauses_during_realtime() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowStoppedTrackSnapshotDuringRealtime(false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowStoppedTrackSnapshotDuringRealtime(true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowStoppedTrackSnapshotDuringRealtime(true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowStoppedTrackSnapshotDuringRealtime(false, true));
}

void test_snapshot_idle_pauses_during_boot_title_drain() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleDuringBootTitleDrain(false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleDuringBootTitleDrain(true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleDuringBootTitleDrain(true, true));
}

void test_loaded_snapshot_hydration_stays_off_after_boot() {
  TEST_ASSERT_FALSE(MidiServiceDrain::allowLoadedSnapshotHydration(true, false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowLoadedSnapshotHydration(false, true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowLoadedSnapshotHydration(false, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowLoadedSnapshotHydration(false, false, false));
}

void test_stopped_clock_snapshot_requires_pending_work() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowStoppedClockSnapshotStep(false, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowStoppedClockSnapshotStep(true, false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowStoppedClockSnapshotStep(true, true, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowStoppedClockSnapshotStep(true, true, true));
}

void test_boot_title_snapshot_stays_on_focus_track() {
  const bool pending[] = {false, false, true, false};
  TEST_ASSERT_EQUAL_UINT8(1, MidiServiceDrain::selectBootTitleSnapshotTrack(
                                 true, 1, 0, 4, pending));
  TEST_ASSERT_EQUAL_UINT8(2, MidiServiceDrain::selectBootTitleSnapshotTrack(
                                 false, 1, 0, 4, pending));
}

void test_stopped_loaded_snapshot_prefers_focus_then_in_flight() {
  const bool pending[] = {true, false, true, false};
  const bool noneInFlight[] = {false, false, false, false};
  TEST_ASSERT_EQUAL_UINT8(0, MidiServiceDrain::selectStoppedLoadedSnapshotTrack(
                                 0, 2, 4, pending, noneInFlight));
  TEST_ASSERT_EQUAL_UINT8(2, MidiServiceDrain::selectStoppedLoadedSnapshotTrack(
                                 2, 0, 4, pending, noneInFlight));
  const bool focusIdle[] = {false, false, true, false};
  const bool inFlight[] = {true, false, false, false};
  TEST_ASSERT_EQUAL_UINT8(1, MidiServiceDrain::selectStoppedLoadedSnapshotTrack(
                                 1, 2, 4, focusIdle, inFlight));
  TEST_ASSERT_EQUAL_UINT8(1, MidiServiceDrain::selectStoppedLoadedSnapshotTrack(
                                 1, 0, 4, focusIdle, noneInFlight));
}

void test_stopped_snapshot_track_prefers_pending_slot() {
  const bool pending[] = {false, false, true, false};
  TEST_ASSERT_EQUAL_UINT8(2, MidiServiceDrain::selectStoppedSnapshotTrack(0, 4, pending));
  TEST_ASSERT_EQUAL_UINT8(2, MidiServiceDrain::selectStoppedSnapshotTrack(2, 4, pending));
  TEST_ASSERT_EQUAL_UINT8(2, MidiServiceDrain::selectStoppedSnapshotTrack(3, 4, pending));
  const bool none[] = {false, false, false, false};
  TEST_ASSERT_EQUAL_UINT8(1, MidiServiceDrain::selectStoppedSnapshotTrack(1, 4, none));
}

void test_allow_snapshot_construction_start_before_first_root_session_exit() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotConstructionStart(
      false, true, false, false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotConstructionStart(
      true, false, false, false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotConstructionStart(
      true, true, false, false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotConstructionStart(
      true, true, false, true, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotConstructionStart(
      false, false, true, true, false));
}

void test_visual_cache_reconstruct_slice_off_during_overdub() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowVisualCacheReconstructSlice(false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowVisualCacheReconstructSlice(true));
}

void test_snapshot_idle_reserve_admits_two_unit_fit() {
  const uint32_t cost = MidiServiceDrain::snapshotIdleConstructionCostUs(
      MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn);
  TEST_ASSERT_EQUAL_UINT32(8000u, cost);
  const uint32_t remaining = cost + MidiServiceDrain::kSnapshotIdleSafetyMarginUs + 1u;
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleReserve(remaining, true));
}

void test_snapshot_idle_reserve_admits_exact_two_unit_fit() {
  const uint32_t remaining =
      MidiServiceDrain::snapshotIdleConstructionCostUs(
          MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn) +
      MidiServiceDrain::kSnapshotIdleSafetyMarginUs;
  TEST_ASSERT_EQUAL_UINT32(
      MidiServiceDrain::snapshotIdleConstructionCostUs(2),
      MidiServiceDrain::admittedSnapshotIdleUs(remaining));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleReserve(remaining, true));
}

void test_snapshot_idle_reserve_yields_when_two_units_do_not_fit() {
  const uint32_t remaining =
      MidiServiceDrain::snapshotIdleConstructionCostUs(
          MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn) +
      MidiServiceDrain::kSnapshotIdleSafetyMarginUs - 1u;
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleReserve(remaining, true));
}

void test_snapshot_idle_reserve_floors_margin_to_zero() {
  TEST_ASSERT_EQUAL_UINT32(0u, MidiServiceDrain::admittedSnapshotIdleUs(0));
  TEST_ASSERT_EQUAL_UINT32(
      0u, MidiServiceDrain::admittedSnapshotIdleUs(MidiServiceDrain::kSnapshotIdleSafetyMarginUs));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleReserve(0, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowSnapshotIdleReserve(
      MidiServiceDrain::kSnapshotIdleSafetyMarginUs, true));
}

void test_snapshot_idle_reserve_caps_at_ceiling() {
  TEST_ASSERT_EQUAL_UINT32(MidiServiceDrain::kSnapshotIdleReserveCeilingUs,
                           MidiServiceDrain::admittedSnapshotIdleUs(40000u));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleReserve(40000u, true));
}

void test_snapshot_idle_reserve_bypasses_when_not_timing_critical() {
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleReserve(0, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotIdleReserve(1, false));
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
  RUN_TEST(test_select_deferred_idle_owner_absent_matches_five_owner_cycle);
  RUN_TEST(test_select_deferred_idle_owner_backlog_snapshot_two_of_six);
  RUN_TEST(test_select_deferred_idle_owner_session_exit_drain_extra_snapshot);
  RUN_TEST(test_deferred_idle_owner_skips_only_visual_dependents_for_pending_wrap);
  RUN_TEST(test_live_record_snapshot_starts_after_first_bar);
  RUN_TEST(test_first_record_snapshot_idle_matches_overdub_playing_start);
  RUN_TEST(test_stopped_track_snapshot_pauses_during_realtime);
  RUN_TEST(test_snapshot_idle_pauses_during_boot_title_drain);
  RUN_TEST(test_loaded_snapshot_hydration_stays_off_after_boot);
  RUN_TEST(test_stopped_clock_snapshot_requires_pending_work);
  RUN_TEST(test_boot_title_snapshot_stays_on_focus_track);
  RUN_TEST(test_stopped_loaded_snapshot_prefers_focus_then_in_flight);
  RUN_TEST(test_stopped_snapshot_track_prefers_pending_slot);
  RUN_TEST(test_snapshot_idle_reserve_admits_two_unit_fit);
  RUN_TEST(test_snapshot_idle_reserve_admits_exact_two_unit_fit);
  RUN_TEST(test_snapshot_idle_reserve_yields_when_two_units_do_not_fit);
  RUN_TEST(test_snapshot_idle_reserve_floors_margin_to_zero);
  RUN_TEST(test_snapshot_idle_reserve_caps_at_ceiling);
  RUN_TEST(test_snapshot_idle_reserve_bypasses_when_not_timing_critical);
  RUN_TEST(test_stopped_pending_snapshot_wins_over_first_paint);
  RUN_TEST(test_allow_snapshot_construction_start_before_first_root_session_exit);
  RUN_TEST(test_visual_cache_reconstruct_slice_off_during_overdub);
  return UNITY_END();
}
