// Dedicated rolling-enabled production-path reproducer for the matched one-bar
// `RecordPublished` retirement shape (I5 — rolling Record diagnosis).
//
// The shared finalization suite's real Track/Loop/storage closure is compiled here with the
// rolling reader enabled, because the hardware stall was observed on the qualification image:
// the frozen/raw Capture reader is preserved at a non-truncating Stop and the Record job's
// release waits for a root-backed window. Without rolling compiled in, `Track::
// completeAdmittedRecordStop` cannot take that preserved-reader edge at all, so the default
// suite cannot express the shape.
//
// Only this suite's own case is registered; the included suite's cases are compiled for their
// real owners but are dead-stripped from the run.
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1
#define main sharedCaptureFinalizationSuiteMain
#define setUp sharedCaptureFinalizationSuiteSetUp
#define tearDown sharedCaptureFinalizationSuiteTearDown
// The cross-track case moves focus through the production `TrackManager::setSelectedTrack` route, which
// departs the note-edit session and invalidates the display cache first. Neither is the behaviour under
// test here, so both neighbours are observable (no-op) through the existing harness switches; every other
// suite keeps their fail-closed default, and nothing is asserted on either.
#define HARNESS_OBSERVABLE_TRACK_SELECT
#define HARNESS_OBSERVABLE_SLOT_CHANGE
#include "../test_shared_capture_finalization/test_shared_capture_finalization.cpp"
#undef main
#undef setUp
#undef tearDown

extern "C" void setUp() { sharedCaptureFinalizationSuiteSetUp(); }
extern "C" void tearDown() { sharedCaptureFinalizationSuiteTearDown(); }

namespace {

// The production deferred-owner offer/select/step shape (`main.cpp`'s window lane), driven with
// a caller-owned cursor. `main.cpp` itself is not native-linkable; this is the same seam the
// existing rolling Record fixtures use, not a copied state machine.
struct RollingOwnerLane {
  MidiServiceDrain::PlaybackHolderCursor cursor{};

  void serviceTracks(uint32_t tick) {
    const auto clock = clockManager.getPlaybackDeadlineClock();
    MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
      Track& track = trackManager.getTrack(i);
      (void)track.serviceRollingPlaybackWindow(clock);
      track.serviceAdmittedDeadlineMisses(clock);
    }
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
      offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(tick, clock);
    uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
    const auto selection = MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers, Config::NUM_TRACKS, chosen);
    if (selection == MidiServiceDrain::PlaybackHolderSelection::Service) {
      Track& chosenTrack = trackManager.getTrack(chosen);
      (void)chosenTrack.stepPlaybackViewMaintenance(tick, nullptr, true, clock);
    }
  }
};

}  // namespace

