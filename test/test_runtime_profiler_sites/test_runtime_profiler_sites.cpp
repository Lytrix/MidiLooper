// Runtime profiler slice 3 (DEC-239): the four DEC-231 preparation-path scopes on the production owners.
// Durations are not asserted here (the record path is covered by test_runtime_profiler); this suite
// proves each production path enters its identity, that a prepared Record edge never reaches the
// monolithic `prewarmViews`, and that the scopes leave the owners' results unchanged.
#define RUNTIME_PROFILE_SCOPES 1
#define HARNESS_REAL_PLAYBACK_HOT_PATH
#define HARNESS_REAL_CAPTURE_STOP
#define HARNESS_OBSERVABLE_TRACK_SELECT
#define HARNESS_OBSERVABLE_SLOT_CHANGE
#include "../test_support/capture_storage_harness.inc"

#include "Utils/RuntimeProfiler.h"
#include "Utils/PlaybackTickContext.h"

namespace {
uint32_t gProfileCycles = 0;
}  // namespace

namespace RuntimeProfiler::Platform {
uint32_t cycles() { return gProfileCycles += 600; }  // 1 us per read: every call has a non-zero duration
uint32_t micros() { return ::micros(); }
uint32_t ipsr() { return 0; }
uint32_t cpuHz() { return 600000000u; }
void emitRow(const char*) {}
void emitRetainedRow(const char*) {}
}  // namespace RuntimeProfiler::Platform

#include "../../src/Utils/ProfileDiagRows.cpp"
#include "../../src/Utils/RuntimeProfiler.cpp"

using RuntimeProfiler::ProfileId;

namespace {
uint32_t calls(ProfileId id) { return RuntimeProfiler::stat(id).calls; }
}  // namespace

void setUp() {
  PersistenceWorkQueue::resetForTests();
  StorageManagerInternal::storageSession.currentWorkspaceSave.pending = false;
  StorageManagerInternal::storageSession.currentWorkspaceSave.urgentRequested = false;
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  nativeTransportRunning = true;
  nativePlaybackDeadlineClock = {};
  clockManager.assignCurrentTickSilently(0);
  nativeMillisSet(0);
  for (auto& row : StorageManagerInternal::currentSetLoopSlotDirty) row.fill(false);
  Track::viewPrewarmStrictForTest() = false;
  monolithicViewPrewarmCallsForTest() = 0;
  RuntimeProfiler::initAtBoot();
}
void tearDown() {}

/// Boot/load shape: `prewarmPlaybackForSlot` with capture preparation reaches the monolithic views.
void test_slot_prewarm_and_monolithic_views_are_profiled() {
  Track& t = trackManager.getTrack(0);
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, false));
  TEST_ASSERT_EQUAL_UINT32(1, calls(ProfileId::PlaybackPrewarm_slot));
  const uint32_t viewsBefore = calls(ProfileId::PlaybackPrewarm_views);
  const uint32_t monolithicBefore = monolithicViewPrewarmCallsForTest();
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_EQUAL_UINT32(2, calls(ProfileId::PlaybackPrewarm_slot));
  // The profile count moves exactly with the production counter: one scope per real call.
  TEST_ASSERT_EQUAL_UINT32(monolithicViewPrewarmCallsForTest() - monolithicBefore,
                           calls(ProfileId::PlaybackPrewarm_views) - viewsBefore);
  TEST_ASSERT_TRUE(calls(ProfileId::PlaybackPrewarm_views) > viewsBefore);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::counters().depth);
  TEST_ASSERT_FALSE(RuntimeProfiler::counters().poisoned);
}

