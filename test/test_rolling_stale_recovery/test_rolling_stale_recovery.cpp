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

// An orphaned successor (device captures/session_20261102_155425): the Record presentation retired while its owed
// successor was still being built; the build was published as the `ready` successor at 88.0 s, but its adopter (the
// Record-return reader's boundary) was gone, so no frame owned it. With the retained view it held the last free role:
// `win_busy` with `dm=0,bld=0,ow=0,rl=0` from then on, across the stop and the restart. The fixture injects that state
// (an Idle frame, a complete unpublished `ready` that nothing owns); the requester must reclaim the role and re-request.
void test_5_1_an_orphaned_ready_successor_returns_its_build_role() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  const auto readyWaits = [&] {
    const auto* vs = runtime.viewSetIfPrewarmed();
    return vs && vs->lifecycle.ready < vs->views.size() && vs->views[vs->lifecycle.ready].complete &&
           !vs->views[vs->lifecycle.ready].published;
  };
  uint32_t tick = 0;
  bool orphanable = false;
  for (; tick < 40 * loop.loopLengthTicks && !orphanable; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    orphanable = runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Ready && readyWaits() &&
                 !runtime.pendingViewRequest_.active && !runtime.viewBuild.active();
  }
  TEST_ASSERT_TRUE_MESSAGE(orphanable, "precondition: a complete successor waits in the ready role");
  runtime.rolling = {};  // its owner is gone: the device's Idle frame beside an unadopted ready
  TEST_ASSERT_TRUE_MESSAGE(readyWaits(), "precondition: the orphan holds the ready role");
  TEST_ASSERT_NOT_EQUAL_UINT8_MESSAGE(0, runtime.coverageRoleRefusal(false), "precondition: no build role is free");

  uint32_t requested = 0;
  const uint32_t end = tick + 2 * loop.loopLengthTicks;
  for (; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    requested += runtime.rolling.phase != LoopPlaybackRuntime::RollingPhase::Idle;
  }
  char detail[200];
  snprintf(detail, sizeof(detail), "requested=%u role=%u exit=%s phase=%u", requested,
           unsigned(runtime.coverageRoleRefusal(false)),
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-",
           unsigned(runtime.rolling.phase));
  printf("ORPHAN %s\n", detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, requested, detail);
}

// A price-refused demand must not stay an owner (DEC-246 amendment 27, owner ruling): main.cpp's per-turn committed
// demand asks for the playhead's own window, so its lead is about zero and DEC-147 refuses it (`handoff_unreachable`,
// device captures/session_20261102_161218 at 88.07 s). The refusal left it pending (`CannotCompleteBeforeHandoff`), so
// the rolling requester answered `win_busy` for the rest of the transport and the routed slot never served. The lane
// here makes main's per-turn demand on every turn, as the device does.
void test_5_1_a_price_refused_demand_leaves_no_pending_owner() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint32_t served = 0, refusedPendingTicks = 0, refusals = 0;
  bool wasRefused = false;
  const uint32_t end = 40 * loop.loopLengthTicks;
  for (uint32_t tick = 0; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    if (tick % 4 == 0) {
      const auto clock = clockManager.getPlaybackDeadlineClock();
      if (t.isPlaying()) (void)t.requestActiveCommittedPlaybackWindow(static_cast<uint32_t>(clock.tick), clock);
      lane.serviceTracks(tick);
    }
    t.playMidiEvents(tick, true);
    const bool refused = runtime.pendingViewRequest_.active &&
        runtime.pendingViewRequest_.reason == PlaybackViewPendingReason::CannotCompleteBeforeHandoff;
    refusedPendingTicks += refused;
    refusals += refused && !wasRefused;
    wasRefused = refused;
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (tick >= end / 2 && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++served;
  }
  char detail[240];
  snprintf(detail, sizeof(detail), "servedTail=%u refusedPendingTicks=%u refusals=%u exit=%s phase=%u pend=%d/%u",
           served, refusedPendingTicks, refusals,
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-",
           unsigned(runtime.rolling.phase), runtime.pendingViewRequest_.active,
           unsigned(runtime.pendingViewRequest_.reason));
  printf("PRICEREF %s\n", detail);
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(loop.loopLengthTicks, refusedPendingTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served, detail);
  // The refusal is named, and what it priced is the requested rolling interval's work -- never the loop's extent.
  TEST_ASSERT_TRUE_MESSAGE(runtime.priceRefusal.active, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * Config::TICKS_PER_BAR, runtime.priceRefusal.coverage.lengthTicks, detail);
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(loop.loopLengthTicks, runtime.priceRefusal.coverage.lengthTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, runtime.priceRefusal.price, detail);
}

// A continuity miss at the handoff (DEC-246 amendment 27; device captures/session_20261102_172043 at 140.6 s): the
// first manifest window after a restart was Ready, and at its boundary the playhead proof failed
// (`CandidateRejected`, term 6: `lastTickInLoop != start - 1`), leaving the frame Blocked for the rest of the
// transport. The fixture skips the playback turn on the tick before the handoff, as one scheduler turn spanning two
// ticks. The candidate must not be served, the frame must not stay Blocked, and a later window must serve.
void test_5_1_a_continuity_miss_at_the_handoff_recovers() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint64_t skipAt = 0;
  uint8_t sawTerm = 0;
  uint32_t blockedTicks = 0, servedAfter = 0;
  const uint32_t end = 40 * loop.loopLengthTicks;
  for (uint32_t tick = 0; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    if (tick % 4 == 0) lane.serviceTracks(tick);
    if (skipAt == 0 && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Ready &&
        runtime.rolling.deadlineTick > tick + 1)
      skipAt = runtime.rolling.deadlineTick - 1;
    if (tick != skipAt) t.playMidiEvents(tick, true);  // the skipped turn: the playhead misses start - 1
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
      ++blockedTicks;
      if (runtime.rolling.rejectTerm) sawTerm = runtime.rolling.rejectTerm;
    }
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (skipAt != 0 && tick > skipAt + 1 && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++servedAfter;
  }
  char detail[200];
  snprintf(detail, sizeof(detail), "skipAt=%llu term=%u blockedTicks=%u servedAfter=%u phase=%u exit=%s",
           (unsigned long long)skipAt, unsigned(sawTerm), blockedTicks, servedAfter, unsigned(runtime.rolling.phase),
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-");
  printf("CONTINUITY %s\n", detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(0, skipAt, "precondition: a Ready frame with a future handoff");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, sawTerm, detail);  // the named continuity term, not another rejection
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(loop.loopLengthTicks, blockedTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, servedAfter, detail);
}

