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

// The balanced tail (DEC-246 amendment 27, owner ruling): an odd-bar loop's rolling partition must not end in a short
// window, whose successor at the wrap gets only that window's length as lead (device: 768 ticks on 33 bars, every
// deny-3 miss). The last two windows are split evenly instead, never wrapping. The default partition is unchanged.
void test_5_1_the_balanced_tail_partition_tiles_odd_loops_without_a_short_window() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR, S = 2 * bar;
  struct Shape { uint32_t bars; uint32_t shortest; };
  for (const Shape shape : {Shape{3, 1152}, Shape{33, 1152}, Shape{35, 1152}, Shape{32, S}, Shape{1, bar},
                            Shape{2, S}, Shape{5, 1152}}) {
    const uint32_t len = shape.bars * bar;
    for (bool balanced : {false, true}) {
      uint32_t at = 0, shortest = UINT32_MAX, windows = 0;
      while (at < len) {
        const uint32_t w = rollingWindowLengthAt(at, len, balanced);
        char detail[96];
        snprintf(detail, sizeof(detail), "bars=%u balanced=%d at=%u w=%u", shape.bars, balanced, at, w);
        TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, w, detail);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(S, w, detail);           // never larger than the nominal span
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(len - at, w, detail);    // never wraps the loop's end
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(at, rollingBoundaryAtOrAfter(at, len, balanced), detail);  // a boundary
        if (at + w < len) TEST_ASSERT_EQUAL_UINT32_MESSAGE(at + w, rollingBoundaryAtOrAfter(at + 1, len, balanced),
                                                           detail);   // and the next one: no gap, no overlap
        shortest = std::min(shortest, w);
        at += w;
        ++windows;
      }
      TEST_ASSERT_EQUAL_UINT32(len, at);  // the windows tile the loop exactly
      char detail[64];
      snprintf(detail, sizeof(detail), "bars=%u balanced=%d windows=%u", shape.bars, balanced, windows);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(balanced ? shape.shortest : std::min(S, len % S ? len % S : S), shortest,
                                       detail);
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

void balancedThreeBarTake(uint32_t notesPerBar) {
  const uint32_t reference = referenceNoteOnsPerTraversal(3, notesPerBar);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, reference, "precondition: the unrouted reference plays the take");
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, 3, notesPerBar);
  reader().serviceOwner(loop);
  reader().serviceOwner(loop);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const uint32_t len = loop.loopLengthTicks;
  // The successor pipeline the device measures: N+1 demanded while N serves, a full span ahead, no denials.
  RollingSuccessorProbe::configure(2 * Config::TICKS_PER_BAR, 0xFF, 0);
  TEST_ASSERT_EQUAL_UINT32(3 * Config::TICKS_PER_BAR, len);
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
      if (cov.startPhase == 0 && cov.lengthTicks == 1152) ++w0;
      else if (cov.startPhase == 1152 && cov.lengthTicks == 1152) ++w1;
      else ++other;
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
    if (tick % len == len - 1) onsAtTraversal = MidiSendCapture::count();
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
  RUN_TEST(test_5_1_the_balanced_tail_partition_tiles_odd_loops_without_a_short_window);
  RUN_TEST(test_5_1_the_routed_slot_serves_a_3_bar_loop_in_two_balanced_windows);
  RUN_TEST(test_5_1_a_note_held_across_the_balanced_seam_closes_once);
  return UNITY_END();
}
