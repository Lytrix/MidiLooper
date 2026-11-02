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
void denseTakeStoppedIntoPlay(uint32_t bars, bool routed = false) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  if (routed) installReader();
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
  const uint32_t end = tick + (bars >= 16 ? 12 : 64) * loop.loopLengthTicks;
  for (; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    if (tick % 4 == 0) {
      if (routed && CommittedManifestLane::gReader == &reader()) reader().serviceOwner(loop);
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
        served.playbackRevision == loop.playbackRevision && (!routed || served.manifestIdentity != 0);
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
  if (routed)  // the Record presentation must not keep the routed slot once the manifest serves it
    TEST_ASSERT_FALSE_MESSAGE(runtime.hasBoundRecordReturnWindowForPendingJob(), detail);
}

// The device deadlock (DEC-246 amendment 27): on the manifest-routed slot the Record presentation retired only once a
// committed window already served, while its own hold kept the committed lane from building one.
void test_5_1_routed_dense_take_presentation_yields_to_the_manifest_lane() { denseTakeStoppedIntoPlay(4, true); }

void test_5_1_a_dense_take_stopped_into_play_reaches_committed_serving() { denseTakeStoppedIntoPlay(4); }

// The device's own shape: a 32-bar dense take (the device's 33-bar HITL take, 16ths) stopped into play.
void test_5_1_a_long_dense_take_stopped_into_play_reaches_committed_serving() {
  denseTakeStoppedIntoPlay(32);
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

  // The device also held the record stop's lead request: an unpositioned committed demand (no clock stamp, no
  // deadline), here retained the way `requestCommittedPlaybackWindow` retains it outside the rolling requester.
  TEST_ASSERT_EQUAL(Track::PlaybackViewRequestResult::DeferredSourceQualification,
                    t.requestCommittedPlaybackWindow(0, PlaybackCoverageInterval{0, len, len}, 0,
                                                     PlaybackDeadlineClock{tick, 0}));  // fresh, unstamped
  TEST_ASSERT_TRUE(runtime.pendingViewRequest_.active);
  TEST_ASSERT_EQUAL_UINT32(0, runtime.pendingViewRequest_.clockEpoch);

  hideFirstNoteOfPitch(loop, 62);  // the committed source moves: R -> C
  TEST_ASSERT_NOT_EQUAL(revisionBefore, loop.playbackRevision);

  uint32_t staleBlocks = 0, blockedTicks = 0, servedFromC = 0, staleAt = 0, positionedAt = 0;
  bool wasStaleBlocked = false;
  const uint32_t end = tick + 48 * len;
  for (++tick; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    const bool staleBlocked = runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked &&
        runtime.rolling.blockedBy == static_cast<uint8_t>(PlaybackTickContext::RouteReject::ServingViewSourceStale);
    if (staleBlocked && !wasStaleBlocked) {
      ++staleBlocks;
      staleAt = tick;
    }
    if (staleAt && !positionedAt && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Requested)
      positionedAt = tick;  // a positioned demand of the current source replaced the frame
    wasStaleBlocked = staleBlocked;
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) ++blockedTicks;
    PlaybackViewSourceIdentity served{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, served, coverage) && served.origin == PlaybackWindowOrigin::Committed &&
        served.playbackRevision == loop.playbackRevision && served.snapshotIdentity == loop.publishedSnapshotIdentity())
      ++servedFromC;
  }
  char detail[200];
  snprintf(detail, sizeof(detail),
           "staleBlocks=%u blockedTicks=%u servedFromC=%u phase=%u blockedBy=%u rev %u->%u requested %u ticks after",
           staleBlocks, blockedTicks, servedFromC, unsigned(runtime.rolling.phase), unsigned(runtime.rolling.blockedBy),
           revisionBefore, loop.playbackRevision, positionedAt ? positionedAt - staleAt : 0);
  printf("STALE3 %s\n", detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, staleBlocks, detail);       // the edge, once
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(4, blockedTicks, detail);  // retired on the producer's next turn
  // ... and the obsolete unpositioned demand does not hold the requester for a traversal: the next window is
  // requested right away (well inside one loop), not after the one-traversal timeout.
  TEST_ASSERT_NOT_EQUAL_MESSAGE(0, positionedAt, detail);
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(len / 2, positionedAt - staleAt, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, servedFromC, detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Blocked),
                                static_cast<int>(runtime.rolling.phase), detail);
}