// A matched (non-truncating) one-bar Record stop must not park its job: retirement waits while
// the queued session-exit root is stale, the Snapshot owner must publish that root on its own
// bounded turns, and the job may release only after a root-backed reader has taken over.
void test_unadopted_frozen_capture_successor_does_not_park_the_record_job() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);

  clockManager.assignCurrentTickSilently(640);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  t.stopRecordingToStopped(640);
  TEST_ASSERT_TRUE(t.isStopped());
  for (unsigned turn = 0; turn < 8192 &&
       TrackCaptureStopTestAccess::phase(t) != PendingStopPhase::RecordPublished; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));

  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_TRUE(job.admission.pending && job.ownsRawRange());
  const auto* retainedReceipt = job.presentation;
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_NOT_EQUAL_UINT64(0, job.frozenCaptureContentGeneration);

  // Publish the queued Record session-exit root first, exactly as the device did.
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn) {
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  // Build the job's own frozen Capture successor into the ready role and leave it unpublished:
  // only the Track window owner can adopt or cancel it, and no reader boundary can come while the
  // transport is stopped.
  const PlaybackViewSourceIdentity frozenCapture{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t), nullptr,
      PlaybackWindowOrigin::Capture, job.frozenCaptureContentGeneration};
  const auto frozenReady = [&]() -> const RuntimePlaybackView* {
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || set->lifecycle.ready >= set->views.size()) return nullptr;
    const auto& view = set->views[set->lifecycle.ready];
    if (!view.complete || view.published) return nullptr;
    if (view.source.origin != PlaybackWindowOrigin::Capture) return nullptr;
    if (view.source.captureContentGeneration != job.frozenCaptureContentGeneration) return nullptr;
    return &view;
  };
  for (unsigned turn = 0; turn < 4096 && frozenReady() == nullptr; ++turn)
    (void)runtime.serviceFrozenRecordWindow(job.presentation->rawRange, job.presentation->rawRange.generation(), frozenCapture,
                                            bar, 400, bar, 1);
  TEST_ASSERT_NOT_NULL_MESSAGE(frozenReady(),
      "the fixture needs the job's frozen Capture successor complete and unpublished in ready");
  TEST_ASSERT_TRUE_MESSAGE(!t.isAudiblePlaybackActive(),
      "the device state is a stopped, non-audible Track with no reader frame");

  // The obligation must still end inside a bounded window.
  unsigned steps = 0;
  while (TrackCaptureStopTestAccess::presentationActive(t, 0) && steps < 8192) {
    ++steps;
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  const auto* set = runtime.viewSetIfPrewarmed();
  char detail[320];
  snprintf(detail, sizeof(detail),
      "steps=%u pending=%u phase=%u queue=%u root=%u exitQueued=%u hold=%u bound=%u "
      "ready=%u readyComplete=%u readyPublished=%u readyOrigin=%u rollPhase=%u",
      steps, t.hasPendingDeferredOverdubStop() ? 1u : 0u,
      t.hasPendingDeferredOverdubStop()
          ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)) : 255u,
      static_cast<unsigned>(TrackCaptureStopTestAccess::stopQueueDepth(t)),
      loop.publishedSnapshotIsCurrent() ? 1u : 0u,
      loop.pendingSnapshotSessionExitQueued() ? 1u : 0u,
      static_cast<unsigned>(runtime.rootReturnHoldGrains),
      runtime.hasBoundRecordReturnWindowForPendingJob() ? 1u : 0u,
      set ? set->lifecycle.ready : 255u,
      set && set->lifecycle.ready < set->views.size()
          ? static_cast<unsigned>(set->views[set->lifecycle.ready].complete) : 0u,
      set && set->lifecycle.ready < set->views.size()
          ? static_cast<unsigned>(set->views[set->lifecycle.ready].published) : 0u,
      set && set->lifecycle.ready < set->views.size()
          ? static_cast<unsigned>(set->views[set->lifecycle.ready].source.origin) : 255u,
      static_cast<unsigned>(runtime.rolling.phase));
  TEST_ASSERT_FALSE_MESSAGE(t.hasPendingDeferredOverdubStop(), detail);
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, steps, "must exercise presentation retirement, not the popped FIFO");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::presentationActive(t, 0), detail);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::extentOwned(t, 0));
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::presentation(t, 0)); // stale interval handle is refused
  const auto* presentation = retainedReceipt;
  TEST_ASSERT_FALSE(presentation->rawRange.needsSourceRelease());
  TEST_ASSERT_TRUE(presentation->rawRange.reservedEmpty());
  TEST_ASSERT_FALSE(loop.hasSealedCaptureBorrow());
  TEST_ASSERT_FALSE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_NULL(frozenReady());
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, committed);
  TEST_ASSERT_EQUAL_UINT32(2, committed.size());
  TEST_ASSERT_EQUAL_UINT32(12, committed[0].tick);
  TEST_ASSERT_EQUAL_UINT32(96, committed[1].tick);
}
// Task 3.15g — the rolling-on 2-bar chain, held as a **control** (it passes; it does not reproduce the board stall).
// A take that **wraps** its own bucket (`raw > final`) keeps two-bar geometry, the windowed lane declines that
// geometry by design (`win_window_ineligible`, `shouldAvoidFullVisualRebuild(1536)` false), the frozen Capture
// reader serves, the Record job's release waits for a root-backed reader the private root-return route prepares,
// and a **live overdub's accepted stop** waits behind it. Every step of that chain completes here within a bounded
// window; on the board the same states end `#STOP_STATUS pending=1,phase=16,queue=1/4` with the root already
// current. Whatever produces the board's stall is therefore a state this shape does not carry, so this case is the
// guard that a future repair must keep green, not the red-producer (see the 2026-10-18 packet).
void test_wrapped_two_bar_record_stop_retires_while_the_transport_runs() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t twoBars = 2 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);

  // The take passes its own wrap: accepted input keeps arriving into the second and third bar, so the captured
  // extent grows past the loop length (the device's `raw 1893 > final 1536` shape) instead of stopping at one bar,
  // while the live bar-1 owner serves and hands off.
  uint32_t tick = 0;
  for (; tick < twoBars + bar - 64; tick += 16) {
    if (tick != 0 && tick % bar == 0) {
      t.noteOn(1, static_cast<uint8_t>(64 + (tick / bar)), 100, tick + 12);
      t.noteOff(1, static_cast<uint8_t>(64 + (tick / bar)), 0, tick + 96);
    }
    clockManager.assignCurrentTickSilently(tick);
    for (unsigned turn = 0; turn < 4; ++turn)
      t.processDeferredIdleMaintenance(tick, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  clockManager.assignCurrentTickSilently(tick);
  t.stopRecording(tick);
  char geometry[192];
  snprintf(geometry, sizeof(geometry), "tick=%u loopLength=%u capturePhase=%u pending=%u", tick,
      loop.loopLengthTicks, static_cast<unsigned>(loop.capture.phase),
      t.hasPendingDeferredOverdubStop() ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(t.isPlaying(), geometry);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(twoBars, loop.loopLengthTicks,
      "a take that wrapped its first bar must keep the two-bar bucket");

  // Reach RecordPublished through the real Track deferred owner only.
  for (unsigned turn = 0; turn < 8192 &&
       TrackCaptureStopTestAccess::phase(t) != PendingStopPhase::RecordPublished; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());

  // The device shape: a **live overdub on the same slot** starts while the predecessor job is still pending, and
  // the session-exit root publishes *while that overdub is live* (device `snap_gate … hp=1,rv=1` at 40.5 s with
  // `#STOP_STATUS … pending=1,phase=16`).
  clockManager.assignCurrentTickSilently(tick);
  t.startOverdubbing(tick);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn) {
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
      "the fixture needs the root current while the record job is still pending");
  TEST_ASSERT_TRUE_MESSAGE(t.hasPendingDeferredOverdubStop(),
      "precondition: publishing the root alone must not retire the record job");

  // The overdub's own stop edge meets the pending predecessor: it is **accepted** (latched), never dropped.
  t.stopOverdubbing();

  // Drive the production owners with the transport running: the window lane's offer/select/step, the clock-edge
  // reader (the E2 root-return adoption edge lives there) and the deferred idle owners -- exactly `main.cpp`'s
  // shape. The overdub's accepted stop must execute once, which needs the predecessor to retire first.
  RollingOwnerLane lane;
  unsigned steps = 0;
  while ((t.isOverdubbing() || t.hasPendingDeferredOverdubStop()) && steps < 8192) {
    ++steps;
    tick += 16;
    clockManager.assignCurrentTickSilently(tick);
    lane.serviceTracks(tick);
    t.playMidiEvents(tick, true);
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  const auto* set = runtime.viewSetIfPrewarmed();
  char detail[400];
  snprintf(detail, sizeof(detail),
      "steps=%u tick=%u overdubbing=%u pending=%u phase=%u queue=%u root=%u exitQueued=%u bound=%u "
      "hold=%u cur=%u ready=%u readyComplete=%u readyPublished=%u readyOrigin=%u rollPhase=%u audible=%u",
      steps, tick, t.isOverdubbing() ? 1u : 0u,
      t.hasPendingDeferredOverdubStop() ? 1u : 0u,
      t.hasPendingDeferredOverdubStop()
          ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)) : 255u,
      static_cast<unsigned>(TrackCaptureStopTestAccess::stopQueueDepth(t)),
      loop.publishedSnapshotIsCurrent() ? 1u : 0u,
      loop.pendingSnapshotSessionExitQueued() ? 1u : 0u,
      runtime.hasBoundRecordReturnWindowForPendingJob() ? 1u : 0u,
      static_cast<unsigned>(runtime.rootReturnHoldGrains),
      set ? set->lifecycle.current : 255u,
      set ? set->lifecycle.ready : 255u,
      set && set->lifecycle.ready < set->views.size()
          ? static_cast<unsigned>(set->views[set->lifecycle.ready].complete) : 0u,
      set && set->lifecycle.ready < set->views.size()
          ? static_cast<unsigned>(set->views[set->lifecycle.ready].published) : 0u,
      set && set->lifecycle.ready < set->views.size()
          ? static_cast<unsigned>(set->views[set->lifecycle.ready].source.origin) : 255u,
      static_cast<unsigned>(runtime.rolling.phase),
      t.isAudiblePlaybackActive() ? 1u : 0u);
  TEST_ASSERT_FALSE_MESSAGE(t.isOverdubbing(), detail);
  TEST_ASSERT_FALSE_MESSAGE(t.hasPendingDeferredOverdubStop(), detail);
  TEST_ASSERT_TRUE_MESSAGE(t.isPlaying() || t.isStopped(), detail);
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::stopQueueDepth(t));

  // Task 3.15g: the released reader must be the **root-backed** one -- a Committed view of the current published
  // root -- not the job's frozen Capture window that was merely complete. That identity is what the board's stalled
  // state is missing (`retire_hold` mask 239: ready complete, root current, bit4 clear), so this assertion is the
  // native form of the question: does the private root-return preparation ever produce the Committed successor?
  {
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval servedCoverage{};
    char identity[256];
    snprintf(identity, sizeof(identity),
        "served=%u origin=%u rev=%u/%u gen=%u/%u snap=%u published=%u root_current=%u",
        t.publishedPlaybackWindowForSlot(0, served, servedCoverage) ? 1u : 0u,
        static_cast<unsigned>(served.origin), static_cast<unsigned>(served.playbackRevision),
        static_cast<unsigned>(loop.playbackRevision), static_cast<unsigned>(served.sourceGeneration),
        static_cast<unsigned>(TrackCaptureStopTestAccess::playbackGenerationOf(t)),
        static_cast<unsigned>(served.snapshotIdentity == loop.publishedSnapshotIdentity()),
        loop.publishedSnapshotIsCurrent() ? 1u : 0u, loop.publishedSnapshotIsCurrent() ? 1u : 0u);
    TEST_ASSERT_TRUE_MESSAGE(t.publishedPlaybackWindowForSlot(0, served, servedCoverage), identity);
    TEST_ASSERT_EQUAL_MESSAGE(PlaybackWindowOrigin::Committed, served.origin, identity);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(loop.publishedSnapshotIdentity(), served.snapshotIdentity, identity);
  }

  // The take's content survives once: the two-bar record pass keeps its committed rows.
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, committed);
  TEST_ASSERT_GREATER_THAN_UINT32(0, committed.size());
}
// Task 3.15g: the **observed** board state, fed directly (`retire_hold` mask 239): the root is current, the job's
// frozen Capture window is the served reader, and a **complete Capture-origin** successor sits in the ready role. The
// question this case asks is the one the device answers "no" to: does the private root-return route then produce a
// **Committed successor of the published root** -- the identity every release edge requires -- or does it keep treating
// the complete Capture successor as sufficient?
// Task 3.15g / I5 — the measured board state, as the fixture that must RED on **slice closure**.
//
// `retire_hold` mask 239 on the device: a wrapped two-bar take reaches the immutable `RecordPublished` boundary, its
// frozen Capture window is the **served** reader, the root is current, and the transport is audible but not playing
// (`state=OVERDUBBING`). The next overdub's stop is accepted there. On the board that stop is then latched and never
// driven for the rest of the run (`state=OVERDUBBING,pending=1,phase=16,queue=1/4`): the completed job still holds the
// interaction role, and the window lane cannot help (`win_gate x=win_track_not_playing` — it needs PLAYING, which needs
// this stop).
//
// The required behaviour is the architectural boundary, not the deadlock: once the slice is immutable, the successor
// stop must close its own slice **while** the presentation obligation is still represented, and that obligation must
// then converge through its own edge (a root-backed reader taking over, or the existing bounded expiry).
void test_a_recordpublished_presentation_obligation_does_not_hold_the_successor_stop() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t twoBars = 2 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);

  // The board's own take: ~1920 raw ticks (raw 1893 > final 1536), two-bar bucket.
  uint32_t tick = 0;
  for (; tick < 1920; tick += 16) {
    if (tick % 24 == 0) {
      t.noteOn(1, static_cast<uint8_t>(60 + ((tick / 24) % 12)), 100, tick);
      t.noteOff(1, static_cast<uint8_t>(60 + ((tick / 24) % 12)), 0, tick + 12);
    }
    clockManager.assignCurrentTickSilently(tick);
    for (unsigned turn = 0; turn < 4; ++turn)
      t.processDeferredIdleMaintenance(tick, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  clockManager.assignCurrentTickSilently(tick);
  t.stopRecording(tick);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(twoBars, loop.loopLengthTicks, "the take wraps but keeps the two-bar bucket");
  for (unsigned turn = 0; turn < 8192 &&
       TrackCaptureStopTestAccess::phase(t) != PendingStopPhase::RecordPublished; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));

  // The root becomes current in this state (the device reads `hp=1, rv=1` while the job is still pending).
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn) {
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  // The frozen Capture window is built by the production route and adopted, so it is the **served** reader.
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_TRUE_MESSAGE(job.admission.pending && job.ownsRawRange(),
                           "the RecordPublished job must still own its sealed extent");
  const PlaybackViewSourceIdentity frozenCapture{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t), nullptr,
      PlaybackWindowOrigin::Capture, job.frozenCaptureContentGeneration};
  const auto readyIsFrozenCapture = [&]() -> bool {
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || set->lifecycle.ready >= set->views.size()) return false;
    const auto& view = set->views[set->lifecycle.ready];
    return view.complete && !view.published && view.source.origin == PlaybackWindowOrigin::Capture &&
        view.source.captureContentGeneration == job.frozenCaptureContentGeneration;
  };
  for (unsigned turn = 0; turn < 4096 && !readyIsFrozenCapture(); ++turn)
    (void)runtime.serviceFrozenRecordWindow(job.presentation->rawRange, job.presentation->rawRange.generation(), frozenCapture, twoBars, 400,
                                           bar, 1);
  TEST_ASSERT_TRUE_MESSAGE(readyIsFrozenCapture(), "the frozen Capture successor must reach the ready role");
  TEST_ASSERT_TRUE_MESSAGE(t.adoptPreparedPlaybackWindow(0), "the frozen successor must be adoptable");

  // The board's transport state at the stall: audible, not playing. A successor overdub is live on the same slot.
  clockManager.assignCurrentTickSilently(tick);
  t.startOverdubbing(tick);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  for (unsigned steps = 0; steps < 4096 && loop.lastTickInLoop == UINT32_MAX; ++steps) {
    tick += 16;
    clockManager.assignCurrentTickSilently(tick);
    t.playMidiEvents(tick, true);
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(tick, false, owner, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.lastTickInLoop != UINT32_MAX,
                           "the served reader must cross a wrap, or the release edge is unreachable");

  // The two terms that make the board state what it is: the job's frozen window is the served reader, and the extent
  // is still bound to it.
  {
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval servedCoverage{};
    char bounded[288];
    snprintf(bounded, sizeof(bounded),
             "served=%u origin=%u token=%u/%u loop=%u/%u bound=%u pending=%u queue=%u",
             t.publishedPlaybackWindowForSlot(0, served, servedCoverage) ? 1u : 0u,
             static_cast<unsigned>(served.origin),
             static_cast<unsigned>(served.captureContentGeneration & 0xFFu),
             static_cast<unsigned>(job.frozenCaptureContentGeneration & 0xFFu),
             static_cast<unsigned>(servedCoverage.loopLengthTicks),
             static_cast<unsigned>(loop.loopLengthTicks),
             runtime.hasBoundRecordReturnWindowForPendingJob() ? 1u : 0u,
             t.hasPendingDeferredOverdubStop() ? 1u : 0u,
             static_cast<unsigned>(TrackCaptureStopTestAccess::stopQueueDepth(t)));
    TEST_ASSERT_TRUE_MESSAGE(t.publishedPlaybackWindowForSlot(0, served, servedCoverage), bounded);
    TEST_ASSERT_EQUAL_MESSAGE(PlaybackWindowOrigin::Capture, served.origin, bounded);
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(job.frozenCaptureContentGeneration, served.captureContentGeneration, bounded);
    TEST_ASSERT_TRUE_MESSAGE(runtime.hasBoundRecordReturnWindowForPendingJob(), bounded);
  }

  // The successor stop: DEC-078 accepts it and must never drop it.
  t.stopOverdubbing();
  TEST_ASSERT_TRUE_MESSAGE(t.hasAcceptedUnexecutedInteraction() || t.hasPendingDeferredOverdubStop() || !t.isOverdubbing(),
      "the stop must be admitted or accepted, never dropped");

  // Drive what this state makes available -- the clock-edge reader and the deferred owners; the window lane is excluded
  // while the slot is not PLAYING, exactly as on the board.
  unsigned steps = 0;
  while (t.hasAcceptedUnexecutedInteraction() && steps < 4096) {
    ++steps;
    tick += 16;
    clockManager.assignCurrentTickSilently(tick);
    t.playMidiEvents(tick, true);
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(tick, false, owner, 1);
  }
  // The verdict is captured **before** cleanup: this case must leave the tree as it found it (a pending job's sealed
  // extent and bound view would otherwise reach the cases that follow).
  const unsigned phaseAfter = t.hasPendingDeferredOverdubStop()
      ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)) : 255u;
  const unsigned queueAfter = static_cast<unsigned>(TrackCaptureStopTestAccess::stopQueueDepth(t));
  const bool extentAfter = TrackCaptureStopTestAccess::extentOwned(t, 0);
  const bool latchedAfter = t.hasAcceptedUnexecutedInteraction();
  const bool overdubbingAfter = t.isOverdubbing();
  const bool pendingAfter = t.hasPendingDeferredOverdubStop();
  const bool boundAfter = runtime.hasBoundRecordReturnWindowForPendingJob();
  const unsigned stateAfter = static_cast<unsigned>(t.getState());
  const uint32_t lastTickAfter = loop.lastTickInLoop;
  // The documented same-slot recovery route (a clear supersedes that slot's deferred stop), then let the bounded
  // release grains run so nothing of this case's obligation survives into the next case.
  t.forceSetState(TRACK_STOPPED);
  t.clear();
  for (unsigned turn = 0; turn < 512; ++turn)
    t.processDeferredIdleMaintenance(tick, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  char detail[384];
  snprintf(detail, sizeof(detail),
           "steps=%u overdubbing=%u latch=%u pending=%u phase=%u queue=%u bound=%u state=%u lastTick=%u",
           steps, overdubbingAfter ? 1u : 0u, latchedAfter ? 1u : 0u, pendingAfter ? 1u : 0u, phaseAfter,
           queueAfter, boundAfter ? 1u : 0u, stateAfter, static_cast<unsigned>(lastTickAfter));
  // The architectural boundary: an immutable slice's presentation obligation must not hold the successor's slice.
  TEST_ASSERT_FALSE_MESSAGE(latchedAfter, detail);
  TEST_ASSERT_FALSE_MESSAGE(overdubbingAfter, detail);
  // ... while the reader obligation is still represented, on its own edge, and not dropped with the job.
  TEST_ASSERT_TRUE_MESSAGE(extentAfter, "an empty successor stop must not clear the Record presentation's sealed extent obligation");
  TEST_ASSERT_TRUE_MESSAGE(boundAfter, detail);
}