// The handoff frontier (DEC-246 amendment 27, owner ruling): continuity means the candidate starts at the next position
// the clock can reach with nothing owed in between, not that the previous internal tick was `S - 1`. On external MIDI
// clock (24 PPQN against 192 internal) one pulse advances 8 ticks: device captures/session_20261102_173023 measured
// `lastTickInLoop = 19960` at `S = 19968` (7 behind `S - 1`), and the exact rule refused every such handoff.
void test_5_1_the_handoff_frontier_accepts_the_next_clock_reachable_start_only() {
  Loop& loop = trackManager.getTrack(1).getLoop(1);
  const uint32_t savedLength = loop.loopLengthTicks, savedLast = loop.lastTickInLoop;
  constexpr uint32_t S = 19968, step = Config::TICKS_PER_CLOCK;
  loop.loopLengthTicks = 25344;
  const auto holds = [&](uint32_t last, uint32_t phase, uint32_t clockStep, bool silent) {
    loop.lastTickInLoop = last;
    return rollingHandoffFrontierHolds(loop, phase, clockStep, silent);
  };
  TEST_ASSERT_TRUE_MESSAGE(holds(S - step, S, step, true), "the measured case: one pulse behind, a silent gap");
  TEST_ASSERT_TRUE_MESSAGE(holds(25344 - step, 0, step, true), "across the wrap: the last pulse of the traversal");
  TEST_ASSERT_TRUE_MESSAGE(holds(S - 1, S, 1, false), "internal clock: the exact predecessor, as before");
  TEST_ASSERT_FALSE_MESSAGE(holds(S - 2 * step, S, step, true), "two pulses behind: a pulse was missed");
  TEST_ASSERT_FALSE_MESSAGE(holds(S, S, step, true), "S already consumed");
  TEST_ASSERT_FALSE_MESSAGE(holds(S + step, S, step, true), "past S");
  TEST_ASSERT_FALSE_MESSAGE(holds(S - step, S, step, false), "the gap was served by another route: content is owed there");
  TEST_ASSERT_FALSE_MESSAGE(holds(UINT32_MAX, S, step, true), "no accounted phase (reset, re-anchored projection)");
  TEST_ASSERT_FALSE_MESSAGE(holds(S - 2, S, 1, true), "internal clock: a tick was missed");
  loop.loopLengthTicks = savedLength;
  loop.lastTickInLoop = savedLast;
}

// End to end on external clock: the routed lane advanced in 8-tick pulses must adopt its manifest windows (no term 6).
void test_5_1_the_routed_lane_serves_on_external_clock_pulses() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  gNativeFrontierStepForTest = Config::TICKS_PER_CLOCK;  // external MIDI clock: one pulse is 8 internal ticks
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint32_t served = 0, term6 = 0;
  const uint32_t end = 40 * loop.loopLengthTicks;
  for (uint32_t tick = 0; tick < end; tick += Config::TICKS_PER_CLOCK) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    term6 += runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked && runtime.rolling.rejectTerm == 6;
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++served;
  }
  gNativeFrontierStepForTest = 0;
  char detail[160];
  snprintf(detail, sizeof(detail), "served=%u term6=%u phase=%u exit=%s", served, term6,
           unsigned(runtime.rolling.phase),
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-");
  printf("EXTCLOCK %s\n", detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, term6, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served, detail);
}