// The post-restart handoff (DEC-246 amendment 27; device captures/c5-3b-sweep-01): a committed manifest slot, Ready
// before the restart, is stopped and started again (the device's MIDI Start: tick 0, a new clock epoch). Its first
// window after the restart is prepared and Ready; at its handoff the resolver refused it (`CandidateRejected`) and the
// frame stayed blocked. The first assertion names the refusing term; the slot must then serve.
void test_5_1_the_first_window_after_a_restart_serves_on_the_manifest_route() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const auto drive = [&](uint32_t from, uint32_t to, uint32_t epoch, uint32_t& served, uint8_t& term, uint32_t& blocked) {
    for (uint32_t tick = from; tick < to; ++tick) {
      nativePlaybackDeadlineClock = {tick, epoch};
      clockManager.assignCurrentTickSilently(tick);
      reader().serviceOwner(loop);
      lane.serviceTracks(tick);
      t.playMidiEvents(tick, true);
      if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
        ++blocked;
        if (runtime.rolling.rejectTerm) term = runtime.rolling.rejectTerm;
      }
      PlaybackViewSourceIdentity src{};
      PlaybackCoverageInterval cov{};
      if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
          t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
        ++served;
    }
  };
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint32_t served1 = 0, blocked1 = 0;
  uint8_t term1 = 0;
  drive(0, 20 * loop.loopLengthTicks, 1, served1, term1, blocked1);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served1, "precondition: the manifest lane serves before the restart");

  // Stop, then Start: tick 0 and a new clock epoch, as the device's MIDI Start.
  t.stopPlaying();
  nativePlaybackDeadlineClock = {0, 2};
  clockManager.assignCurrentTickSilently(0);
  lane.serviceTracks(0);
  t.startPlaying(0);
  uint32_t served2 = 0, blocked2 = 0;
  uint8_t term2 = 0;
  drive(0, 20 * loop.loopLengthTicks, 2, served2, term2, blocked2);
  char detail[260];
  snprintf(detail, sizeof(detail),
           "after restart: served=%u blocked=%u rejectTerm=%u blockedBy=%u phase=%u exit=%s playing=%u lane=%u",
           served2, blocked2, unsigned(term2), unsigned(runtime.rolling.blockedBy), unsigned(runtime.rolling.phase),
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-",
           t.isPlaying() ? 1u : 0u, unsigned(CommittedManifestLane::gServable.state));
  printf("RESTART %s\n", detail);
  {
    const auto* ls = runtime.viewSetIfPrewarmed();
    printf("RESTART roles: c=%u r=%u b=%u i=%u ret=%u v0=%d%d v1=%d%d v2=%d%d\n", ls->lifecycle.current,
           ls->lifecycle.ready, ls->lifecycle.build, ls->lifecycle.idle, ls->lifecycle.retained, ls->views[0].complete,
           ls->views[0].published, ls->views[1].complete, ls->views[1].published, ls->views[2].complete,
           ls->views[2].published);
  }
  printf("RESTART busy: pend=%d reason=%u kind=%u epoch=%u deadline=%llu admitted=%llu froz=%d role=%u grains=%u own=%d "
         "bld=%d gate=%s\n", runtime.pendingViewRequest_.active, unsigned(runtime.pendingViewRequest_.reason),
         unsigned(runtime.pendingViewRequest_.sourceKind), runtime.pendingViewRequest_.clockEpoch,
         (unsigned long long)runtime.pendingViewRequest_.deadlineTick,
         (unsigned long long)runtime.pendingViewRequest_.work.admittedTick, runtime.frozenViewRequest.active,
         unsigned(runtime.coverageRoleRefusal(false)), unsigned(t.playbackViewOutstandingGrains(0)),
         runtime.ownsCoverageSource(), runtime.viewBuild.active(),
         playbackBuildRefusalName(t.playbackBuildGateRefusal(0, clockManager.getPlaybackDeadlineClock())));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, term2, detail);  // no candidate term refused the first window
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served2, detail);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_5_1_a_source_stale_blocked_frame_recovers_while_playing);
  RUN_TEST(test_5_1_a_dense_take_stopped_into_play_reaches_committed_serving);
  RUN_TEST(test_5_1_a_long_dense_take_stopped_into_play_reaches_committed_serving);
  RUN_TEST(test_5_1_a_stale_serving_frame_is_retired_once_and_the_current_source_serves);
  RUN_TEST(test_5_1_the_first_window_after_a_restart_serves_on_the_manifest_route);
  RUN_TEST(test_5_1_routed_dense_take_presentation_yields_to_the_manifest_lane);
  return UNITY_END();
}
