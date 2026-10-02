#include <unity.h>
#include <initializer_list>

#include "Utils/MidiServiceDrain.h"
#include "Utils/SlotProvisionHold.h"

void test_canonical_prework_prioritizes_runnable_visual_work() {
  using MidiServiceDrain::canonicalPreparationHasPriorityWork;
  TEST_ASSERT_FALSE(canonicalPreparationHasPriorityWork(true, true, false, true));
  TEST_ASSERT_TRUE(canonicalPreparationHasPriorityWork(false, true, false, true));
  TEST_ASSERT_TRUE(canonicalPreparationHasPriorityWork(true, true, true, true));
  TEST_ASSERT_TRUE(canonicalPreparationHasPriorityWork(true, false, false, false));
  TEST_ASSERT_FALSE(canonicalPreparationHasPriorityWork(false, false, false, false));
}

void test_canonical_prefix_turn_has_global_priority_and_one_child() {
  using namespace MidiServiceDrain;
  for (uint8_t owner = 0; owner < 5; ++owner) {
    for (int priority = -1; priority < 4; ++priority) {
      uint8_t cursor = 2;
      unsigned steps = 0;
      const bool ran = serviceCanonicalPrefixTurn(static_cast<DeferredIdleOwner>(owner), 4, cursor,
        [&](uint8_t i) { return i == priority; },
        [&](uint8_t i) { TEST_ASSERT_EQUAL_UINT8(2, i); ++steps; return true; });
      const bool expected = (owner == static_cast<uint8_t>(DeferredIdleOwner::VisualCache) ||
          owner == static_cast<uint8_t>(DeferredIdleOwner::VisualCacheWrapAppend)) && priority == -1;
      TEST_ASSERT_EQUAL(expected, ran);
      TEST_ASSERT_EQUAL_UINT32(expected ? 1 : 0, steps);
      TEST_ASSERT_EQUAL_UINT8(expected ? 3 : 2, cursor);
    }
  }
}

void test_canonical_prefix_turn_rotates_and_retains_denied_work() {
  using namespace MidiServiceDrain;
  uint8_t cursor = 3;
  unsigned visits = 0, children = 0;
  TEST_ASSERT_FALSE(serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 0, cursor,
    [](uint8_t) { TEST_FAIL_MESSAGE("Empty track set must not be read"); return false; },
    [](uint8_t) { TEST_FAIL_MESSAGE("Empty track set must not be serviced"); return true; }));
  TEST_ASSERT_FALSE(serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 4, cursor,
    [](uint8_t) { return false; }, [&](uint8_t) { ++visits; return false; }));
  TEST_ASSERT_EQUAL_UINT32(4, visits); TEST_ASSERT_EQUAL_UINT8(3, cursor);
  for (uint8_t selected : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3)}) {
    TEST_ASSERT_TRUE(serviceCanonicalPrefixTurn(DeferredIdleOwner::VisualCache, 4, cursor,
      [](uint8_t) { return false; }, [&](uint8_t i) {
        if (i != selected) return false;
        ++children; return true;
      }));
    TEST_ASSERT_EQUAL_UINT8((selected + 1) % 4, cursor);
  }
  TEST_ASSERT_EQUAL_UINT32(4, children);
}

void test_stopping_retains_midi_polls_and_excludes_pending_save() {
  TEST_ASSERT_TRUE(MidiServiceDrain::aroundIdleMaintenance(false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::aroundDeferredLoadFrame(false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::afterDeferredDisplay(true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::allowDeferredSave(false, true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::aroundIdleMaintenance(false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::aroundDeferredLoadFrame(false, false));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredSave(false, true, false));
}

void test_active_load_display_selects_exactly_one_child() {
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::Display),
                    static_cast<int>(MidiServiceDrain::selectActiveLoadDisplayChild(true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::LoadJob),
                    static_cast<int>(MidiServiceDrain::selectActiveLoadDisplayChild(false, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::None),
                    static_cast<int>(MidiServiceDrain::selectActiveLoadDisplayChild(false, false)));
}

void test_active_load_job_denies_unbounded_commit() {
  TEST_ASSERT_TRUE(MidiServiceDrain::admitLoadJobChild(false, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::admitLoadJobChild(true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::admitLoadJobChild(true, true));
}

void test_active_load_display_continuation_resumes_saved_child_after_denial() {
  MidiServiceDrain::ActiveLoadDisplayContinuation continuation;

  const auto display = continuation.select(true, true, true);
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::Display),
                    static_cast<int>(display));
  continuation.finish(display, true);

  // An owner-denied turn does not select or finish work. A later eligible turn
  // resumes Display even when a LoadLoopJob is still pending.
  TEST_ASSERT_TRUE(continuation.hasPendingChild());
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::Display),
                    static_cast<int>(continuation.select(true, true, true)));
  continuation.finish(MidiServiceDrain::ActiveLoadDisplayChild::Display, false);

  const auto load = continuation.select(true, false, true);
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::LoadJob),
                    static_cast<int>(load));
  continuation.finish(load, true);

  // The load keeps its progress in StorageManager, but a due display frame
  // receives the next owner turn instead of waiting for the whole load.
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::Display),
                    static_cast<int>(continuation.select(true, true, true)));
  continuation.finish(MidiServiceDrain::ActiveLoadDisplayChild::Display, false);
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::ActiveLoadDisplayChild::LoadJob),
                    static_cast<int>(continuation.select(true, false, true)));
  continuation.finish(MidiServiceDrain::ActiveLoadDisplayChild::LoadJob, false);
  TEST_ASSERT_FALSE(continuation.hasPendingChild());
}