// An expired demand must not own the lane (DEC-246 amendment 27, owner invariant B; device
// captures/session_20261102_182701): a committed demand's deadline expired while its window was qualifying
// (`demand_invalidated` at 85.2 s). The build was dropped but the demand stayed pending (`DeadlineExpired`), through the
// stop and the restart, so the requester answered `win_busy` for the whole 90 s and the routed slot never served. The
// fixture injects that state (an Idle frame beside a pending demand whose deadline passed, no build), then restarts.
void test_5_1_an_expired_routed_demand_is_discharged_after_a_restart() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const uint32_t len = loop.loopLengthTicks;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  const uint32_t at = 2 * len + 100;
  nativePlaybackDeadlineClock = {at, 1};
  clockManager.assignCurrentTickSilently(at);
  t.cancelRollingPlaybackWindow(0);
  runtime.clearViewPendingRequest();
  TEST_ASSERT_EQUAL(Track::PlaybackViewRequestResult::DeferredSourceQualification,
                    t.requestCommittedPlaybackWindow(0, PlaybackCoverageInterval{1536, 1536, len}, 100,
                                                     nativePlaybackDeadlineClock));
  auto& demand = runtime.pendingViewRequest_;
  TEST_ASSERT_TRUE_MESSAGE(demand.active && demand.clockEpoch == 1 && demand.deadlineTick != 0,
                           "precondition: a positioned committed demand");
  demand.deadlineTick = at - 1;  // the device's shape: the deadline passed while it qualified
  demand.work.deadlineTick = at - 1;
  demand.reason = PlaybackViewPendingReason::DeadlineExpired;
  demand.positionInvalidated = true;

  t.stopPlaying();
  nativePlaybackDeadlineClock = {0, 2};
  clockManager.assignCurrentTickSilently(0);
  lane.serviceTracks(0);
  t.startPlaying(0);
  uint32_t served = 0, busyTicks = 0;
  for (uint32_t tick = 0; tick < 20 * len; ++tick) {
    nativePlaybackDeadlineClock = {tick, 2};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    // The rolling requester alone: on the device nothing else replaced the demand (no qualification began after the
    // restart), so the slot's own requester is the owner that must discharge it.
    if (tick % 4 == 0) lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    const char* ex = PlaybackTickContext::gState.producerExit[0];
    busyTicks += ex != nullptr && strcmp(ex, "win_busy") == 0;
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++served;
  }
  char detail[200];
  snprintf(detail, sizeof(detail), "served=%u busyTicks=%u pend=%d/%u epoch=%u exit=%s", served, busyTicks,
           demand.active, unsigned(demand.reason), demand.clockEpoch,
           PlaybackTickContext::gState.producerExit[0] ? PlaybackTickContext::gState.producerExit[0] : "-");
  printf("EXPIRED %s\n", detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served, detail);
}

// A superseded request cannot kill a newer Ready frame (owner ruling 2026-11-03; device
// captures/session_20261103_084810). On a two-pass take the first window was built and Ready, positioned two
// traversals out, while an older committed demand stayed pending (`dm=1`, owners=4). When that older demand's deadline
// passed it was judged `DeadlineExpired`, and the producer blocked the frame terminally (`win_blocked_by_request`
// rq=10): the routed slot never served. The fixture reaches a real Ready frame, reinstates an older positioned demand
// (captured from this frame's own Requested phase) whose deadline lies before the frame's, and drives past it.
void expiredSupersededRequestCase(bool sameDeadlineOtherSource) {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const uint32_t len = loop.loopLengthTicks;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  const auto step = [&](uint32_t tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
  };
  // 1. Drive until the frame is Ready (built, not yet due), keeping a copy of its demand from the Requested phase.
  PlaybackViewPendingRequest older{};
  uint32_t tick = 0;
  for (; tick < 40 * len; ++tick) {
    step(tick);
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Requested) {
      if (runtime.pendingViewRequest_.active) older = runtime.pendingViewRequest_;
      else if (runtime.frozenViewRequest.active) older = runtime.frozenViewRequest;
    }
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Ready && older.active &&
        runtime.rolling.deadlineTick > uint64_t(tick) + 64 && !runtime.pendingViewRequest_.active)
      break;
  }
  TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::RollingPhase::Ready, runtime.rolling.phase,
                            "precondition: a Ready frame, its handoff still ahead");
  TEST_ASSERT_TRUE_MESSAGE(older.active && older.clockEpoch == 1, "precondition: a positioned committed demand copy");
  // 2. The device's state: an older demand still pending beside the Ready frame, due before the frame's handoff.
  const uint64_t frameDeadline = runtime.rolling.deadlineTick;
  // Negative control: the stale demand shares the frame's deadline but names another source revision, so the
  // deadline alone cannot tell it from the frame's own demand.
  older.deadlineTick = sameDeadlineOtherSource ? frameDeadline : tick + 16;
  if (sameDeadlineOtherSource) older.source.playbackRevision += 1;
  older.work.deadlineTick = older.deadlineTick;
  older.work.admittedTick = tick;
  older.positionInvalidated = false;
  older.reason = PlaybackViewPendingReason::None;
  runtime.pendingViewRequest_ = older;
  // 3. Past the older deadline and on through the frame's handoff and beyond.
  uint32_t blocked = 0, served = 0;
  uint8_t blockedBy = 0;
  const char* firstBlockExit = nullptr;
  for (++tick; tick < frameDeadline + 4 * len; ++tick) {
    step(tick);
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
      if (!blocked) {
        blockedBy = runtime.rolling.blockedBy;
        firstBlockExit = PlaybackTickContext::gState.producerExit[0];
      }
      ++blocked;
    }
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++served;
  }
  char detail[220];
  snprintf(detail, sizeof(detail), "blocked=%u blockedBy=%u firstExit=%s served=%u phase=%u pend=%d/%u", blocked,
           unsigned(blockedBy), firstBlockExit ? firstBlockExit : "-", served, unsigned(runtime.rolling.phase),
           runtime.pendingViewRequest_.active, unsigned(runtime.pendingViewRequest_.reason));
  printf("SUPERSEDED[%s] %s\n", sameDeadlineOtherSource ? "same-deadline" : "earlier", detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, blocked, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served, detail);
}

void test_5_1_an_expired_superseded_request_cannot_block_a_ready_frame() { expiredSupersededRequestCase(false); }

// The same, with the stale demand at the frame's own deadline but another source identity (owner: a deadline is a
// scheduling property, not demand provenance).
void test_5_1_a_stale_request_at_the_frame_s_deadline_is_still_superseded() { expiredSupersededRequestCase(true); }