/// DEC-231 prepared edge: the units are profiled with their result, the edge is profiled, and the
/// edge never enters the monolithic views (its count does not move across the edge).
void test_prepared_record_edge_profiles_units_and_never_the_monolithic_views() {
  Track& t = trackManager.getTrack(0);
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, false));
  t.setActiveLoopIndex(0);
  Track::viewPrewarmStrictForTest() = true;
  t.requestViewPrewarmForSlot(0);
  uint32_t steps = 0;
  for (uint16_t unit = 0; unit < 256 && !t.viewPrewarmReadyForSlot(0); ++unit) {
    (void)t.stepViewPrewarmForSlot(0, 1, 5000);
    ++steps;
  }
  TEST_ASSERT_TRUE(t.viewPrewarmReadyForSlot(0));
  TEST_ASSERT_EQUAL_UINT32(steps, calls(ProfileId::ViewPrewarm_unit));
  TEST_ASSERT_EQUAL_UINT8(1, RuntimeProfiler::stat(ProfileId::ViewPrewarm_unit).lastResult);  // ready
  const uint32_t viewsBefore = calls(ProfileId::PlaybackPrewarm_views);
  const uint32_t slotBefore = calls(ProfileId::PlaybackPrewarm_slot);
  TEST_ASSERT_TRUE(t.startRecording(100));
  TEST_ASSERT_TRUE(t.isRecording());
  TEST_ASSERT_EQUAL_UINT32(1, calls(ProfileId::RecordStart_edge));
  TEST_ASSERT_TRUE(calls(ProfileId::PlaybackPrewarm_slot) > slotBefore);  // the edge's own prewarm
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(viewsBefore, calls(ProfileId::PlaybackPrewarm_views),
                                   "the prepared Record edge must not enter the monolithic prewarmViews");
  // Nesting: the edge's prewarm ran inside the edge, so the edge's self time excludes it.
  const auto& edge = RuntimeProfiler::stat(ProfileId::RecordStart_edge);
  TEST_ASSERT_TRUE(edge.selfCycles < edge.inclusiveCycles);
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::counters().depth);
  Track::viewPrewarmStrictForTest() = false;
}

/// DEC-231 cold edge: the not-prepared `return false` is a timed exit and the result is unchanged.
void test_cold_record_edge_is_a_timed_refusal() {
  Track& t = trackManager.getTrack(0);
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, false));
  t.setActiveLoopIndex(0);
  Track::viewPrewarmStrictForTest() = true;
  TEST_ASSERT_FALSE(t.startRecording(100));
  TEST_ASSERT_FALSE(t.isRecording());
  TEST_ASSERT_EQUAL_UINT32(1, calls(ProfileId::RecordStart_edge));
  TEST_ASSERT_EQUAL_UINT16(0, t.viewPrewarmProgressForSlot(0));  // owner behaviour unchanged
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::counters().depth);
  Track::viewPrewarmStrictForTest() = false;
}


// ---- slice 4: the remaining bodies reachable in this harness ----

namespace {
/// A committed one-bar loop in slot 0, the shape the shared finalization suite uses.
Track& playingTrackWithContent() {
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  t.setActiveLoopIndex(0);
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, false));
  t.forceSetState(TRACK_PLAYING);
  return t;
}
}  // namespace

void test_playback_window_build_is_profiled_but_not_its_reuse_poll() {
  Track& t = playingTrackWithContent();
  const uint32_t before = calls(ProfileId::PlaybackWindow_build);
  t.ensurePlaybackMergedEventsForSlot(0);
  const uint32_t afterBuild = calls(ProfileId::PlaybackWindow_build);
  TEST_ASSERT_TRUE(afterBuild > before);
  // The window now covers the playhead: the reuse return is the poll and must not be measured.
  t.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_EQUAL_UINT32(afterBuild, calls(ProfileId::PlaybackWindow_build));
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::counters().depth);
}

void test_idle_owner_turn_is_not_instrumented_by_decision() {
  // Slice-4 device evidence: the turn ran ~6.2 million times per chain at ~6 us -- it is the poll, so its
  // scope was removed (design D11); the children it dispatches keep their own scopes.
  Track& t = playingTrackWithContent();
  t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_EQUAL_UINT32(0, calls(ProfileId::IdleOwner_unit));
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::counters().depth);
  TEST_ASSERT_FALSE(RuntimeProfiler::counters().poisoned);
}

void test_overdub_start_is_profiled_on_every_exit() {
  Track& t = playingTrackWithContent();
  t.startOverdubbing(10);
  TEST_ASSERT_EQUAL_UINT32(1, calls(ProfileId::OverdubStart_body));
  t.startOverdubbing(20);  // a repeated start is a (timed) refusal or no-op, never unmeasured
  TEST_ASSERT_EQUAL_UINT32(2, calls(ProfileId::OverdubStart_body));
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::counters().depth);
}