// Task 3.15g: the **observed** board state, fed directly (`retire_hold` mask 239): the root is current, the job's
// frozen Capture window is the served reader, and a **complete Capture-origin** successor sits in the ready role. The
// question this case asks is the one the device answers "no" to: does the private root-return route then produce a
// **Committed successor of the published root** -- the identity every release edge requires -- or does it keep treating
// the complete Capture successor as sufficient?
void test_the_record_stop_commit_keeps_a_retained_committed_demand() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);  // accepted bar-1 pair: On@12 / Off@96
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  clockManager.assignCurrentTickSilently(640);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(640, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);

  // A retained Committed demand for this loop, live at the moment the Record stop commits.
  auto& demand = runtime.pendingViewRequest_;
  demand.active = true;
  demand.sourceKind = PlaybackViewPendingRequest::SourceKind::CommittedSnapshot;
  demand.source.origin = PlaybackWindowOrigin::Committed;
  demand.source.playbackRevision = loop.playbackRevision;
  demand.source.sourceGeneration = TrackCaptureStopTestAccess::playbackGenerationOf(t);
  demand.source.snapshotIdentity = loop.publishedSnapshotIdentity();
  demand.coverage.startPhase = 0;
  demand.coverage.lengthTicks = bar;
  demand.coverage.loopLengthTicks = bar;
  demand.loopLengthTicks = bar;
  demand.operationGeneration = TrackCaptureStopTestAccess::playbackGenerationOf(t);
  demand.reason = PlaybackViewPendingReason::None;
  demand.positionInvalidated = false;
  demand.work.admittedTick = 640;
  demand.deadlineTick = 640 + bar;
  demand.clockEpoch = 3u;

  t.stopRecording(640);
  char detail[224];
  snprintf(detail, sizeof(detail),
      "demandActive=%u demandKind=%u pending=%u phase=%u frozenActive=%u",
      runtime.pendingViewRequest_.active ? 1u : 0u,
      static_cast<unsigned>(runtime.pendingViewRequest_.sourceKind),
      t.hasPendingDeferredOverdubStop() ? 1u : 0u,
      static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)),
      runtime.frozenViewRequest.active ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(runtime.pendingViewRequest_.active, detail);
  TEST_ASSERT_EQUAL_MESSAGE(PlaybackViewPendingRequest::SourceKind::CommittedSnapshot,
                            runtime.pendingViewRequest_.sourceKind, detail);
}