// The per-turn playhead demand defers to a positioned rolling owner (owner ruling 2026-11-03; device
// captures/session_20261103_092547, `pending_armed` writer 2 via `requestActiveCommittedPlaybackWindow`). Main's
// per-turn demand stepped aside only while the routed frame was Requested; with the frame Ready for a window still
// ahead it armed a playhead-centred demand nothing owned, which expired a traversal later. Contract: once the rolling
// owner holds a positioned obligation for the routed slot, the per-turn requester creates no competing obligation until
// that ownership is released.
void test_5_1_the_per_turn_demand_defers_to_a_ready_or_serving_routed_frame() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const uint32_t len = loop.loopLengthTicks;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  // `main.cpp`'s window lane: the per-turn demand first, then the owner lane.
  uint32_t armedBesideOwner = 0, served = 0, blocked = 0;
  uint32_t lastPhase = 0, lastStart = 0;
  for (uint32_t tick = 0; tick < 20 * len; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    if (tick % 4 == 0) {
      const auto phase = runtime.rolling.phase;
      const bool owned = phase == LoopPlaybackRuntime::RollingPhase::Ready ||
                         phase == LoopPlaybackRuntime::RollingPhase::Serving;
      const bool wasPending = runtime.pendingViewRequest_.active;
      (void)t.requestActiveCommittedPlaybackWindow(tick, nativePlaybackDeadlineClock);
      if (owned && !wasPending && runtime.pendingViewRequest_.active) {
        ++armedBesideOwner;
        lastPhase = static_cast<uint32_t>(phase);
        lastStart = runtime.pendingViewRequest_.coverage.startPhase;
      }
      lane.serviceTracks(tick);
    }
    t.playMidiEvents(tick, true);
    blocked += runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked;
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++served;
  }
  char detail[200];
  snprintf(detail, sizeof(detail), "armedBesideOwner=%u (last phase=%u start=%u) served=%u blocked=%u", armedBesideOwner,
           lastPhase, lastStart, served, blocked);
  printf("DEFER %s\n", detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, armedBesideOwner, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, blocked, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served, detail);
}

/// The routed slot's playback-view lifecycle as one comparable stamp: the four roles and each view's storage generation.
struct ViewLifecycleStamp {
  uint8_t current = 255, ready = 255, build = 255, idle = 255;
  uint64_t generation[RuntimePlaybackViewConfig::kViewCount] = {};
  bool operator==(const ViewLifecycleStamp& o) const {
    if (current != o.current || ready != o.ready || build != o.build || idle != o.idle) return false;
    for (size_t i = 0; i < RuntimePlaybackViewConfig::kViewCount; ++i)
      if (generation[i] != o.generation[i]) return false;
    return true;
  }
};
ViewLifecycleStamp stampViews(LoopPlaybackRuntime& runtime) {
  ViewLifecycleStamp stamp{};
  if (const auto* set = runtime.viewSetIfPrewarmed()) {
    stamp.current = set->lifecycle.current;
    stamp.ready = set->lifecycle.ready;
    stamp.build = set->lifecycle.build;
    stamp.idle = set->lifecycle.idle;
    for (size_t i = 0; i < RuntimePlaybackViewConfig::kViewCount; ++i) stamp.generation[i] = set->views[i].storageGeneration;
  }
  return stamp;
}

// A coverage build whose destination generation moved is dead, not its frame's obligation (owner ruling 2026-11-03;
// host captures the device's post-overdub shape: ~12,000 Denied grains, the build never discarded, the frame timed
// out). The fixture moves the destination under an admitted manifest build: the build must retire in bounded turns,
// the frame must not block terminally, and a replacement must serve.
void test_5_1_a_dead_coverage_build_retires_and_its_frame_is_served() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const uint32_t len = loop.loopLengthTicks;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  const auto step = [&](uint32_t tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    if (tick % 4 == 0) {
      (void)t.requestActiveCommittedPlaybackWindow(tick, nativePlaybackDeadlineClock);
      lane.serviceTracks(tick);
    }
    t.playMidiEvents(tick, true);
  };
  // 1. Serve once, then reach an admitted manifest build for the next frame.
  uint32_t tick = 0;
  bool served = false;
  for (; tick < 20 * len; ++tick) {
    step(tick);
    served = served || runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving;
    if (served && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Requested &&
        runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Building && runtime.viewBuild.active())
      break;
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.viewBuild.active() &&
                               runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Building,
                           "precondition: an admitted manifest build for a Requested frame");
  // 2. Move the destination's generation underneath it (any owner's reset, recovery, clear/load would do the same).
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_TRUE(set->lifecycle.build < set->views.size());
  set->views[set->lifecycle.build].resetMetadata();
  // 3. Drive: the dead build must retire in bounded lane turns; the frame must not block terminally; it must serve.
  uint32_t deadTurns = 0, maxDeadTurns = 0, blocked = 0, servedAfter = 0;
  for (const uint32_t end = tick + 8 * len; ++tick < end;) {
    step(tick);
    if (tick % 4 == 0) {
      const bool dead = runtime.viewBuild.active() && runtime.viewBuild.nextGrain() == RuntimePlaybackViewBuildGrain::Denied;
      deadTurns = dead ? deadTurns + 1 : 0;
      maxDeadTurns = std::max(maxDeadTurns, deadTurns);
    }
    blocked += runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked;
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0)
      ++servedAfter;
  }
  char detail[200];
  snprintf(detail, sizeof(detail), "maxDeadTurns=%u blocked=%u servedAfter=%u phase=%u by=%u", maxDeadTurns, blocked,
           servedAfter, unsigned(runtime.rolling.phase), unsigned(runtime.rolling.blockedBy));
  printf("DEADBUILD %s\n", detail);
  // Within one round-robin rotation of the lane (H = 8 holders), never until the frame's deadline.
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(Config::NUM_TRACKS, maxDeadTurns, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, blocked, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, servedAfter, detail);
}