void test_save_work_item_step_is_profiled() {
  (void)StorageManagerInternal::stepPersistenceWorkItem(LOOPER_IDLE);
  TEST_ASSERT_EQUAL_UINT32(1, calls(ProfileId::SaveWorkItem_step));
}

void test_load_step_is_not_profiled_without_a_job() {
  // The no-job gate is the poll: the scope sits after it, so an idle call costs nothing.
  (void)StorageManagerInternal::stepLoadLoopJob(1000);
  TEST_ASSERT_EQUAL_UINT32(0, calls(ProfileId::LoadStep));
}


// ---- DEC-241: the internal-clock ISR tick is consume-only ----

/// Device trace 2026-11-01: IRQ_PIT -> updateAllTracks -> playCommittedLoopMidi -> buildMergedPlaybackFallbackCold ->
/// ensurePlaybackMergedMidiEventsBuilt ran real 5-18 ms builds inside the interrupt. A tick in interrupt context may
/// consume a current merged fallback but must never build one: it owes the build to the main-loop owner instead.
void test_isr_tick_never_builds_the_merged_fallback_and_owes_it() {
  Track& t = playingTrackWithContent();
  resetPlaybackMergedRebuildCountForTest();
  const uint32_t missesBefore = mergedFallbackTickMissesForTest();
  {
    PlaybackTickContext::InterruptScope isr;
    t.playMidiEvents(96, false);
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, playbackMergedRebuildCountForTest(),
                                 "an interrupt-context tick must not build the merged fallback");
  TEST_ASSERT_EQUAL_UINT32(0, RuntimeProfiler::stat(ProfileId::PlaybackWindow_build).calls);
  TEST_ASSERT_TRUE_MESSAGE(mergedFallbackOwedForTest(0, 0), "the missed source must be owed to the main loop");
  TEST_ASSERT_TRUE(mergedFallbackTickMissesForTest() > missesBefore);

  // The main-loop owner builds it off-clock; the next interrupt tick consumes it without building.
  trackManager.serviceOwedTickPlaybackFallbacks();
  TEST_ASSERT_EQUAL_UINT(1, playbackMergedRebuildCountForTest());
  TEST_ASSERT_FALSE(mergedFallbackOwedForTest(0, 0));
  {
    PlaybackTickContext::InterruptScope isr;
    t.playMidiEvents(120, false);
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, playbackMergedRebuildCountForTest(), "a current fallback is consumed, not rebuilt");
  TEST_ASSERT_FALSE(mergedFallbackOwedForTest(0, 0));
}

/// The main-loop tick (external clock) keeps today's behaviour in this slice: it still builds on demand.
void test_main_loop_tick_still_builds_the_merged_fallback() {
  Track& t = playingTrackWithContent();
  resetPlaybackMergedRebuildCountForTest();
  t.playMidiEvents(96, false);
  TEST_ASSERT_EQUAL_UINT(1, playbackMergedRebuildCountForTest());
  TEST_ASSERT_FALSE(mergedFallbackOwedForTest(0, 0));
}


/// DEC-241: an order-dirty fallback is not consumable in the interrupt either -- the tick would otherwise sort the
/// playback order inside the ISR. It is owed, and the owner rebuilds the order off-clock.
void test_isr_tick_never_rebuilds_the_playback_order() {
  Track& t = playingTrackWithContent();
  t.playMidiEvents(96, false);  // main-loop tick: builds the fallback and its order
  Loop& loop = t.getLoop(0);
  loop.playbackOrderDirty = true;
  {
    PlaybackTickContext::InterruptScope isr;
    t.playMidiEvents(120, false);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.playbackOrderDirty, "the interrupt tick must not rebuild the playback order");
  TEST_ASSERT_TRUE(mergedFallbackOwedForTest(0, 0));
  trackManager.serviceOwedTickPlaybackFallbacks();
  TEST_ASSERT_FALSE_MESSAGE(loop.playbackOrderDirty, "the main-loop owner rebuilds the order");
  TEST_ASSERT_FALSE(mergedFallbackOwedForTest(0, 0));
}