void test_playing_display_keeps_refreshing_with_deferred_load_backlog() {
  using namespace MidiServiceDrain;
  ActiveLoadDisplayContinuation continuation;
  unsigned frames = 0;
  unsigned loadSlices = 0;
  unsigned paintPhase = 0;
  for (unsigned turn = 0; turn < 1000; ++turn) {
    // Model queued background work that remains pending throughout playback.
    // Each frame needs three separate owner turns; a load slice cannot stack
    // on any of them, and must resume between frames.
    const bool displayDue = paintPhase != 0 || turn % 10 == 1;
    const auto child = continuation.select(true, displayDue, true);
    if (displayDue) {
      TEST_ASSERT_EQUAL(static_cast<int>(ActiveLoadDisplayChild::Display),
                        static_cast<int>(child));
      paintPhase = (paintPhase + 1) % 3;
      if (paintPhase == 0) ++frames;
      continuation.finish(child, paintPhase != 0);
    } else {
      TEST_ASSERT_EQUAL(static_cast<int>(ActiveLoadDisplayChild::LoadJob),
                        static_cast<int>(child));
      ++loadSlices;
      continuation.finish(child, true);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(100, frames);
  TEST_ASSERT_EQUAL_UINT32(700, loadSlices);
  // Once the actual backlog is gone, permission to load must not retain it.
  TEST_ASSERT_EQUAL(static_cast<int>(ActiveLoadDisplayChild::None),
                    static_cast<int>(continuation.select(true, false, false)));
  TEST_ASSERT_FALSE(continuation.hasPendingChild());
}

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
      MidiServiceDrain::DeferredIdleOwner::SlotProvision,
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
  };
  for (const MidiServiceDrain::DeferredIdleOwner next : expected) {
    owner = MidiServiceDrain::nextDeferredIdleOwner(owner);
    TEST_ASSERT_EQUAL(static_cast<int>(next), static_cast<int>(owner));
  }
}

void test_select_deferred_idle_owner_absent_matches_seven_owner_cycle() {
  MidiServiceDrain::DeferredIdleOwnerCursor cursor;
  // With no backlog and no session-exit drain the cursor is the plain owner cycle: the six original owners
  // plus the window-build owner the deferred enumeration gained (DEC-108 item 1).
  const MidiServiceDrain::DeferredIdleOwner expected[] = {
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::SlotProvision,
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::SlotProvision,
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance,
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
      MidiServiceDrain::DeferredIdleOwner::SlotProvision,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      // The session-exit drain inserts its extra Snapshot after *every* non-Snapshot owner, so the window-build
      // owner the enumeration gained (DEC-108 item 1) takes its own place in the cycle with an extra after it.
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
  };
  for (const MidiServiceDrain::DeferredIdleOwner next : expected) {
    TEST_ASSERT_EQUAL(static_cast<int>(next),
                      static_cast<int>(MidiServiceDrain::selectDeferredIdleOwner(cursor, true, true)));
  }
}