// A build admitted for a Requested routed frame must advance that frame or retire it (owner ruling 2026-11-03; device
// captures/session_20261103_095257 at 86.5-101.9 s). After a live overdub stop, during the stop job's record-return
// phase (`rw=1`), the frame's build completed but was published outside the frame (`published_as_ready`); the frame stayed
// Requested and blocked at its deadline (`CandidateNotReady`) for the rest of the transport. The fixture plays a routed
// take, overdubs it for one traversal interleaved in time, stops the overdub while playing, and keeps the device's lane
// order (reader owner, per-turn demand, owner lane, deferred idle owners) through the stop job.
struct OverdubStopOutcome { uint32_t foreign, blockedTicks, handoffMissed, servedAfter, bridgeTicksAfter; };

/// A take on track 0 slot 0 (manifest-routed when `routed`), overdubbed for one traversal interleaved in time, the overdub
/// stopped while playing, then 12 traversals in `main.cpp`'s lane order.
OverdubStopOutcome liveOverdubStopRun(bool routed) {
  if (routed) installReader(); else uninstallReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 4, 32);
  if (routed) {
    reader().serviceOwner(loop);
    reader().serviceOwner(loop);
  }
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  const uint32_t len = loop.loopLengthTicks;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint32_t notReady = 0, blockedTicks = 0, servedAfter = 0, foreignMutations = 0, handoffMissed = 0;
  uint8_t firstBlockedBy = 0;
  bool stopped = false;
  const auto step = [&](uint32_t tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    if (routed) reader().serviceOwner(loop);
    if (tick % 4 == 0) {
      if (t.isPlaying() && !t.isRecording() && !t.isOverdubbing())
        (void)t.requestActiveCommittedPlaybackWindow(tick, nativePlaybackDeadlineClock);
      lane.serviceTracks(tick);
      const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
      // Ownership: while the manifest lane owns the routed slot's coverage, no deferred owner may rotate, publish or
      // reset that slot's playback views.
      const bool manifestOwned = runtime.ownsManifestCoverage();
      const ViewLifecycleStamp before = stampViews(runtime);
      t.processDeferredIdleMaintenance(tick, false, MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false),
                                       1);
      if (manifestOwned && !(stampViews(runtime) == before)) ++foreignMutations;
    }
    t.playMidiEvents(tick, true);
    if (runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
      if (!blockedTicks) firstBlockedBy = runtime.rolling.blockedBy;
      ++blockedTicks;
      notReady += runtime.rolling.blockedBy ==
                  static_cast<uint8_t>(PlaybackTickContext::RouteReject::CandidateNotReady);
      handoffMissed += runtime.rolling.blockedBy ==
                       static_cast<uint8_t>(PlaybackTickContext::RouteReject::HandoffMissed);
    }
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    if (stopped && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && (!routed || src.manifestIdentity != 0))
      ++servedAfter;
  };
  uint32_t tick = 0;
  for (; tick < 4 * len; ++tick) step(tick);  // the routed take serves
  // One traversal of overdub, its notes between the take's.
  t.startOverdubbing(tick);
  TEST_ASSERT_TRUE_MESSAGE(t.isOverdubbing(), "precondition: overdub started");
  const uint32_t odEnd = tick + len;
  for (; tick < odEnd; ++tick) {
    if (tick % 48 == 24) t.noteOn(1, static_cast<uint8_t>(84 + (tick / 48) % 8), 100, tick);
    if (tick % 48 == 40) t.noteOff(1, static_cast<uint8_t>(84 + (tick / 48) % 8), 0, tick);
    step(tick);
  }
  t.stopOverdubbing();
  stopped = true;
  const uint32_t bridgeBefore = PlaybackTickContext::gState.route[static_cast<int>(PlaybackTickContext::TickRoute::Bridge)];
  for (const uint32_t end = tick + 12 * len; tick < end; ++tick) step(tick);
  const uint32_t bridgeTicks =
      PlaybackTickContext::gState.route[static_cast<int>(PlaybackTickContext::TickRoute::Bridge)] - bridgeBefore;
  printf("OVERDUBSTOP[%s] foreign=%u blocked=%u handoffMissed=%u notReady=%u servedAfter=%u bridgeTicks=%u phase=%u\n",
         routed ? "routed" : "unrouted", foreignMutations, blockedTicks, handoffMissed, notReady, servedAfter,
         bridgeTicks, unsigned(runtime.rolling.phase));
  return {foreignMutations, blockedTicks, handoffMissed, servedAfter, bridgeTicks};
}

// One playback source authority on a routed slot (owner ruling 2026-11-03, DEC-246 amendment 30): after a live overdub
// stop no deferred owner mutates the routed slot's views, and the stop job's bridge -- which may still exist as storage
// -- is never an eligible committed playback source there, so the Ready manifest frame is adopted. Negative control:
// the same run on an unrouted slot still plays the bridge.
void test_5_1_a_routed_slot_has_one_playback_source_after_an_overdub_stop() {
  const OverdubStopOutcome unrouted = liveOverdubStopRun(false);
  const OverdubStopOutcome routed = liveOverdubStopRun(true);
  char detail[200];
  snprintf(detail, sizeof(detail), "routed: foreign=%u bridge=%u missed=%u blocked=%u served=%u | unrouted bridge=%u",
           routed.foreign, routed.bridgeTicksAfter, routed.handoffMissed, routed.blockedTicks, routed.servedAfter,
           unrouted.bridgeTicksAfter);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, unrouted.bridgeTicksAfter, detail);  // the control: the bridge is eligible
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, routed.foreign, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, routed.bridgeTicksAfter, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, routed.handoffMissed, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, routed.blockedTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, routed.servedAfter, detail);
}

