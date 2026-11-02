// playback-tick-consume-only 5.1, pulled forward narrowly (DEC-246 amendment 27): a rolling frame the resolver blocked
// because its **served view's source went stale** recovers while the transport runs.
//
// The device chain (captures/c5-3b-sweep-smoke-03): a fresh dense take is served by a whole-loop (deadline-free) frame,
// a newer committed revision makes that served view stale, the resolver blocks the frame (`ServingViewSourceStale`) and
// the producer then answers `win_blocked` for the rest of the transport -- the merged fallback plays everything. Here the
// same resolver branch is reached with a one-bar loop's cold whole-loop window and a production Delete edit.
#include "../test_revision_manifest_lane/manifest_lane_harness.h"

namespace {

struct LaneRun {
  uint32_t servedTicks = 0;
  uint32_t servedCurrentTicks = 0;  // served by a window of the loop's current revision
  uint32_t blockedTicks = 0;
};

/// Play `[from, to)` on the production owner shape and count how the committed lane serves.
LaneRun playAndCount(Track& t, uint32_t from, uint32_t to) {
  LaneRun r;
  RollingOwnerLane lane;
  const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  for (uint32_t tick = from; tick < to; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) ++r.blockedTicks;
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval coverage{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, served, coverage) && served.origin == PlaybackWindowOrigin::Committed) {
      ++r.servedTicks;
      if (served.playbackRevision == t.getLoop(0).playbackRevision) ++r.servedCurrentTicks;
    }
  }
  return r;
}

}  // namespace

extern "C" void setUp() {
  sharedCaptureFinalizationSuiteSetUp();
  uninstallReader();
}
extern "C" void tearDown() {
  uninstallReader();
  sharedCaptureFinalizationSuiteTearDown();
}

// The RED. Serving, then a newer committed revision: the stale frame is blocked by the resolver, and the lane must
// serve the current revision again within a bounded number of traversals instead of staying blocked until the stop.
void test_5_1_a_source_stale_blocked_frame_recovers_while_playing() {
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 1, 4);
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  MidiSendCapture::reset();
  const uint32_t len = loop.loopLengthTicks;
  const LaneRun before = playAndCount(t, 0, 24 * len);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, before.servedTicks, "precondition: the lane serves the take");

  const uint32_t revisionBefore = loop.playbackRevision;
  hideFirstNoteOfPitch(loop, 62);  // a newer committed revision while the frame serves
  TEST_ASSERT_NOT_EQUAL(revisionBefore, loop.playbackRevision);

  // Bound: the lane's own re-acquisition (the priced lookahead of a fresh window) plus a few traversals.
  const LaneRun after = playAndCount(t, 24 * len, 96 * len);
  const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  char detail[160];
  snprintf(detail, sizeof(detail), "after the edit: served=%u current=%u blocked=%u final phase=%u blockedBy=%u",
           after.servedTicks, after.servedCurrentTicks, after.blockedTicks, unsigned(runtime.rolling.phase),
           unsigned(runtime.rolling.blockedBy));
  printf("STALE before served=%u blocked=%u | %s\n", before.servedTicks, before.blockedTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, after.servedCurrentTicks, detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Blocked),
                                static_cast<int>(runtime.rolling.phase), detail);
}

// A dense take stopped **into play**. The record-stop commit asks for the committed window early (its "1.4 lead"
// request) but never positions it: no clock stamp, no deadline. It could never qualify (`gate_unqualified`) and held
// the active slot's rolling requester at `win_busy` for the rest of the transport -- 64 traversals, no committed window
// (the RED). The requester now replaces a demand left unqualified for more than a traversal with a positioned one.
void test_5_1_a_dense_take_stopped_into_play_reaches_committed_serving() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t bars = 4;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  uint32_t tick = 0;
  for (; tick < bars * bar - 32; tick += 16) {
    if (tick >= 96 && tick % 48 == 0) {  // dense: a 16th-note stream after the recorder's own first note
      const uint8_t pitch = static_cast<uint8_t>(61 + (tick / 48) % 24);
      t.noteOn(1, pitch, 100, tick + 3);
      t.noteOff(1, pitch, 0, tick + 27);
    }
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    for (unsigned turn = 0; turn < 4; ++turn)
      t.processDeferredIdleMaintenance(tick, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  clockManager.assignCurrentTickSilently(tick);
  t.stopRecording(tick);
  TEST_ASSERT_TRUE_MESSAGE(t.isPlaying(), "the take stops into play");

  // main.cpp's shape with the transport running: the window lane, the clock-edge reader and the deferred idle owners.
  RollingOwnerLane lane;
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  uint32_t rootCurrentAt = 0, blockedTicks = 0, servedCurrent = 0, servedCurrentTail = 0;
  const uint32_t end = tick + 64 * loop.loopLengthTicks;
  for (; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    if (tick % 4 == 0) {
      lane.serviceTracks(tick);
      const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false), 1);
    }
    t.playMidiEvents(tick, true);
    if (rootCurrentAt == 0 && loop.publishedSnapshotIsCurrent()) rootCurrentAt = tick;
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) ++blockedTicks;
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval coverage{};
    const bool current = runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, served, coverage) && served.origin == PlaybackWindowOrigin::Committed &&
        served.playbackRevision == loop.playbackRevision;
    servedCurrent += current;
    if (tick + 8 * loop.loopLengthTicks >= end) servedCurrentTail += current;
  }
  char detail[420];
  snprintf(detail, sizeof(detail), "len=%u rootCurrentAt=%u blockedTicks=%u servedCurrent=%u tail=%u phase=%u "
           "blockedBy=%u pending=%u exit=%s playing=%u | pend=%d/%u/%u froz=%d/%u role=%u grains=%u own=%d bld=%d",
           loop.loopLengthTicks, rootCurrentAt, blockedTicks, servedCurrent,
           servedCurrentTail, unsigned(runtime.rolling.phase), unsigned(runtime.rolling.blockedBy),
           t.hasPendingDeferredOverdubStop() ? 1u : 0u,
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-",
           t.isPlaying() ? 1u : 0u, runtime.pendingViewRequest_.active, unsigned(runtime.pendingViewRequest_.reason),
           unsigned(runtime.pendingViewRequest_.sourceKind), runtime.frozenViewRequest.active,
           unsigned(runtime.frozenViewRequest.sourceKind), unsigned(runtime.coverageRoleRefusal(false)),
           unsigned(t.playbackViewOutstandingGrains(0)), runtime.ownsCoverageSource(), runtime.viewBuild.active());
  printf("STALE2 %s\n", detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(0, rootCurrentAt, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, servedCurrentTail, detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Blocked),
                                static_cast<int>(runtime.rolling.phase), detail);
}