void test_select_deferred_idle_owner_backlog_snapshot_two_of_eight() {
  MidiServiceDrain::DeferredIdleOwnerCursor cursor;
  const MidiServiceDrain::DeferredIdleOwner expected[] = {
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::SlotProvision,
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::VisualCache,
      MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend,
      MidiServiceDrain::DeferredIdleOwner::Persistence,
      MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay,
      MidiServiceDrain::DeferredIdleOwner::Snapshot,
      MidiServiceDrain::DeferredIdleOwner::SlotProvision,
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance,
  };
  uint8_t snapshotTurns = 0;
  uint8_t visualCacheTurns = 0;
  uint8_t wrapAppendTurns = 0;
  uint8_t persistenceTurns = 0;
  uint8_t loadTurns = 0;
  uint8_t slotProvisionTurns = 0;
  uint8_t playbackViewMaintenanceTurns = 0;
  for (uint8_t i = 0; i < 16; ++i) {
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
      case MidiServiceDrain::DeferredIdleOwner::SlotProvision:
        ++slotProvisionTurns;
        break;
      case MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance:
        ++playbackViewMaintenanceTurns;
        break;
    }
    // One cycle is now **eight** turns: the six original owners, the extra Snapshot the backlog inserts
    // after LoadAndDisplay, and the window-build owner the deferred enumeration gained (DEC-108 item 1).
    // Every owner but Snapshot gets exactly one turn per cycle -- this is the control that the new turn
    // neither displaces nor starves any of the others.
    if ((i + 1) % 8 == 0) {
      TEST_ASSERT_EQUAL_UINT8(2, snapshotTurns);
      TEST_ASSERT_EQUAL_UINT8(1, visualCacheTurns);
      TEST_ASSERT_EQUAL_UINT8(1, wrapAppendTurns);
      TEST_ASSERT_EQUAL_UINT8(1, persistenceTurns);
      TEST_ASSERT_EQUAL_UINT8(1, loadTurns);
      TEST_ASSERT_EQUAL_UINT8(1, slotProvisionTurns);
      TEST_ASSERT_EQUAL_UINT8(1, playbackViewMaintenanceTurns);
      snapshotTurns = 0;
      visualCacheTurns = 0;
      wrapAppendTurns = 0;
      persistenceTurns = 0;
      loadTurns = 0;
      slotProvisionTurns = 0;
      playbackViewMaintenanceTurns = 0;
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

void test_slot_provision_turn_work_is_the_pending_switch_itself() {
  // S4b.7: the `SlotProvision` turn exists exactly while a pending switch's inbound occupancy is absent.
  // No request record is involved, so nothing can consume the eligibility elsewhere -- the pending switch
  // *is* the work, and a switch that is already provisioned belongs to the commit path.
  TEST_ASSERT_TRUE(MidiServiceDrain::slotProvisionNeeded(true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::slotProvisionNeeded(true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::slotProvisionNeeded(false, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::slotProvisionNeeded(false, true));
  // And provisioning does not depend on the visual cache, unlike VisualCache/LoadAndDisplay: a switch must
  // still be provisioned while a wrap append is pending.
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::SlotProvision, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::deferredIdleOwnerDependsOnVisualCacheWrap(
      MidiServiceDrain::DeferredIdleOwner::SlotProvision));
}

// DEC-108 item 1: the window-build owner's turn has work exactly while an owed window exists -- a build
// that is begun and unfinished. State-driven like `slotProvisionNeeded`: nothing else is tracked, and an
// absent runtime owes nothing (the runtime's owner is the provisioning path).
void test_playback_view_maintenance_turn_work_is_an_unfinished_build() {
  TEST_ASSERT_TRUE(MidiServiceDrain::playbackViewMaintenanceNeeded(true, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::playbackViewMaintenanceNeeded(true, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::playbackViewMaintenanceNeeded(false, true));
  TEST_ASSERT_FALSE(MidiServiceDrain::playbackViewMaintenanceNeeded(false, false));
  // Reachability, pure half: the turn is *not* a visual-cache dependent, so a pending wrap append (which
  // skips VisualCache and LoadAndDisplay) cannot skip the window owner. The dispatch placement itself lives
  // in `src/main.cpp`, which is not native-linkable -- see the slice's execution packet.
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance, true));
  TEST_ASSERT_TRUE(MidiServiceDrain::allowDeferredIdleOwner(
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance, false));
  TEST_ASSERT_FALSE(MidiServiceDrain::deferredIdleOwnerDependsOnVisualCacheWrap(
      MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance));
  // The new value is the **last** one, so the enumeration count and the enum agree and no existing owner
  // index silently starts naming a different owner.
  TEST_ASSERT_EQUAL_UINT8(MidiServiceDrain::kDeferredIdleOwnerCount,
                          static_cast<uint8_t>(MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance) + 1);
}

void test_slot_provision_hold_age_is_bounded_and_wrap_safe() {
  // S4b.7: the hold's age test. The bound is a duration in ticks, so it must be measured as a *difference*
  // (the tick domain wraps) and must never fire for a switch that has no queued tick at all.
  constexpr uint32_t kBound = 4 * 768;
  TEST_ASSERT_FALSE_MESSAGE(SlotProvisionHold::holdExceeded(5000, UINT32_MAX, kBound),
                            "a switch with no queued tick has no age to exceed");
  TEST_ASSERT_FALSE(SlotProvisionHold::holdExceeded(2000, 1000, kBound));
  TEST_ASSERT_FALSE_MESSAGE(SlotProvisionHold::holdExceeded(1000 + kBound - 1, 1000, kBound),
                            "one tick short of the bound is not the bound");
  TEST_ASSERT_TRUE_MESSAGE(SlotProvisionHold::holdExceeded(1000 + kBound, 1000, kBound),
                           "the bound is inclusive: at it, the hold is reported");
  TEST_ASSERT_TRUE(SlotProvisionHold::holdExceeded(1000 + kBound + 1, 1000, kBound));
  // Wrap: a switch queued just before the tick domain rolls over is still measured correctly -- 16 ticks of
  // age for a queued tick five ticks below the top, and past the bound once the domain wraps.
  TEST_ASSERT_FALSE(SlotProvisionHold::holdExceeded(10, UINT32_MAX - 5, kBound));
  TEST_ASSERT_TRUE(SlotProvisionHold::holdExceeded(kBound, UINT32_MAX - 5, kBound));
  // A zero bound means "unbounded", not "immediately": the caller opts out by passing zero.
  TEST_ASSERT_FALSE(SlotProvisionHold::holdExceeded(UINT32_MAX - 1, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(kBound, SlotProvisionHold::boundTicksFor(768));
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

void test_background_snapshot_is_one_unit_while_selected_keeps_normal_budget() {
  TEST_ASSERT_EQUAL_UINT8(2u, MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn);
  TEST_ASSERT_EQUAL_UINT32(8000u, MidiServiceDrain::snapshotIdleConstructionCostUs(
                                        MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn));
  TEST_ASSERT_EQUAL_UINT32(4000u, MidiServiceDrain::snapshotIdleConstructionCostUs(1));
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
  RUN_TEST(test_canonical_prework_prioritizes_runnable_visual_work);
  RUN_TEST(test_canonical_prefix_turn_has_global_priority_and_one_child);
  RUN_TEST(test_canonical_prefix_turn_rotates_and_retains_denied_work);
  RUN_TEST(test_stopping_retains_midi_polls_and_excludes_pending_save);
  RUN_TEST(test_active_load_display_selects_exactly_one_child);
  RUN_TEST(test_active_load_job_denies_unbounded_commit);
  RUN_TEST(test_active_load_display_continuation_resumes_saved_child_after_denial);
  RUN_TEST(test_playing_display_keeps_refreshing_with_deferred_load_backlog);
  RUN_TEST(test_around_idle_only_when_post_overdub_playing_drain);
  RUN_TEST(test_after_display_keeps_capture_and_adds_post_overdub);
  RUN_TEST(test_around_load_frame_only_when_post_overdub_playing_drain);
  RUN_TEST(test_deferred_idle_owner_rotates_unconditionally);
  RUN_TEST(test_select_deferred_idle_owner_absent_matches_seven_owner_cycle);
  RUN_TEST(test_select_deferred_idle_owner_backlog_snapshot_two_of_eight);
  // S4b.7: the `SlotProvision` turn's eligibility is the pending switch itself, and it does not depend on
  // the visual cache -- a switch must still be provisioned while a wrap append is pending.
  RUN_TEST(test_slot_provision_turn_work_is_the_pending_switch_itself);
  // DEC-108 item 1: the window-build owner's eligibility, and the proof that the deferred enumeration it
  // joined keeps every owner's turn (see the rotation fixtures above).
  RUN_TEST(test_playback_view_maintenance_turn_work_is_an_unfinished_build);
  RUN_TEST(test_slot_provision_hold_age_is_bounded_and_wrap_safe);
  RUN_TEST(test_select_deferred_idle_owner_session_exit_drain_extra_snapshot);
  RUN_TEST(test_deferred_idle_owner_skips_only_visual_dependents_for_pending_wrap);
  RUN_TEST(test_live_record_snapshot_starts_after_first_bar);
  RUN_TEST(test_first_record_snapshot_idle_matches_overdub_playing_start);
  RUN_TEST(test_stopped_track_snapshot_pauses_during_realtime);
  RUN_TEST(test_background_snapshot_is_one_unit_while_selected_keeps_normal_budget);
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