// The equal partition (DEC-246 amendment 27, owner ruling): an odd-bar loop's rolling windows are the fewest that fit
// the nominal span, sized evenly in 8-tick clock quanta, so no successor follows a shorter window into a longer one
// (a local tail scheme always has that seam; device: the 1.5 -> 2-bar wrap missed under the 3-denial bound). Never
// wrapping; the default partition is unchanged.
void test_5_1_the_equal_partition_tiles_odd_loops_evenly_on_the_clock_grid() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, S = 2 * bar, Q = Config::TICKS_PER_CLOCK;
  for (const uint32_t bars : {1u, 2u, 3u, 4u, 5u, 32u, 33u, 35u}) {
    const uint32_t len = bars * bar;
    for (bool equal : {false, true}) {
      uint32_t at = 0, shortest = UINT32_MAX, longest = 0, windows = 0, longWindows = 0, shortWindows = 0;
      while (at < len) {
        const uint32_t w = rollingWindowLengthAt(at, len, equal);
        char detail[96];
        snprintf(detail, sizeof(detail), "bars=%u equal=%d at=%u w=%u", bars, equal, at, w);
        TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, w, detail);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(S, w, detail);          // never larger than the nominal span
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(len - at, w, detail);   // never wraps the loop's end
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, at % Q, detail);             // every boundary is clock-reachable
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(at, rollingBoundaryAtOrAfter(at, len, equal), detail);
        if (at + w < len)
          TEST_ASSERT_EQUAL_UINT32_MESSAGE(at + w, rollingBoundaryAtOrAfter(at + 1, len, equal), detail);  // no gap
        shortest = std::min(shortest, w);
        longest = std::max(longest, w);
        longWindows += w == 1496;
        shortWindows += w == 1488;
        at += w;
        ++windows;
      }
      char detail[96];
      snprintf(detail, sizeof(detail), "bars=%u equal=%d windows=%u shortest=%u longest=%u", bars, equal, windows,
               shortest, longest);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(len, at, detail);                       // the windows tile the loop exactly
      TEST_ASSERT_EQUAL_UINT32_MESSAGE((len + S - 1) / S, windows, detail);    // the fewest windows that fit S
      if (equal) TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(Q, longest - shortest, detail);  // evenly sized
      if (equal && bars == 3) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1152, shortest, detail);
      if (equal && bars == 5) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1280, shortest, detail);
      if (equal && bars == 33) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, longWindows, detail);   // 6 x 1,496
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(11, shortWindows, detail); // + 11 x 1,488
      }
      if (bars % 2 == 0) TEST_ASSERT_EQUAL_UINT32_MESSAGE(S, shortest, detail);  // even loops: unchanged
    }
  }
}

// End to end on the routed slot: a dense 3-bar take serves [0, 1152) and [1152, 2304) -- no one-bar window -- plays
// continuously once a window serves, and sends every note of the take in each traversal.
// The authoritative reference: the same take on the unrouted lane (no reader installed), NoteOns in one late traversal.
uint32_t referenceNoteOnsPerTraversal(uint32_t bars, uint32_t notesPerBar) {
  uninstallReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, bars, notesPerBar);
  RollingOwnerLane lane;
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  const uint32_t len = loop.loopLengthTicks;
  size_t mark = 0;
  uint32_t ons = 0;
  MidiSendCapture::reset();
  for (uint32_t tick = 0; tick < 12 * len; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    if (tick % 4 == 0) lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    if (tick == 10 * len - 1) mark = MidiSendCapture::count();
    if (tick == 11 * len - 1)
      for (size_t i = mark; i < MidiSendCapture::count(); ++i) ons += MidiSendCapture::at(i).isNoteOn() ? 1u : 0u;
  }
  t.stopPlaying();
  return ons;
}