void test_real_record_closure_prepares_bar_two_before_pass_publication() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  t.noteOn(1, 62, 100, bar + 12);
  t.noteOff(1, 62, 0, bar + 240);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 2 * bar;
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  clockManager.assignCurrentTickSilently(2 * bar);
  t.stopRecording(2 * bar);
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_FALSE(presentation->retirementActive);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  const auto prepare = [&](uint32_t tick) {
    nativePlaybackDeadlineClock = {tick, 3u};
    clockManager.assignCurrentTickSilently(tick);
    for (unsigned turn = 0; turn < 256 && !runtime.frozenRecordWindowReady(); ++turn)
      lane.serviceTracks(tick);
    TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
        "real finalizer admission plus closure must offer onward preparation before pass publication");
  };
  prepare(2 * bar);
  t.playMidiEvents(2 * bar, true); // adopt the genuinely missing first frozen interval
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());
  t.playMidiEvents(2 * bar + 96, true);
  prepare(2 * bar + 100);
  TEST_ASSERT_FALSE(presentation->retirementActive);
  TEST_ASSERT_FALSE(loop.passes.hasRecordPass());
  const auto* views = runtime.viewSetIfPrewarmed();
  const auto readyRole = views->lifecycle.ready;
  const auto readyStart = views->views[readyRole].coverage.startPhase;
  TEST_ASSERT_EQUAL_UINT32(bar, readyStart);
  MidiSendCapture::reset();
  for (uint32_t tick = 3 * bar; tick <= 3 * bar + 240; ++tick) {
    nativePlaybackDeadlineClock = {tick, 3u};
    clockManager.assignCurrentTickSilently(tick);
    t.playMidiEvents(tick, true);
  }
  char detail[256];
  snprintf(detail, sizeof(detail), "readyStart=%u readyRole=%u current=%u ready=%u frame=%u origin=%d last=%llu length=%u bound=%u lastLoop=%u",
      readyStart, readyRole, views->lifecycle.current, views->lifecycle.ready, unsigned(runtime.rolling.phase),
      t.getProjectionCycleStartTick(), (unsigned long long)runtime.rolling.lastTick, loop.loopLengthTicks,
      runtime.rolling.boundView, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, MidiSendCapture::count(), detail);
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOn());
  TEST_ASSERT_TRUE(MidiSendCapture::at(1).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(62, MidiSendCapture::at(0).data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(62, MidiSendCapture::at(1).data.noteData.note);
  TEST_ASSERT_FALSE(loop.passes.hasRecordPass());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());

  // Complete the finalizer while the already-served frozen interval remains authoritative.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  for (unsigned turn = 0; turn < 4096 && !loop.publishedSnapshotIsCurrent(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  nativePlaybackDeadlineClock = {3 * bar + 240, 3u};
  clockManager.assignCurrentTickSilently(3 * bar + 240);
  const auto rootReady = [&]() {
    return views->lifecycle.ready < views->views.size() &&
        views->views[views->lifecycle.ready].complete &&
        views->views[views->lifecycle.ready].source.snapshotIdentity == loop.publishedSnapshotIdentity();
  };
  for (unsigned turn = 0; turn < 1024 && !rootReady(); ++turn) lane.serviceTracks(3 * bar + 240);
  TEST_ASSERT_TRUE(rootReady());
  // Finish source cleanup before adoption: afterward no extra window grain is promised.
  for (unsigned turn = 0; turn < 256; ++turn) lane.serviceTracks(3 * bar + 240);
  TEST_ASSERT_FALSE(runtime.ownsCoverageSource());
  TEST_ASSERT_TRUE(presentation->retirementActive);
  bool rootServed = false;
  uint32_t takeoverTick = 0;
  for (uint32_t tick = 3 * bar + 241; tick <= 6 * bar && !rootServed; ++tick) {
    nativePlaybackDeadlineClock = {tick, 3u};
    clockManager.assignCurrentTickSilently(tick);
    t.playMidiEvents(tick, true);
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval coverage{};
    rootServed = t.publishedPlaybackWindowForSlot(0, served, coverage) &&
        served.snapshotIdentity == loop.publishedSnapshotIdentity();
    takeoverTick = tick;
  }
  TEST_ASSERT_TRUE_MESSAGE(rootServed, "real playback edge must adopt the prepared current-root reader");
  // No forced window turn here. Ordinary production deferred dispatch owns retirement wakeup.
  for (unsigned turn = 0; turn < 256 && presentation->retirementActive; ++turn)
    t.processDeferredIdleMaintenance(takeoverTick, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_FALSE_MESSAGE(presentation->retirementActive, "root takeover must not lose presentation retirement wakeup");
  TEST_ASSERT_FALSE(presentation->ownsExtent);
  TEST_ASSERT_FALSE(loop.hasSealedCaptureBorrow());
  TEST_ASSERT_TRUE(presentation->rawRange.reservedEmpty());
  t.startOverdubbing(takeoverTick);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  t.noteOn(1, 67, 100, takeoverTick + 12);
  t.noteOff(1, 67, 0, takeoverTick + 96);
  clockManager.assignCurrentTickSilently(takeoverTick + 100);
  t.stopOverdubbing();
  for (unsigned turn = 0; turn < 256 && t.hasAcceptedUnexecutedInteraction(); ++turn)
    t.processDeferredIdleMaintenance(takeoverTick + 100, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_FALSE(t.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_FALSE(t.isOverdubbing());
  for (unsigned turn = 0; turn < 16384 && t.hasPendingDeferredOverdubStop(); ++turn)
    t.processDeferredIdleMaintenance(takeoverTick + 100, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_FALSE(t.hasPendingDeferredOverdubStop());
  for (unsigned turn = 0; turn < 4096 && t.hasSnapshotPublicationBacklogOnAnySlot(); ++turn)
    t.processDeferredIdleMaintenance(takeoverTick + 100, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_TRUE_MESSAGE(presentation->rawRange.reservedEmpty(), "first overdub retirement must re-arm its slot range");
  clockManager.assignCurrentTickSilently(takeoverTick + 200);
  t.startOverdubbing(takeoverTick + 200);
  TEST_ASSERT_TRUE_MESSAGE(t.isOverdubbing(), "hardware second overdub must pass cold readmission after the first successor publishes");
}

void test_dense_record_presentation_releases_every_chunk_before_readmission() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  for (unsigned i = 0; i < 2 * LoopEventStoreConfig::CHUNK_CAPACITY + 8; ++i) {
    t.noteOn(1, 64, 100, 100 + 2 * i);
    t.noteOff(1, 64, 0, 101 + 2 * i);
  }
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecordingToStopped(1536);
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3, presentation->rawRange.chunkCount());
  for (unsigned turn = 0; turn < 16384 && t.hasPendingDeferredOverdubStop(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_FALSE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(presentation->retirementActive);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(3, presentation->rawRange.chunkCount(), "sealed take must still span three chunks at publication");
  for (unsigned turn = 0; turn < 16384 && !t.getLoop(0).publishedSnapshotIsCurrent(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_TRUE_MESSAGE(t.getLoop(0).publishedSnapshotIsCurrent(), "publish the dense Record root before measuring release");
  MidiServiceDrain::DeferredIdleOwnerCursor cursor{};
  unsigned partialReleases = 0;
  for (unsigned turn = 0; turn < 16384 && presentation->retirementActive; ++turn) {
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(
        cursor, t.hasSnapshotPublicationBacklogOnAnySlot(), false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
    if (!presentation->rawRange.ready() && presentation->rawRange.needsSourceRelease()) {
      ++partialReleases;
      TEST_ASSERT_TRUE_MESSAGE(presentation->retirementActive && presentation->ownsExtent,
          "partial release must retain its production wakeup and extent obligation");
    }
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, partialReleases);
  TEST_ASSERT_FALSE(presentation->retirementActive);
  TEST_ASSERT_FALSE(presentation->ownsExtent);
  TEST_ASSERT_FALSE(presentation->rawRange.needsSourceRelease());
  TEST_ASSERT_TRUE(presentation->rawRange.reservedEmpty());
  TEST_ASSERT_FALSE(t.getLoop(0).hasSealedCaptureBorrow());
  t.startOverdubbing(800);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  t.noteOn(1, 67, 100, 812);
  t.noteOff(1, 67, 0, 896);
  clockManager.assignCurrentTickSilently(1000);
  t.stopOverdubbing();
  for (unsigned turn = 0; turn < 256 && t.hasAcceptedUnexecutedInteraction(); ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  TEST_ASSERT_FALSE_MESSAGE(t.hasAcceptedUnexecutedInteraction(), "released descriptor must admit a nonempty successor");
  TEST_ASSERT_FALSE(t.isOverdubbing());
}

// DEC-143 -- between a wrapped Record take's `RecordPublished` edge and its root's publication the take had no
// servable source at all: the merged fallback is committed-only, so the gather missed and the window was held empty.
// That is the device's 3.078 s post-stop silence (`captures/i5-playback-witness-base-05`). The overlay now serves the
// slot's **sealed extent** (the object DEC-142 keeps alive), keyed to that publication edge, so playback has content
// while the root is still being built -- and one build per identity change keeps the work bounded.
void test_the_merged_fallback_serves_a_published_record_slice_before_its_root() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);

  clockManager.assignCurrentTickSilently(640);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  t.stopRecordingToStopped(640);
  TEST_ASSERT_TRUE(t.isStopped());
  // The **negative half of the gate**: once the stop has admitted its job the extent is sealed and borrowed, but the
  // take has not crossed `RecordPublished`, so it is still mutable material behind the live finalizer and must not
  // reach the merged representation. Without this the overlay could serve a take mid-finalization.
  // The **negative half of the gate** is sampled on the first turn where the admitted stop owns its sealed extent but
  // the take has not crossed `RecordPublished`: it is then still mutable material behind the live finalizer and must not
  // reach the merged representation.
  bool sampledPrePublication = false;
  for (unsigned turn = 0; turn < 8192 &&
       TrackCaptureStopTestAccess::phase(t) != PendingStopPhase::RecordPublished; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    if (sampledPrePublication || !loop.hasSealedCaptureBorrow() ||
        TrackCaptureStopTestAccess::phase(t) == PendingStopPhase::RecordPublished) {
      continue;
    }
    sampledPrePublication = true;
    auto& prePublicationRuntime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    TEST_ASSERT_FALSE(t.hasPublishedRecordPresentationForSlot(0));
    // Force a fresh gather so this sample cannot pass by reusing an earlier build.
    prePublicationRuntime.mergedMidiEvents.clear();
    (void)ensurePlaybackMergedMidiEventsBuilt(t, loop, prePublicationRuntime, true, 640);
    TEST_ASSERT_TRUE_MESSAGE(prePublicationRuntime.mergedMidiEvents.empty(),
        "a pre-publication Record take is mutable material and must not be served by the merged fallback");
  }
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE_MESSAGE(sampledPrePublication,
      "the fixture must sample the admitted-but-unpublished state");

  // The measured state: the take is published and immutable, its root has not published yet, and the Loop holds the
  // read borrow on the presentation record's sealed extent.
  TEST_ASSERT_NULL_MESSAGE(loop.publishedSnapshot().get(),
      "the overlay only applies before the containing root publishes");
  TEST_ASSERT_TRUE(loop.hasSealedCaptureBorrow());
  TEST_ASSERT_TRUE(t.hasPublishedRecordPresentationForSlot(0));

  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  resetPlaybackMergedRebuildCountForTest();
  TEST_ASSERT_TRUE(ensurePlaybackMergedMidiEventsBuilt(t, loop, runtime, true, 640));
  TEST_ASSERT_FALSE_MESSAGE(runtime.mergedMidiEvents.empty(),
      "a published Record slice is read material: the merged fallback must not hold an empty window");
  // The take's own accepted note, served from the sealed extent in the loop window.
  unsigned takeNoteOns = 0;
  unsigned takeNoteOffs = 0;
  for (const MidiEvent& event : runtime.mergedMidiEvents.mergedEvents) {
    if (event.data.noteData.note != 60) continue;
    if (event.isNoteOn()) ++takeNoteOns;
    if (event.isNoteOff()) ++takeNoteOffs;
  }
  TEST_ASSERT_EQUAL_UINT32(1u, takeNoteOns);
  TEST_ASSERT_EQUAL_UINT32(1u, takeNoteOffs);
  TEST_ASSERT_EQUAL_UINT32(0u, runtime.mergedMidiEvents.windowStartTick);
  TEST_ASSERT_EQUAL_UINT32(loop.loopLengthTicks, runtime.mergedMidiEvents.windowLengthTicks);
  const unsigned rebuildsWhileServing = playbackMergedRebuildCountForTest();
  TEST_ASSERT_EQUAL_UINT32(1u, rebuildsWhileServing);

  // Bounded work: the same publication identity performs no further gather, however many maintenance turns run.
  for (unsigned turn = 0; turn < 16; ++turn) {
    TEST_ASSERT_FALSE(ensurePlaybackMergedMidiEventsBuilt(t, loop, runtime, true, 640));
  }
  TEST_ASSERT_EQUAL_UINT32(rebuildsWhileServing, playbackMergedRebuildCountForTest());

  // The root publishes: the overlay is superseded in exactly one rebuild and nothing is doubled.
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn) {
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(ensurePlaybackMergedMidiEventsBuilt(t, loop, runtime, true, 640));
  TEST_ASSERT_EQUAL_UINT32(rebuildsWhileServing + 1, playbackMergedRebuildCountForTest());
  unsigned rootNoteOns = 0;
  for (const MidiEvent& event : runtime.mergedMidiEvents.mergedEvents) {
    if (event.isNoteOn() && event.data.noteData.note == 60) ++rootNoteOns;
  }
  TEST_ASSERT_EQUAL_UINT32(takeNoteOns, rootNoteOns);
}
// DEC-143 follow-up -- the post-stop phase reset must not silence the rest of the pass.
//
// The device run that proved the sealed-extent overlay (`captures/i5-dec143-sealed-extent-base-01`) also showed the
// wire staying quiet for 3.076 s after the wrapped record stop even though the merged window held the take: the stop
// resets the loop's phase (`lastTickInLoop = UINT32_MAX`), and the turn that establishes a phase past the first bar
// returned without writing it back, so every later turn re-entered the same branch until the wrap. This fixture drives
// the production entry with that exact state: a second-bar interval, PLAYING, and an unknown previous phase.
void test_a_post_stop_phase_reset_does_not_silence_the_rest_of_the_pass() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  Loop& loop = t.getLoop(0);
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1000, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(1100, 1, 60, 0)));
  loop.loopLengthTicks = 1536;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 1536));

  // Publish the root, so the merged fallback has a committed source for the whole loop.
  MidiServiceDrain::DeferredIdleOwnerCursor idleCursor{};
  for (unsigned turn = 0; turn < 8192 && !loop.publishedSnapshotIsCurrent(); ++turn) {
    const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot();
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(idleCursor, backlog, false);
    t.processDeferredIdleMaintenance(0, false, owner, 1);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  // The device's post-stop state: audible, PLAYING, and the loop's phase reset with the playhead past the first bar.
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::armEmit(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = UINT32_MAX;

  MidiSendCapture::reset();
  // Phase 800 is past the first bar: this turn establishes the phase and emits no catch-up burst.
  t.playMidiEvents(800, true);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  // The interval's own events are still owed from here on, and the next turn must serve them.
  t.playMidiEvents(1105, true);
  size_t ons = 0;
  size_t offs = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if (event.isNoteOn() && event.data.noteData.note == 60) ++ons;
    if (event.isNoteOff() && event.data.noteData.note == 60) ++offs;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ons,
      "a latched phase reset silences the rest of the pass: the owed note-on must reach the wire");
  TEST_ASSERT_EQUAL_UINT32(1, offs);
}
// DEC-136 window progression -- the **first** frozen return window must cover the interval the playhead is in.
//
// The device measured the consequence (`captures/i5-dec143-phase-seed-base-01`): during finalization the take is not yet
// read material for the merged fallback (DEC-143 keeps its `RecordPublished` edge), so playback depends on the frozen
// return window -- and the preparation targeted the *later* bar while the playhead sat in bar one (`adopt_wait,e2_boundary`
// with `start=768` in a 1536-tick loop), leaving 1.7 s of silence before publication. This case holds the playhead in
// bar one of a two-bar take across the finalization window and requires the preparation to serve that same interval.
void test_the_first_frozen_return_window_covers_the_playhead_interval() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 2 * bar;
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  clockManager.assignCurrentTickSilently(2 * bar);
  t.stopRecording(2 * bar);
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_FALSE(presentation->retirementActive);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));

  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  RollingOwnerLane lane;
  // Hold the playhead inside bar one for the whole finalization window.
  constexpr uint32_t holdTick = 320;
  for (unsigned turn = 0; turn < 256; ++turn) {
    nativePlaybackDeadlineClock = {holdTick, 3u};
    clockManager.assignCurrentTickSilently(holdTick);
    lane.serviceTracks(holdTick);
    if (runtime.frozenRecordWindowReady()) break;
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
      "the pending job must prepare a frozen return window before publication");
  const auto* views = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(views);
  const auto& ready = views->views[views->lifecycle.ready];
  char detail[224];
  snprintf(detail, sizeof(detail), "readyStart=%u readyLen=%u current=%u ready=%u frame=%u bound=%u",
      ready.coverage.startPhase, ready.coverage.lengthTicks, views->lifecycle.current,
      views->lifecycle.ready, unsigned(runtime.rolling.phase), runtime.rolling.boundView);
  // The prepared window is the one the playhead is in, not the later bar.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ready.coverage.startPhase, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(bar, ready.coverage.lengthTicks, detail);

  // Continuous playback through finalization: the take's bar-one note reaches the wire from the frozen window while the
  // pass is still unpublished and the merged fallback has nothing (DEC-143's edge is intact).
  TEST_ASSERT_FALSE(loop.passes.hasRecordPass());
  TEST_ASSERT_FALSE(presentation->retirementActive);
  // The frozen window's own qualification is what the reader keys on, so the fixture pins it.
  RuntimePlaybackViewSource frozenInspection{};
  PlaybackViewSourceIdentity receiptInspection{};
  TEST_ASSERT_TRUE(t.qualifiedFrozenRecordCaptureSourceForSlot(
      0, presentation->interval, frozenInspection, receiptInspection));
  TEST_ASSERT_EQUAL_UINT64(ready.source.captureContentGeneration,
                           frozenInspection.captureContentToken);
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::armEmit(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  MidiSendCapture::reset();
  for (uint32_t tick = holdTick; tick <= holdTick + 260; ++tick) {
    nativePlaybackDeadlineClock = {tick, 3u};
    clockManager.assignCurrentTickSilently(tick);
    t.playMidiEvents(tick, true);
  }
  size_t ons = 0;
  size_t offs = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if (event.data.noteData.note != 60) continue;
    if (event.isNoteOn()) ++ons;
    if (event.isNoteOff()) ++offs;
  }
  const auto& rc = views->views[views->lifecycle.current < views->views.size()
      ? views->lifecycle.current : views->lifecycle.ready];
  char served[320];
  snprintf(served, sizeof(served),
      "sent=%u cur=%u ready=%u curComp=%d curPub=%d curRaw=%d curFirst=%d curOrigin=%d curStart=%u curLen=%u "
      "readyComp=%d readyPub=%d readyRaw=%d readyStart=%u servable=%d bound=%d "
      "rollingPhase=%d boundView=%d paged=%d permits=%d jobFrozen=%llu readyFirst=%d firstIdx=%d owns=%d",
      (unsigned)MidiSendCapture::count(), views->lifecycle.current, views->lifecycle.ready,
      (int)rc.complete, (int)rc.published, (int)rc.rawCoveragePrepared, (int)rc.firstRecordReturnWindow,
      (int)rc.source.origin, rc.coverage.startPhase, rc.coverage.lengthTicks,
      (int)ready.complete, (int)ready.published, (int)ready.rawCoveragePrepared, ready.coverage.startPhase,
      (int)runtime.hasServableDeferredRecordView(), (int)runtime.hasBoundRecordReturnWindowForPendingJob(),
      (int)runtime.rolling.phase, (int)runtime.rolling.boundView,
      (int)runtime.pagedEmission.bound,
      (int)TrackCaptureStopTestAccess::head(t).admission.permitsPlaybackEmission(),
      (unsigned long long)TrackCaptureStopTestAccess::head(t).frozenCaptureContentGeneration,
      (int)ready.firstRecordReturnWindow, (int)views->firstRecordReturnWindowIndex(),
      (int)presentation->ownsExtent);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ons, served);
  TEST_ASSERT_EQUAL_UINT32(1, offs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_post_stop_phase_reset_does_not_silence_the_rest_of_the_pass);
  RUN_TEST(test_a_recordpublished_presentation_obligation_does_not_hold_the_successor_stop);
  RUN_TEST(test_real_record_closure_prepares_bar_two_before_pass_publication);
  RUN_TEST(test_dense_record_presentation_releases_every_chunk_before_readmission);
  RUN_TEST(test_published_record_presentation_prepares_frozen_return_before_root_is_current);
  RUN_TEST(test_record_bind_refusal_recovers_and_prepares_second_bar);
  RUN_TEST(test_record_bind_refusal_keeps_second_bar_audible_during_overdub);
  RUN_TEST(test_record_bind_refusal_keeps_base_during_record_publication);
  RUN_TEST(test_record_ready_before_publication_survives_owner_and_adoption);
  RUN_TEST(test_the_first_frozen_return_window_covers_the_playhead_interval);
  RUN_TEST(test_the_merged_fallback_serves_a_published_record_slice_before_its_root);
  RUN_TEST(test_refreshed_raw_record_view_keeps_spare_role_after_bind_refusal);
  RUN_TEST(test_unadopted_frozen_capture_successor_does_not_park_the_record_job);
  RUN_TEST(test_the_record_stop_commit_keeps_a_retained_committed_demand);
  return UNITY_END();
}
