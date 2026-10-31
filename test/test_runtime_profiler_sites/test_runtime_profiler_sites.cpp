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
  return UNITY_END();
}