void balancedThreeBarTake(uint32_t notesPerBar, uint32_t bars = 3) {
  const uint32_t reference = referenceNoteOnsPerTraversal(bars, notesPerBar);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, reference, "precondition: the unrouted reference plays the take");
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, bars, notesPerBar);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const uint32_t len = loop.loopLengthTicks;
  // The successor pipeline the device measures: N+1 demanded while N serves, a full span ahead, no denials.
  RollingSuccessorProbe::configure(2 * Config::TICKS_PER_BAR, 0xFF, 0);
  TEST_ASSERT_EQUAL_UINT32(bars * Config::TICKS_PER_BAR, len);
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint32_t firstServed = UINT32_MAX, gapTicks = 0, other = 0, w0 = 0, w1 = 0;
  const uint32_t end = 60 * len;
  size_t onsAtTraversal = 0;
  uint32_t lastTraversalOns = 0, traversalsCounted = 0, minOns = UINT32_MAX, unmatched = 0;
  MidiSendCapture::reset();
  for (uint32_t tick = 0; tick < end; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    if (tick % 4 == 0) lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    PlaybackViewSourceIdentity src{};
    PlaybackCoverageInterval cov{};
    const bool serving = runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        t.publishedPlaybackWindowForSlot(0, src, cov) && src.manifestIdentity != 0;
    if (serving) {
      if (firstServed == UINT32_MAX) firstServed = tick;
      // Every served window is exactly one window of the equal partition (3 bars: [0, 1152) and [1152, 2304)).
      const bool partitionWindow = rollingBoundaryAtOrAfter(cov.startPhase, len, true) == cov.startPhase &&
          cov.lengthTicks == rollingWindowLengthAt(cov.startPhase, len, true);
      if (!partitionWindow) ++other;
      else if (cov.startPhase == 0) ++w0;
      else ++w1;
    } else if (firstServed != UINT32_MAX && tick < end - len) {
      ++gapTicks;
    }
    if (tick % len == len - 1 && firstServed != UINT32_MAX && tick > firstServed + 2 * len && tick < end - len) {
      size_t ons = 0, offs = 0;
      for (size_t i = onsAtTraversal; i < MidiSendCapture::count(); ++i) {
        ons += MidiSendCapture::at(i).isNoteOn() ? 1u : 0u;
        offs += MidiSendCapture::at(i).isNoteOff() ? 1u : 0u;
      }
      unmatched += ons > offs ? uint32_t(ons - offs) : uint32_t(offs - ons);
      lastTraversalOns = static_cast<uint32_t>(ons);
      minOns = std::min(minOns, lastTraversalOns);
      ++traversalsCounted;
    }
    // One traversal per capture window: the capture holds 8,192 events, fewer than a long dense run.
    if (tick % len == len - 1) {
      MidiSendCapture::reset();
      onsAtTraversal = 0;
    }
  }
  const auto& succ = RollingSuccessorProbe::gCounters.perTrack[0];
  char detail[260];
  snprintf(detail, sizeof(detail), "firstServed=%u w0=%u w1=%u other=%u gap=%u traversals=%u minOns=%u lastOns=%u "
           "succ req=%u handed=%u missed=%u minLead=%u", firstServed, w0, w1, other, gapTicks, traversalsCounted,
           minOns, lastTraversalOns, succ.requested, succ.handedOff, succ.missed,
           succ.minRequestLead == UINT32_MAX ? 0u : succ.minRequestLead);
  RollingSuccessorProbe::gKnobs = RollingSuccessorProbe::Knobs{};
  printf("BALANCED %s\n", detail);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(UINT32_MAX, firstServed, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, other, detail);  // no one-bar window, no other shape
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, w0, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, w1, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, traversalsCounted, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, succ.missed, detail);
  // The wrap successor's lead is the balanced window's (1,152 ticks less the request's own turn), not one bar.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(Config::TICKS_PER_BAR, succ.minRequestLead, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, gapTicks, detail);          // continuous once the first window serves
  // Every note of the take, every traversal: exactly the unrouted (authoritative) lane's count.
  printf("BALANCED reference=%u\n", reference);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(reference, minOns, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, unmatched, detail);        // each NoteOn closed in its traversal
}

void test_5_1_the_routed_slot_serves_a_3_bar_loop_in_two_balanced_windows() { balancedThreeBarTake(16); }

// A note held across the balanced seam: at 3 notes a bar the note at 1,027 sounds until 1,155, across [0, 1152) |
// [1152, 2304). It must close once, in its traversal, with no stuck or doubled note.
void test_5_1_a_note_held_across_the_balanced_seam_closes_once() { balancedThreeBarTake(3); }

// A 5-bar loop under the equal partition: 3 x 1,280 ticks, seams at 1,280 and 2,560 -- not the default 2 | 2 | 1.
void test_5_1_the_routed_slot_serves_a_5_bar_loop_in_three_equal_windows() {
  TEST_ASSERT_EQUAL_UINT32(1280, rollingWindowLengthAt(0, 5 * Config::TICKS_PER_BAR, true));
  TEST_ASSERT_EQUAL_UINT32(2560, rollingBoundaryAtOrAfter(1281, 5 * Config::TICKS_PER_BAR, true));
  balancedThreeBarTake(16, 5);
}

// The deadline-successor service bound (DEC-246 amendment 27/26, owner ruling): an eligible rolling successor with an
// active handoff deadline may be denied at most three consecutive eligible opportunities (measured on device: 3 holds
// 50/50 seams at 300 BPM with the balanced tail; 4-6 miss). Ordinary holders keep round-robin, and are never starved.
void test_5_1_a_deadline_successor_is_never_denied_more_than_three_opportunities() {
  using namespace MidiServiceDrain;
  constexpr uint8_t kHolders = 8, kSuccessor = 5;
  PlaybackHolder holders[kHolders] = {};
  for (uint8_t i = 0; i < kHolders; ++i) holders[i].eligible = true;
  holders[kSuccessor].deadlineSuccessor = true;
  holders[kSuccessor].hasDeadline = true;
  // Round-robin alone: the successor waits a whole rotation (7 denials).
  {
    PlaybackHolderCursor cursor{};
    uint32_t streak = 0, worst = 0;
    for (unsigned turn = 0; turn < 400; ++turn) {
      uint8_t chosen = kNoPlaybackHolder;
      (void)selectPlaybackHolderRoundRobin(cursor, holders, kHolders, chosen);
      streak = chosen == kSuccessor ? 0 : streak + 1;
      worst = std::max(worst, streak);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kHolders - 1, worst, "precondition: round-robin alone denies the successor 7 times");
  }
  // The bounded selection, with a competing owner that withholds every grant it may (the device injector's model).
  PlaybackHolderCursor cursor{};
  uint32_t streak = 0, worst = 0, served[kHolders] = {};
  for (unsigned turn = 0; turn < 4000; ++turn) {
    uint8_t chosen = kNoPlaybackHolder;
    (void)selectPlaybackHolderBounded(cursor, holders, kHolders, chosen);
    TEST_ASSERT_NOT_EQUAL(kNoPlaybackHolder, chosen);
    const bool withheld = chosen == kSuccessor && !playbackHolderMustBeServed(cursor, chosen, holders[chosen]);
    if (withheld) {
      notePlaybackHolderWithheld(cursor, chosen, holders[chosen]);
    } else {
      notePlaybackHolderServed(cursor, chosen);
      ++served[chosen];
    }
    streak = (chosen == kSuccessor && !withheld) ? 0 : (holders[kSuccessor].eligible ? streak + 1 : 0);
    worst = std::max(worst, streak);
  }
  char detail[96];
  snprintf(detail, sizeof(detail), "worst=%u served %u %u %u %u %u %u %u %u", worst, served[0], served[1], served[2],
           served[3], served[4], served[5], served[6], served[7]);
  printf("BOUND %s\n", detail);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(kDeadlineSuccessorMaxConsecutiveDenials, worst, detail);
  for (uint8_t i = 0; i < kHolders; ++i) TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, served[i], detail);  // no starvation
  // Not a successor any more (retired, cancelled, moved): the count resets and nothing is forced.
  holders[kSuccessor].deadlineSuccessor = false;
  uint8_t chosen = kNoPlaybackHolder;
  (void)selectPlaybackHolderBounded(cursor, holders, kHolders, chosen);
  TEST_ASSERT_EQUAL_UINT8(0, cursor.successorDenials[kSuccessor]);
}