/// playback-tick-consume-only phase 1: every slot-tick that reaches route selection is counted by the route it consumes,
/// and an interrupt miss is counted per clock -- report-only, so the long-loop bypass can be measured before any repair.
void test_tick_route_and_miss_counters_follow_the_consumed_route() {
  using PlaybackTickContext::TickRoute;
  Track& t = playingTrackWithContent();
  const auto before = PlaybackTickContext::routeCountsForTest();
  t.playMidiEvents(96, false);  // main-loop tick: builds and consumes the merged fallback
  auto after = PlaybackTickContext::routeCountsForTest();
  TEST_ASSERT_EQUAL_UINT32(1, after.route[static_cast<int>(TickRoute::Merged)] -
                                  before.route[static_cast<int>(TickRoute::Merged)]);
  Loop& loop = t.getLoop(0);
  loop.playbackOrderDirty = true;  // make the next interrupt tick's source unconsumable
  {
    PlaybackTickContext::InterruptScope isr;
    t.playMidiEvents(120, false);
  }
  const auto missed = PlaybackTickContext::routeCountsForTest();
  TEST_ASSERT_EQUAL_UINT32(1, missed.route[static_cast<int>(TickRoute::None)] -
                                  after.route[static_cast<int>(TickRoute::None)]);
  TEST_ASSERT_EQUAL_UINT32(1, missed.missInterrupt - after.missInterrupt);
  TEST_ASSERT_EQUAL_UINT32(0, missed.missExternal - after.missExternal);
  trackManager.serviceOwedTickPlaybackFallbacks();
  {
    PlaybackTickContext::InterruptScope isr;
    t.playMidiEvents(144, false);
  }
  const auto served = PlaybackTickContext::routeCountsForTest();
  TEST_ASSERT_EQUAL_UINT32(1, served.route[static_cast<int>(TickRoute::Merged)] -
                                  missed.route[static_cast<int>(TickRoute::Merged)]);
  TEST_ASSERT_EQUAL_UINT32(0, served.missInterrupt - missed.missInterrupt);
}

/// playback-tick-consume-only phase 2: a merged-fallback tick through the real hot path files its reason under the slot
/// (this suite compiles the windowed lane out, so the reason is that one), on both clocks.
void test_merged_fallback_tick_files_its_route_reject_reason() {
  using PlaybackTickContext::RouteReject;
  PlaybackTickContext::initAtBoot();
  Track& t = playingTrackWithContent();
  t.playMidiEvents(96, false);
  auto filed = PlaybackTickContext::routeRejectForTest(0, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RouteReject::RollingDisabled), filed.reason);
  TEST_ASSERT_EQUAL_UINT32(1, filed.ticks);
  TEST_ASSERT_EQUAL_UINT32(1, filed.changes);
  {
    PlaybackTickContext::InterruptScope isr;
    t.playMidiEvents(120, false);
  }
  filed = PlaybackTickContext::routeRejectForTest(0, 0);
  TEST_ASSERT_EQUAL_UINT32(2, filed.ticks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, filed.changes, "an unchanged reason is not a change");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_prewarm_and_monolithic_views_are_profiled);
  RUN_TEST(test_prepared_record_edge_profiles_units_and_never_the_monolithic_views);
  RUN_TEST(test_cold_record_edge_is_a_timed_refusal);
  RUN_TEST(test_playback_window_build_is_profiled_but_not_its_reuse_poll);
  RUN_TEST(test_idle_owner_turn_is_not_instrumented_by_decision);
  RUN_TEST(test_overdub_start_is_profiled_on_every_exit);
  RUN_TEST(test_save_work_item_step_is_profiled);
  RUN_TEST(test_load_step_is_not_profiled_without_a_job);
  RUN_TEST(test_isr_tick_never_builds_the_merged_fallback_and_owes_it);
  RUN_TEST(test_main_loop_tick_still_builds_the_merged_fallback);
  RUN_TEST(test_isr_tick_never_rebuilds_the_playback_order);
  RUN_TEST(test_tick_route_and_miss_counters_follow_the_consumed_route);
  RUN_TEST(test_merged_fallback_tick_files_its_route_reject_reason);
  return UNITY_END();
}
