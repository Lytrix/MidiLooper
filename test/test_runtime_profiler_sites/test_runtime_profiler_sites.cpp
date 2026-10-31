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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_prewarm_and_monolithic_views_are_profiled);
  RUN_TEST(test_prepared_record_edge_profiles_units_and_never_the_monolithic_views);
  RUN_TEST(test_cold_record_edge_is_a_timed_refusal);
  return UNITY_END();
}