// The offer names the deadline successor while it is owed (DEC-246 amendment 27). On the device main's per-turn demand can
// hold `pending` while the successor is in frozen qualification or being built; the flag must still be set, or the
// service bound never engages (device: deny 4 with the bound missed on a full lead).
void test_5_1_the_offer_names_the_deadline_successor_beside_main_s_demand() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 3, 16);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  RollingSuccessorProbe::configure(2 * Config::TICKS_PER_BAR, 0xFF, 0);
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  t.startPlaying(0);
  TrackCaptureStopTestAccess::unmute(t);
  uint32_t owedTurns = 0, flaggedTurns = 0, besideMainDemand = 0;
  for (uint32_t tick = 0; tick < 30 * loop.loopLengthTicks; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    reader().serviceOwner(loop);
    if (tick % 4 == 0) {
      const auto clock = clockManager.getPlaybackDeadlineClock();
      (void)t.requestActiveCommittedPlaybackWindow(static_cast<uint32_t>(clock.tick), clock);  // main's demand
      const bool frozenOwed = runtime.frozenViewRequest.active &&
          rollingSuccessorDemandIs(runtime, runtime.frozenViewRequest.deadlineTick);
      const bool buildOwed = runtime.viewBuild.active() && rollingSuccessorDemandIs(runtime, runtime.viewBuildWork.deadlineTick);
      const auto offer = t.playbackWindowServiceOffer(static_cast<uint32_t>(clock.tick), clock);
      if (offer.eligible && (frozenOwed || buildOwed)) {
        ++owedTurns;
        flaggedTurns += offer.deadlineSuccessor ? 1u : 0u;
        besideMainDemand += runtime.pendingViewRequest_.active &&
            !rollingSuccessorDemandIs(runtime, runtime.pendingViewRequest_.deadlineTick);
      }
      lane.serviceTracks(tick);
    }
    t.playMidiEvents(tick, true);
  }
  RollingSuccessorProbe::gKnobs = RollingSuccessorProbe::Knobs{};
  char detail[120];
  snprintf(detail, sizeof(detail), "owed=%u flagged=%u besideMainDemand=%u", owedTurns, flaggedTurns, besideMainDemand);
  printf("OFFERFLAG %s\n", detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, owedTurns, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(owedTurns, flaggedTurns, detail);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_5_1_a_source_stale_blocked_frame_recovers_while_playing);
  RUN_TEST(test_5_1_a_dense_take_stopped_into_play_reaches_committed_serving);
  RUN_TEST(test_5_1_a_long_dense_take_stopped_into_play_reaches_committed_serving);
  RUN_TEST(test_5_1_a_stale_serving_frame_is_retired_once_and_the_current_source_serves);
  RUN_TEST(test_5_1_the_first_window_after_a_restart_serves_on_the_manifest_route);
  RUN_TEST(test_5_1_routed_dense_take_presentation_yields_to_the_manifest_lane);
  RUN_TEST(test_5_1_an_orphaned_ready_successor_returns_its_build_role);
  RUN_TEST(test_5_1_a_price_refused_demand_leaves_no_pending_owner);
  RUN_TEST(test_5_1_a_continuity_miss_at_the_handoff_recovers);
  RUN_TEST(test_5_1_the_handoff_frontier_accepts_the_next_clock_reachable_start_only);
  RUN_TEST(test_5_1_the_routed_lane_serves_on_external_clock_pulses);
  RUN_TEST(test_5_1_an_expired_routed_demand_is_discharged_after_a_restart);
  RUN_TEST(test_5_1_an_expired_superseded_request_cannot_block_a_ready_frame);
  RUN_TEST(test_5_1_a_stale_request_at_the_frame_s_deadline_is_still_superseded);
  RUN_TEST(test_5_1_the_per_turn_demand_defers_to_a_ready_or_serving_routed_frame);
  RUN_TEST(test_5_1_a_routed_slot_has_one_playback_source_after_an_overdub_stop);
  RUN_TEST(test_5_1_a_dead_coverage_build_retires_and_its_frame_is_served);
  RUN_TEST(test_5_1_the_equal_partition_tiles_odd_loops_evenly_on_the_clock_grid);
  RUN_TEST(test_5_1_the_routed_slot_serves_a_3_bar_loop_in_two_balanced_windows);
  RUN_TEST(test_5_1_a_note_held_across_the_balanced_seam_closes_once);
  RUN_TEST(test_5_1_the_routed_slot_serves_a_5_bar_loop_in_three_equal_windows);
  RUN_TEST(test_5_1_a_deadline_successor_is_never_denied_more_than_three_opportunities);
  RUN_TEST(test_5_1_the_offer_names_the_deadline_successor_beside_main_s_demand);
  return UNITY_END();
}