// Defect B (DEC-246 amendment 27): a deadline-free whole-loop frame -- the shape a Record-return or launch-bound frame
// serves with (`deadlineTick == 0`, the resolver's own currency check) -- whose served source becomes obsolete while the
// transport runs. The resolver blocks it once (`ServingViewSourceStale`); the producer must retire that frame once and
// serve the current source again, not leave it `Blocked` for the rest of the transport.
void test_5_1_a_stale_serving_frame_is_retired_once_and_the_current_source_serves() {
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 2, 4);  // two bars: a two-bar window is the whole loop
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const uint32_t len = loop.loopLengthTicks;
  RollingOwnerLane lane;
  uint32_t tick = 0;
  // Play until a whole-loop window serves.
  for (; tick < 64 * len; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving && runtime.rolling.startPhase == 0 &&
        tick % len == len / 2)
      break;
  }
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
                            static_cast<int>(runtime.rolling.phase), "precondition: a whole-loop window serves");
  // The deadline-free shape of a bound Record-return / launch frame: its currency is judged on every serving tick.
  runtime.rolling.deadlineTick = 0;
  PlaybackViewSourceIdentity before{};
  PlaybackCoverageInterval coverage{};
  TEST_ASSERT_TRUE(t.publishedPlaybackWindowForSlot(0, before, coverage));
  const uint32_t revisionBefore = before.playbackRevision;

  hideFirstNoteOfPitch(loop, 62);  // the committed source moves: R -> C
  TEST_ASSERT_NOT_EQUAL(revisionBefore, loop.playbackRevision);

  uint32_t staleBlocks = 0, blockedTicks = 0, servedFromC = 0;
  bool wasStaleBlocked = false;
  const uint32_t end = tick + 48 * len;
  for (++tick; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    const bool staleBlocked = runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked &&
        runtime.rolling.blockedBy == static_cast<uint8_t>(PlaybackTickContext::RouteReject::ServingViewSourceStale);
    if (staleBlocked && !wasStaleBlocked) ++staleBlocks;
    wasStaleBlocked = staleBlocked;
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) ++blockedTicks;
    PlaybackViewSourceIdentity served{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, served, coverage) && served.origin == PlaybackWindowOrigin::Committed &&
        served.playbackRevision == loop.playbackRevision && served.snapshotIdentity == loop.publishedSnapshotIdentity())
      ++servedFromC;
  }
  char detail[200];
  snprintf(detail, sizeof(detail), "staleBlocks=%u blockedTicks=%u servedFromC=%u phase=%u blockedBy=%u rev %u->%u",
           staleBlocks, blockedTicks, servedFromC, unsigned(runtime.rolling.phase), unsigned(runtime.rolling.blockedBy),
           revisionBefore, loop.playbackRevision);
  printf("STALE3 %s\n", detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, staleBlocks, detail);       // the edge, once
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(4, blockedTicks, detail);  // retired on the producer's next turn
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, servedFromC, detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Blocked),
                                static_cast<int>(runtime.rolling.phase), detail);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_5_1_a_source_stale_blocked_frame_recovers_while_playing);
  RUN_TEST(test_5_1_a_dense_take_stopped_into_play_reaches_committed_serving);
  RUN_TEST(test_5_1_a_stale_serving_frame_is_retired_once_and_the_current_source_serves);
  return UNITY_END();
}
