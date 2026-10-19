// Controlled functional opportunities, not target cadence or deadline proof.
namespace {
struct RollingRecordCoverageFixture {
  Track& track;
  LoopPlaybackRuntime* runtime;
  MidiServiceDrain::PlaybackHolderCursor cursor{};
  explicit RollingRecordCoverageFixture(uint32_t origin = 0, bool prepareInitial = true, bool attach = false)
      : track(attach ? trackManager.getTrack(0) : beginRunningRecord(0, origin)),
      runtime(TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0)) {
    TEST_ASSERT_NOT_NULL(runtime);
    if (prepareInitial) for (unsigned i = 0; i < 64; ++i) owner();
  }
  void owner() {
    const auto clock = clockManager.getPlaybackDeadlineClock();
    MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
      auto& candidate = trackManager.getTrack(i);
      (void)candidate.serviceRollingPlaybackWindow(clock);
      candidate.serviceAdmittedDeadlineMisses(clock);
    }
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
      offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(uint32_t(clock.tick), clock);
    uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
    if (MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers, Config::NUM_TRACKS, chosen) ==
        MidiServiceDrain::PlaybackHolderSelection::Service)
      (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(uint32_t(clock.tick), nullptr, true, clock);
  }
  void advance(uint32_t tick, bool service = true) {
    TEST_ASSERT_EQUAL_UINT32(0, tick % Config::TICKS_PER_CLOCK);
    while (clockManager.getCurrentTick() < tick) {
      pulses(0, 1);
      if (service) for (unsigned i = 0; i < Config::TICKS_PER_CLOCK; ++i) owner();
    }
    TEST_ASSERT_EQUAL_UINT32(tick, clockManager.getCurrentTick());
  }
  void input(bool on, uint8_t pitch) {
    push(0, on ? midi::NoteOn : midi::NoteOff, pitch, on ? 100 : 0);
    poll();
  }
  const RuntimePlaybackView& current() const {
    const auto* set = runtime->viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    TEST_ASSERT_TRUE(set->lifecycle.current < set->views.size());
    return set->views[set->lifecycle.current];
  }
  void frame(uint32_t period, uint32_t start, int32_t origin) {
    TEST_ASSERT_EQUAL(LoopPlaybackRuntime::RollingPhase::Serving, runtime->rolling.phase);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(period, runtime->rolling.loopLength,
        "live Record must adopt the complete later-interval period through the rolling reader");
    TEST_ASSERT_EQUAL_UINT32(start, current().coverage.startPhase);
    TEST_ASSERT_EQUAL_UINT32(Config::TICKS_PER_BAR, current().coverage.lengthTicks);
    TEST_ASSERT_EQUAL_UINT32(period, current().coverage.loopLengthTicks);
    TEST_ASSERT_EQUAL_INT32(origin, runtime->rolling.projectionStart);
    TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, current().source.origin);
    TEST_ASSERT_TRUE(current().complete && current().published && !current().rawCoveragePrepared);
    TEST_ASSERT_EQUAL_UINT64(current().storageGeneration, runtime->rolling.boundStorageGeneration);
  }
  void sounding(unsigned count) {
    unsigned actual = 0;
    runtime->ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++actual; });
    TEST_ASSERT_EQUAL_UINT32(count, actual);
  }
  void event(uint32_t tick, unsigned count, bool on, uint8_t pitch, uint32_t sourceTick, NoteId id) {
    advance(tick);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, MidiSendCapture::count(), "exact independent rolling output inventory");
    const auto& event = MidiSendCapture::at(count - 1);
    TEST_ASSERT_EQUAL(on, event.isNoteOn());
    TEST_ASSERT_EQUAL(!on, event.isNoteOff());
    TEST_ASSERT_EQUAL_UINT8(1, event.channel);
    TEST_ASSERT_EQUAL_UINT8(pitch, event.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(on ? 100 : 0, event.data.noteData.velocity);
    TEST_ASSERT_EQUAL_UINT32(sourceTick, event.tick);
    TEST_ASSERT_EQUAL_UINT32(id, event.noteId);
    sounding(on ? 1 : 0);
    trackManager.updateAllTracks(tick);
    TEST_ASSERT_EQUAL_UINT32(count, MidiSendCapture::count());
  }
};

void rollingRecordCoversLaterBarsAndRetainsOwedInterval() {
  RollingRecordCoverageFixture f;
  f.advance(64); f.input(true, 60);
  f.advance(96); f.input(false, 60);
  const auto firstId = f.track.getLoop(0).capture.store.at(0).noteId;
  f.advance(800);
  const auto& old = f.current();
  const uint64_t storage = old.storageGeneration;
  const auto source = old.source;
  f.advance(832); f.input(true, 62);
  f.advance(864); f.input(false, 62);
  TEST_ASSERT_EQUAL_UINT64(storage, old.storageGeneration);
  TEST_ASSERT_TRUE(old.source == source);
  TEST_ASSERT_EQUAL_UINT32(2, old.storage.eventCount);
  const auto secondId = f.track.getLoop(0).capture.store.at(2).noteId;
  f.advance(880);
  MidiSendCapture::reset();
  f.advance(1536);
  f.frame(1536, 0, 1536);
  TEST_ASSERT_EQUAL_UINT32(2, f.current().storage.eventCount);
  f.event(1600, 1, true, 60, 64, firstId);
  f.input(true, 64); // New third-bar content must not replace owed bar two.
  f.event(1632, 2, false, 60, 96, firstId);
  f.advance(1640); f.input(false, 64);
  const auto thirdId = f.track.getLoop(0).capture.store.at(4).noteId;
  for (unsigned i = 0; i < 16; ++i) f.owner();
  const auto& request = f.runtime->pendingViewRequest_;
  TEST_ASSERT_EQUAL_UINT32(768, request.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(1536, request.coverage.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT64(2304, request.deadlineTick);
  f.advance(2304);
  f.frame(1536, 768, 1536);
  TEST_ASSERT_EQUAL_UINT32(2, f.current().storage.eventCount);
  f.event(2368, 3, true, 62, 832, secondId);
  f.event(2400, 4, false, 62, 864, secondId);
  f.advance(3072);
  f.frame(2304, 0, 3072);
  f.event(3136, 5, true, 60, 64, firstId);
  f.event(3168, 6, false, 60, 96, firstId);
  f.advance(3840); f.frame(2304, 768, 3072);
  f.event(3904, 7, true, 62, 832, secondId);
  f.event(3936, 8, false, 62, 864, secondId);
  f.advance(4608); f.frame(2304, 1536, 3072);
  TEST_ASSERT_EQUAL_UINT32(2, f.current().storage.eventCount);
  f.event(4672, 9, true, 64, 1600, thirdId);
  f.event(4712, 10, false, 64, 1640, thirdId);
  TEST_ASSERT_EQUAL_UINT32(6, f.track.getLoop(0).capture.store.size());
  TEST_ASSERT_TRUE(f.track.isRecording());
}

void rollingRecordKeepsHeldIdentityAcrossLiveIntervals() {
  RollingRecordCoverageFixture f;
  f.advance(736); f.input(true, 65);
  // Deliberately miss the initial successor while accepting its crossing Off.
  // This is a named functional recovery case, never a deadline-readiness PASS.
  f.advance(800, false); f.input(false, 65);
  const auto id = f.track.getLoop(0).capture.store.at(0).noteId;
  for (unsigned i = 0; i < 32; ++i) f.owner();
  MidiSendCapture::reset();
  f.advance(1536); f.frame(1536, 0, 1536);
  f.event(2272, 1, true, 65, 736, id);
  const auto* priorView = &f.current();
  f.advance(2304); f.frame(1536, 768, 1536);
  TEST_ASSERT_TRUE(priorView != &f.current());
  f.sounding(1);
  TEST_ASSERT_EQUAL_UINT32(1, MidiSendCapture::count());
  f.event(2336, 2, false, 65, 800, id);
}
}

namespace {
void seedTwoLiveRecordBars(RollingRecordCoverageFixture& f) {
  f.advance(64); f.input(true, 60);
  f.advance(96); f.input(false, 60);
  f.advance(832); f.input(true, 62);
  f.advance(1008); f.input(false, 62);
}

void rollingRecordMatchingStopKeepsSubintervalCurrent() {
  RollingRecordCoverageFixture f;
  seedTwoLiveRecordBars(f);
  const auto firstId = f.track.getLoop(0).capture.store.at(0).noteId;
  const auto secondId = f.track.getLoop(0).capture.store.at(2).noteId;
  f.advance(1536); f.frame(1536, 0, 1536);
  TEST_ASSERT_EQUAL_UINT32(2, f.current().storage.eventCount);
  TEST_ASSERT_EQUAL_UINT32(4, f.track.getLoop(0).capture.store.size());
  const auto* set = f.runtime->viewSetIfPrewarmed();
  const auto role = set->lifecycle.current;
  const auto storage = f.current().storageGeneration;
  const auto token = f.current().source.captureContentGeneration;
  const auto cursor = f.track.getLoop(0).nextEventIndex;
  const auto lastTick = f.runtime->rolling.lastTick;
  MidiSendCapture::reset();
  f.track.stopRecording(1536);
  TEST_ASSERT_TRUE(f.track.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(1536, f.track.getLoop(0).loopLengthTicks);
  TEST_ASSERT_TRUE(f.track.getLoop(0).hasSealedCaptureBorrow());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0));
  TEST_ASSERT_EQUAL_UINT8(role, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(storage, f.current().storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(token, f.current().source.captureContentGeneration);
  TEST_ASSERT_EQUAL_UINT64(lastTick, f.runtime->rolling.lastTick);
  TEST_ASSERT_EQUAL_UINT32(cursor, f.track.getLoop(0).nextEventIndex);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.build < set->views.size(), "matching Stop must leave a legal private build role");
  f.event(1600, 1, true, 60, 64, firstId);
  f.event(1632, 2, false, 60, 96, firstId);
  f.advance(2304);
  char detail[240];
  snprintf(detail, sizeof(detail), "frozen continuation current=%u ready=%u build=%u idle=%u retained=%u readyComplete=%u active=%u grain=%u borrow=%u bound=%u", set->lifecycle.current, set->lifecycle.ready, set->lifecycle.build, set->lifecycle.idle, set->lifecycle.retained, set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete, f.runtime->viewBuild.active(), unsigned(f.runtime->viewBuild.nextGrain()), f.runtime->viewBuild.ownsLiveCaptureBorrow(), f.runtime->hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(768, f.current().coverage.startPhase, detail);
  f.event(2368, 3, true, 62, 832, secondId);
  f.event(2544, 4, false, 62, 1008, secondId);
  TEST_ASSERT_TRUE(f.track.getLoop(0).hasSealedCaptureBorrow());
}



void rollingRecordMismatchingStopPreparesCorrectedGeometry() {
  RollingRecordCoverageFixture f;
  seedTwoLiveRecordBars(f);
  const auto secondId = f.track.getLoop(0).capture.store.at(2).noteId;
  f.advance(1536); f.frame(1536, 0, 1536);
  f.advance(1600); f.input(true, 64);
  f.advance(2000); f.input(false, 64);
  f.advance(2304); f.frame(1536, 768, 1536);
  const auto& old = f.current();
  const auto oldStorage = old.storageGeneration;
  const auto oldSource = old.source;
  MidiSendCapture::reset();
  f.track.stopRecording(2304);
  TEST_ASSERT_EQUAL_UINT32(2304, f.track.getLoop(0).loopLengthTicks);
  TEST_ASSERT_TRUE(f.track.getLoop(0).hasSealedCaptureBorrow());
  TEST_ASSERT_EQUAL_UINT64(oldStorage, old.storageGeneration);
  TEST_ASSERT_TRUE(old.source == oldSource);
  const auto* stoppedSet = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE_MESSAGE(stoppedSet->lifecycle.build < stoppedSet->views.size(), "mismatching Stop must leave a legal private build role");
  TEST_ASSERT_EQUAL_UINT32(1536, old.coverage.loopLengthTicks);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0));
  // The old live view has the wrong geometry. Recover/adopt the current corrected interval first.
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto* recoverySet = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(f.runtime->frozenRecordWindowReady());
  TEST_ASSERT_EQUAL_UINT32(0, recoverySet->views[recoverySet->lifecycle.ready].coverage.startPhase);
  f.track.playMidiEvents(2304, true);
  TEST_ASSERT_FALSE(f.runtime->frozenRecordWindowReady());
  f.advance(3064, false);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto* set = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.ready < set->views.size());
  const auto& ready = set->views[set->lifecycle.ready];
  char detail[220];
  snprintf(detail, sizeof(detail), "frozen mismatch ready=%u build=%u idle=%u retained=%u active=%u grain=%u borrow=%u bound=%u candidateRows=%u", set->lifecycle.ready, set->lifecycle.build, set->lifecycle.idle, set->lifecycle.retained, f.runtime->viewBuild.active(), unsigned(f.runtime->viewBuild.nextGrain()), f.runtime->viewBuild.ownsLiveCaptureBorrow(), f.runtime->hasBoundRecordReturnWindowForPendingJob(), set->lifecycle.build < set->views.size() ? unsigned(set->views[set->lifecycle.build].storage.eventCount) : 999u);
  TEST_ASSERT_TRUE_MESSAGE(ready.complete, detail);
  TEST_ASSERT_EQUAL_UINT32(2304, ready.coverage.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(768, ready.coverage.startPhase);
  MidiSendCapture::reset();
  f.advance(3072, false);
  TEST_ASSERT_EQUAL_UINT64(ready.storageGeneration, f.runtime->rolling.boundStorageGeneration);
  TEST_ASSERT_EQUAL_UINT32(2304, f.current().coverage.loopLengthTicks);
  f.event(3136, 1, true, 62, 832, secondId);
  f.event(3312, 2, false, 62, 1008, secondId);
  TEST_ASSERT_TRUE(f.track.getLoop(0).hasSealedCaptureBorrow());
}

void rollingRecordLateIntervalDoesNotAdoptMidTraversal() {
  RollingRecordCoverageFixture f;
  seedTwoLiveRecordBars(f);
  const auto firstId = f.track.getLoop(0).capture.store.at(0).noteId;
  const auto secondId = f.track.getLoop(0).capture.store.at(2).noteId;
  f.advance(1528);
  f.advance(1536, false); f.frame(1536, 0, 1536);
  const auto storage = f.current().storageGeneration;
  const auto* priorView = &f.current();
  f.advance(2312, false); // No main owner opportunity for the next interval.
  for (unsigned i = 0; i < 32; ++i) f.owner();
  TEST_ASSERT_EQUAL_UINT64(2304, f.runtime->pendingViewRequest_.deadlineTick);
  TEST_ASSERT_EQUAL_UINT64(storage, f.current().storageGeneration);
  TEST_ASSERT_TRUE(f.runtime->lagState.missedTraversals != 0);
  MidiSendCapture::reset();
  f.advance(2400, false);
  TEST_ASSERT_EQUAL_UINT64(storage, f.current().storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  f.advance(3072);
  TEST_ASSERT_EQUAL_UINT64(storage, f.current().storageGeneration);
  f.event(3136, 1, true, 60, 64, firstId);
  f.event(3168, 2, false, 60, 96, firstId);
  f.advance(3840); f.frame(1536, 768, 1536);
  TEST_ASSERT_TRUE(priorView != &f.current());
  f.event(3904, 3, true, 62, 832, secondId);
  f.event(4080, 4, false, 62, 1008, secondId);
}
}

namespace {
void rollingRecordGrowthPreservesNonzeroRawOrigin() {
  RollingRecordCoverageFixture f(512);
  f.advance(576); f.input(true, 60);
  f.advance(608); f.input(false, 60);
  f.advance(1344); f.input(true, 62);
  f.advance(1520); f.input(false, 62);
  TEST_ASSERT_EQUAL_UINT32(64, f.track.getLoop(0).capture.store.at(0).tick);
  TEST_ASSERT_EQUAL_UINT32(832, f.track.getLoop(0).capture.store.at(2).tick);
  const auto firstId = f.track.getLoop(0).capture.store.at(0).noteId;
  const auto secondId = f.track.getLoop(0).capture.store.at(2).noteId;
  f.advance(1536);
  MidiSendCapture::reset();
  f.advance(2048); f.frame(1536, 0, 2048);
  TEST_ASSERT_EQUAL_UINT32(512, f.track.getLoop(0).startLoopTick);
  f.event(2112, 1, true, 60, 64, firstId);
  f.event(2144, 2, false, 60, 96, firstId);
  f.advance(2816); f.frame(1536, 768, 2048);
  f.event(2880, 3, true, 62, 832, secondId);
  f.event(3056, 4, false, 62, 1008, secondId);
  TEST_ASSERT_EQUAL_UINT32(512, f.track.getLoop(0).startLoopTick);
}

void rollingRecordEmptyMiddleIntervalIsCompleteSilence() {
  RollingRecordCoverageFixture f;
  f.advance(64); f.input(true, 60);
  f.advance(96); f.input(false, 60);
  f.advance(1600); f.input(true, 64);
  f.advance(1640); f.input(false, 64);
  const auto firstId = f.track.getLoop(0).capture.store.at(0).noteId;
  const auto thirdId = f.track.getLoop(0).capture.store.at(2).noteId;
  f.advance(1680);
  MidiSendCapture::reset();
  f.advance(2304); f.frame(2304, 0, 2304);
  f.event(2368, 1, true, 60, 64, firstId);
  f.event(2400, 2, false, 60, 96, firstId);
  f.advance(3072); f.frame(2304, 768, 2304);
  TEST_ASSERT_EQUAL_UINT32(0, f.current().storage.eventCount);
  f.advance(3832);
  TEST_ASSERT_EQUAL_UINT32(2, MidiSendCapture::count());
  f.sounding(0);
  f.advance(3840); f.frame(2304, 1536, 2304);
  f.event(3904, 3, true, 64, 1600, thirdId);
  f.event(3944, 4, false, 64, 1640, thirdId);
}
}

namespace {
void rollingRecordGrowthDoesNotRetriggerAlreadySoundingIdentity() {
  RollingRecordCoverageFixture f;
  f.advance(736); f.input(true, 65);
  f.advance(800); f.input(false, 65);
  const auto id = f.track.getLoop(0).capture.store.at(0).noteId;
  f.advance(808);
  MidiSendCapture::reset();
  f.event(1504, 1, true, 65, 736, id);
  f.advance(1536); f.frame(1536, 0, 1536);
  f.sounding(1);
  f.advance(2272);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, MidiSendCapture::count(),
      "period growth must not retrigger a NoteId still sounding from the old traversal");
  f.sounding(1);
  f.advance(2304); f.frame(1536, 768, 1536);
  f.event(2336, 2, false, 65, 800, id);
}
}

namespace {
void rollingRecordProductionVisualTurnPreservesSuccessor() {
  RollingRecordCoverageFixture f;
  f.advance(192); f.input(true, 60);
  f.owner();
  f.advance(288, false); f.input(false, 60);
  const auto id = f.track.getLoop(0).capture.store.at(0).noteId;
  for (unsigned i = 0; i < 64; ++i) {
    f.owner();
    f.track.updateGrowingRecordLoopLength(clockManager.getCurrentTick());
    const auto& loop = f.track.getLoop(0);
    const PlaybackViewSourceIdentity legacySource{loop.playbackRevision,
        TrackCaptureStopTestAccess::playbackGenerationOf(f.track), loop.publishedSnapshotIdentity()};
    (void)f.runtime->serviceFirstRecordingBar(loop, legacySource, Config::TICKS_PER_BAR,
        MidiServiceDrain::kCanonicalPrefixInputQuantum);
  }
  const auto* set = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.ready < set->views.size());
  const auto& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_TRUE_MESSAGE(ready.complete,
      "production VisualCache turn must not discard rolling-owned live Capture preparation");
  TEST_ASSERT_EQUAL_UINT32(2, ready.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT64(f.track.getLoop(0).captureContentGeneration(), ready.source.captureContentGeneration);
  MidiSendCapture::reset();
  f.advance(768, false); f.frame(768, 0, 0);
  f.event(960, 1, true, 60, 192, id);
  f.event(1056, 2, false, 60, 288, id);
}
}

namespace {
void rollingRecordSecondTakeReclaimsCompletedIdleShell() {
  RollingRecordCoverageFixture f(0, false);
  auto* views = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(views->lifecycle.idle < views->views.size());
  auto& idle = views->views[views->lifecycle.idle];
  MidiEvent old;
  old.tick = 64; old.type = midi::NoteOn; old.channel = 1;
  old.data.noteData.note = 60; old.data.noteData.velocity = 98; old.noteId = 99;
  RuntimePlaybackViewBuild oldWorker;
  const PlaybackViewSourceIdentity oldSource{0, 0, nullptr, PlaybackWindowOrigin::Capture, 99};
  TEST_ASSERT_TRUE(oldWorker.begin(idle, &old, 1, oldSource, {0, 768, 768}, {}, {}));
  for (unsigned i = 0; i < 32 && oldWorker.active(); ++i) {
    if (oldWorker.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) oldWorker.stepFill(64);
    else oldWorker.stepOrder(48);
  }
  TEST_ASSERT_TRUE(idle.complete && !idle.published);
  oldWorker.discard(&idle); // Prior worker detached; only its idle copy remains.
  for (unsigned i = 0; i < 64; ++i) f.owner();
  f.advance(192); f.input(true, 67);
  f.advance(288); f.input(false, 67);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto* set = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.ready < set->views.size());
  const auto& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_TRUE_MESSAGE(ready.complete && ready.source.captureContentGeneration ==
      f.track.getLoop(0).captureContentGeneration(), "second Record must not be blocked by a previous take's complete idle shell");
  TEST_ASSERT_EQUAL_UINT32(2, ready.storage.eventCount);
  const auto id = f.track.getLoop(0).capture.store.at(0).noteId;
  MidiSendCapture::reset();
  f.advance(768); f.frame(768, 0, 0);
  f.event(960, 1, true, 67, 192, id);
  f.event(1056, 2, false, 67, 288, id);
}
}

namespace {
void rollingRecordLegacyVisualTurnBeforeFirstOwner() {
  RollingRecordCoverageFixture f(0, false);
  f.advance(192, false); f.input(true, 67);
  auto& loop = f.track.getLoop(0);
  const PlaybackViewSourceIdentity source{0, TrackCaptureStopTestAccess::playbackGenerationOf(f.track),
      loop.publishedSnapshotIdentity()};
  // Production VisualCache can win multiple turns before the rolling holder.
  for (unsigned i = 0; i < 2; ++i)
    (void)f.runtime->serviceFirstRecordingBar(loop, source, Config::TICKS_PER_BAR, 1);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  f.advance(288); f.input(false, 67);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto* set = f.runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.ready < set->views.size());
  const auto& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_TRUE_MESSAGE(ready.complete && ready.source.captureContentGeneration == loop.captureContentGeneration(),
      "legacy-first VisualCache must not strand the rolling successor");
  TEST_ASSERT_EQUAL_UINT32(2, ready.storage.eventCount);
}
}

namespace {
void rollingRecordInitialPublicationBeforeClockKeepsNextDeadline() {
  RollingRecordCoverageFixture f(0, false);
  for (unsigned i = 0; i < 32; ++i) {
    f.owner();
    const auto* set = f.runtime->viewSetIfPrewarmed();
    if (set->lifecycle.current < set->views.size() && set->views[set->lifecycle.current].published) break;
  }
  TEST_ASSERT_TRUE(f.current().complete && f.current().published);
  TEST_ASSERT_EQUAL(LoopPlaybackRuntime::RollingPhase::Idle, f.runtime->rolling.phase);
  f.owner(); // Main wins one more turn before the first clock reader.
  f.advance(8);
  TEST_ASSERT_EQUAL(LoopPlaybackRuntime::RollingPhase::Serving, f.runtime->rolling.phase);
  f.advance(192); f.input(true, 67);
  f.advance(288); f.input(false, 67);
  for (unsigned i = 0; i < 32; ++i) f.owner();
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(768, f.runtime->pendingViewRequest_.deadlineTick,
      "a published initial current must not leave a new zero-deadline request across clock admission");
  const auto id = f.track.getLoop(0).capture.store.at(0).noteId;
  MidiSendCapture::reset();
  f.advance(768); f.frame(768, 0, 0);
  f.event(960, 1, true, 67, 192, id);
  f.event(1056, 2, false, 67, 288, id);
}
}

namespace {
void rollingRecordHeldMismatchStopGetsFrozenReader() {
  RollingRecordCoverageFixture f;
  f.advance(728); f.input(true, 65);
  f.advance(920); f.input(false, 65);
  f.advance(3352);
  TEST_ASSERT_EQUAL_UINT32(1536, f.runtime->rolling.loopLength);
  f.track.stopRecording(3352);
  TEST_ASSERT_EQUAL_UINT32(1536, f.track.getLoop(0).loopLengthTicks);
  TEST_ASSERT_TRUE(f.runtime->hasBoundRecordReturnWindowForPendingJob());
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto tick = clockManager.getCurrentTick();
  f.advance(((tick / 768) + 1) * 768);
  {
    const auto* v = f.runtime->viewSetIfPrewarmed();
    const auto& cand = v->views[v->lifecycle.build];
    printf("HELD ADM probe tick%u srcEq%u candRows%u qualified%u srcFilled%u candSrcOrigin%u frozenOrigin%u complete%u units%u qualifiedUnits%u\n",
        clockManager.getCurrentTick(), cand.source == f.runtime->frozenViewRequest.source,
        unsigned(cand.storage.eventCount), f.runtime->viewBuild.qualifiedEventCount(),
        cand.sourceFilled, unsigned(cand.source.origin), unsigned(f.runtime->frozenViewRequest.source.origin),
        f.runtime->coverageSource.complete(), f.runtime->coverageSource.readUnits(), f.runtime->coverageSourceUnits);
  }
  TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::RollingPhase::Serving, f.runtime->rolling.phase,
      "held crossing mismatch Stop must adopt a corrected frozen rolling reader");
}
}

namespace {
void rollingRecordHeldMismatchStopWithDeferredOwners() {
  RollingRecordCoverageFixture f;
  f.advance(728); f.input(true, 65);
  f.advance(920); f.input(false, 65);
  f.advance(3352);
  f.track.stopRecording(3352);
  TEST_ASSERT_EQUAL_UINT32(1536, f.track.getLoop(0).loopLengthTicks);
  TEST_ASSERT_TRUE(f.runtime->hasBoundRecordReturnWindowForPendingJob());
  unsigned lastStateKey = UINT32_MAX;
  for (unsigned turn = 0; turn < 512; ++turn) {
    f.owner();
    if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
      (void)TrackCaptureStopTestAccess::step(f.track);
    {
      const auto* v = f.runtime->viewSetIfPrewarmed();
      const unsigned key = (unsigned(f.runtime->coveragePreparation) << 12) | (f.runtime->ownsCoverageSource() << 11)
          | (f.runtime->coverageSource.complete() << 10) | (f.runtime->coverageSource.retiring() << 9)
          | (f.runtime->coverageSource.readUnits() != f.runtime->coverageSourceUnits) << 8
          | (v->lifecycle.current << 6) | (v->lifecycle.ready << 4) | (v->lifecycle.build << 2)
          | unsigned(f.runtime->viewBuild.active());
      if (key != lastStateKey)
        printf("HELD STATE turn%u root%u prep%u own%u complete%u retiring%u unitsMismatch%u cur%u ready%u build%u active%u grain%u frozen%u/%llu candRows%u qualified%u srcFilled%u rev%u\n",
            turn, f.track.getLoop(0).publishedSnapshotIsCurrent(), unsigned(f.runtime->coveragePreparation),
            f.runtime->ownsCoverageSource(), f.runtime->coverageSource.complete(), f.runtime->coverageSource.retiring(),
            f.runtime->coverageSource.readUnits() != f.runtime->coverageSourceUnits,
            v->lifecycle.current, v->lifecycle.ready, v->lifecycle.build, f.runtime->viewBuild.active(),
            unsigned(f.runtime->viewBuild.nextGrain()), f.runtime->frozenViewRequest.active,
            static_cast<unsigned long long>(f.runtime->frozenViewRequest.deadlineTick),
            v->lifecycle.build < v->views.size() ? unsigned(v->views[v->lifecycle.build].storage.eventCount) : 0u,
            f.runtime->coverageEventCount, v->views[v->lifecycle.build].sourceFilled,
            unsigned(v->views[v->lifecycle.build].source.playbackRevision));
      lastStateKey = key;
    }
    if (turn < 4 || (turn > 450 && turn % 8 == 0)) {
      const auto* v = f.runtime->viewSetIfPrewarmed();
      printf("HELD turn%u tick%u root%u phase%u cur%u/%u ready%u/%u/%u build%u/%u prep%u own%u req%u/%u debt%llu\n", turn,
          clockManager.getCurrentTick(), f.track.getLoop(0).publishedSnapshotIsCurrent(), unsigned(f.runtime->rolling.phase),
          v->lifecycle.current, unsigned(f.current().source.origin), v->lifecycle.ready,
          v->lifecycle.ready < v->views.size() ? unsigned(v->views[v->lifecycle.ready].source.origin) : 99u,
          v->lifecycle.ready < v->views.size() ? unsigned(v->views[v->lifecycle.ready].complete) : 0u,
          v->lifecycle.build, unsigned(f.runtime->viewBuild.nextGrain()), unsigned(f.runtime->coveragePreparation),
          f.runtime->ownsCoverageSource(), f.runtime->pendingViewRequest_.active,
          unsigned(f.runtime->pendingViewRequest_.reason), static_cast<unsigned long long>(f.runtime->pendingViewRequest_.deadlineTick));
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(f.track.getLoop(0).publishedSnapshotIsCurrent(), "short held take root must be published before releasing the clock");
  unsigned lastCandRows = UINT32_MAX;
  while (clockManager.getCurrentTick() < 768) {
    pulses(0, 1);
    f.owner();
    const auto* v = f.runtime->viewSetIfPrewarmed();
    const unsigned candRows = v->lifecycle.build < v->views.size()
        ? unsigned(v->views[v->lifecycle.build].storage.eventCount) : 0u;
    if (candRows != lastCandRows || clockManager.getCurrentTick() >= 752)
      printf("HELD ROWS tick%u rows%u qualified%u prep%u own%u cur%u ready%u build%u grain%u\n",
          clockManager.getCurrentTick(), candRows, f.runtime->coverageEventCount,
          unsigned(f.runtime->coveragePreparation), f.runtime->ownsCoverageSource(),
          v->lifecycle.current, v->lifecycle.ready, v->lifecycle.build, unsigned(f.runtime->viewBuild.nextGrain()));
    lastCandRows = candRows;
    const unsigned key = (unsigned(f.runtime->coveragePreparation) << 12) | (f.runtime->ownsCoverageSource() << 11)
        | (f.runtime->coverageSource.complete() << 10) | (f.runtime->coverageSource.retiring() << 9)
        | (f.runtime->coverageSource.readUnits() != f.runtime->coverageSourceUnits) << 8
        | (v->lifecycle.current << 6) | (v->lifecycle.ready << 4) | (v->lifecycle.build << 2)
        | unsigned(f.runtime->viewBuild.active());
    if (key != lastStateKey)
      printf("HELD ADV tick%u root%u frame%u prep%u own%u complete%u retiring%u unitsMismatch%u cur%u ready%u build%u active%u grain%u frozen%u/%llu\n",
          clockManager.getCurrentTick(), f.track.getLoop(0).publishedSnapshotIsCurrent(),
          unsigned(f.runtime->rolling.phase), unsigned(f.runtime->coveragePreparation),
          f.runtime->ownsCoverageSource(), f.runtime->coverageSource.complete(), f.runtime->coverageSource.retiring(),
          f.runtime->coverageSource.readUnits() != f.runtime->coverageSourceUnits,
          v->lifecycle.current, v->lifecycle.ready, v->lifecycle.build, f.runtime->viewBuild.active(),
          unsigned(f.runtime->viewBuild.nextGrain()), f.runtime->frozenViewRequest.active,
          static_cast<unsigned long long>(f.runtime->frozenViewRequest.deadlineTick));
    lastStateKey = key;
  }
  TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::RollingPhase::Serving, f.runtime->rolling.phase,
      "root publication before first frozen boundary must not prevent reader adoption");
  bool servedFrozen = false;
  // DEC-136 containment oracles: the private preparation must never tear the served frozen reader, must never
  // promise a deadline it cannot keep, and must end by serving the root-backed successor S7.5b.5 waits for.
  bool privateOwedSeen = false;
  bool frozenReaderBound = false;
  uint64_t frozenReaderStorage = 0;
  bool rootSuccessorServed = false;
  unsigned lastMaintKey = UINT32_MAX;
  for (unsigned pulse = 0; pulse < 400; ++pulse) {
    pulses(0, 1);
    if (pulse < 3) {
      auto* postPulse = TrackCaptureStopTestAccess::runtimeIfAllocated(f.track, 0);
      if (postPulse && unsigned(postPulse->coveragePreparation) != 2u)
        printf("HELD POSTPULSE pulse%u tick%u prep%u retiring%u own%u frozen%u\n", pulse,
            clockManager.getCurrentTick(), unsigned(postPulse->coveragePreparation),
            postPulse->coverageSource.retiring(), postPulse->ownsCoverageSource(),
            postPulse->frozenViewRequest.active);
    }
    for (unsigned turn = 0; turn < 8; ++turn) {
      {
        const auto clock = clockManager.getPlaybackDeadlineClock();
        MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
        for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
          (void)trackManager.getTrack(i).serviceRollingPlaybackWindow(clock);
          trackManager.getTrack(i).serviceAdmittedDeadlineMisses(clock);
        }
        if (pulse < 3) {
          auto* afterRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(f.track, 0);
          if (afterRuntime && unsigned(afterRuntime->coveragePreparation) != 2u)
            printf("HELD AFTERSVC pulse%u tick%u prep%u retiring%u own%u frozen%u\n", pulse,
                clockManager.getCurrentTick(), unsigned(afterRuntime->coveragePreparation),
                afterRuntime->coverageSource.retiring(), afterRuntime->ownsCoverageSource(),
                afterRuntime->frozenViewRequest.active);
        }
        for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
          offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(uint32_t(clock.tick), clock);
        uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
        if (MidiServiceDrain::selectPlaybackHolderRoundRobin(f.cursor, offers, Config::NUM_TRACKS, chosen) ==
            MidiServiceDrain::PlaybackHolderSelection::Service)
          chosen = chosen; // keep selected slot for the step below
        auto& entryTrack = trackManager.getTrack(chosen == MidiServiceDrain::kNoPlaybackHolder ? 0 : chosen);
        auto* entryRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(entryTrack,
            entryTrack.getActiveLoopIndex());
        const unsigned entryPrep = entryRuntime ? unsigned(entryRuntime->coveragePreparation) : 99u;
        const bool entryRetiring = entryRuntime != nullptr && entryRuntime->coverageSource.retiring();
        const bool entryOwn = entryRuntime != nullptr && entryRuntime->ownsCoverageSource();
        const bool entryFrozen = entryRuntime != nullptr && entryRuntime->frozenViewRequest.active;
        if (pulse < 3 && entryRuntime && entryPrep != 99u &&
            (entryPrep != 2u || !entryOwn || entryRetiring || !entryFrozen))
          printf("HELD PRESTEP pulse%u tick%u prep%u retiring%u own%u frozen%u\n", pulse,
              clockManager.getCurrentTick(), entryPrep, entryRetiring, entryOwn, entryFrozen);
        const auto result = chosen == MidiServiceDrain::kNoPlaybackHolder
            ? Track::PlaybackViewTurnResult::Idle
            : trackManager.getTrack(chosen).stepPlaybackViewMaintenance(uint32_t(clock.tick), nullptr, true, clock);
        const unsigned mkey = (unsigned(result) << 4) | (entryPrep << 1) | (chosen != MidiServiceDrain::kNoPlaybackHolder);
        if (mkey != lastMaintKey)
          printf("HELD MAINT pulse%u turn%u tick%u chosen%u result%u entryPrep%u entryRetiring%u entryOwn%u entryFrozen%u exitPrep%u exitOwn%u exitFrozen%u\n",
              pulse, turn, clockManager.getCurrentTick(), chosen, unsigned(result),
              entryPrep, entryRetiring, entryOwn, entryFrozen,
              entryRuntime ? unsigned(entryRuntime->coveragePreparation) : 99u,
              entryRuntime ? entryRuntime->ownsCoverageSource() : false,
              entryRuntime ? entryRuntime->frozenViewRequest.active : false);
        // DEC-136: the bound frozen reader's root-return successor is prepared **privately** here -- a coverage
        // build in service while the job is bound and its frozen reader is owed, and (item 3) it carries no
        // position at all, because the route promises no deadline it could not keep.
        if (entryRuntime != nullptr && entryRuntime->coverageBuildNeedsService()) {
          privateOwedSeen = true;
          TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, entryRuntime->viewBuildWork.deadlineTick,
              "the private root-return build must not carry a fabricated deadline");
          TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, entryRuntime->viewBuildWork.clockEpoch,
              "the private root-return build must not carry a fabricated clock position");
        }
        lastMaintKey = mkey;
      }
      if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
        (void)TrackCaptureStopTestAccess::step(f.track);
    }
    const auto& current = f.current();
    {
      const auto* v = f.runtime->viewSetIfPrewarmed();
      const unsigned key = (unsigned(f.runtime->coveragePreparation) << 12) | (f.runtime->ownsCoverageSource() << 11)
          | (f.runtime->coverageSource.complete() << 10) | (f.runtime->coverageSource.retiring() << 9)
          | (f.runtime->coverageSource.readUnits() != f.runtime->coverageSourceUnits) << 8
          | (v->lifecycle.current << 6) | (v->lifecycle.ready << 4) | (v->lifecycle.build << 2)
          | unsigned(f.runtime->viewBuild.active());
      if (key != lastStateKey)
        printf("HELD POST pulse%u tick%u frame%u prep%u own%u complete%u retiring%u unitsMis%u cur%u/%u ready%u build%u active%u grain%u frozen%u/%llu pend%u/%u\n",
            pulse, clockManager.getCurrentTick(), unsigned(f.runtime->rolling.phase),
            unsigned(f.runtime->coveragePreparation), f.runtime->ownsCoverageSource(),
            f.runtime->coverageSource.complete(), f.runtime->coverageSource.retiring(),
            f.runtime->coverageSource.readUnits() != f.runtime->coverageSourceUnits,
            v->lifecycle.current, unsigned(current.source.origin), v->lifecycle.ready, v->lifecycle.build,
            f.runtime->viewBuild.active(), unsigned(f.runtime->viewBuild.nextGrain()),
            f.runtime->frozenViewRequest.active, static_cast<unsigned long long>(f.runtime->frozenViewRequest.deadlineTick),
            f.runtime->pendingViewRequest_.active, unsigned(f.runtime->pendingViewRequest_.reason));
      lastStateKey = key;
    }
    if (pulse < 110 && pulse % 8 == 0) {
      const auto* v = f.runtime->viewSetIfPrewarmed();
      printf("HELD edge tick%u frame%u cur%u/%u/%u ready%u/%u build%u/%u prep%u own%u req%u/%u debt%llu\n",
          clockManager.getCurrentTick(), unsigned(f.runtime->rolling.phase), v->lifecycle.current,
          unsigned(current.source.origin), unsigned(current.source.playbackRevision), v->lifecycle.ready,
          v->lifecycle.ready < v->views.size() ? unsigned(v->views[v->lifecycle.ready].source.origin) : 99u,
          v->lifecycle.build, unsigned(f.runtime->viewBuild.nextGrain()), unsigned(f.runtime->coveragePreparation),
          f.runtime->ownsCoverageSource(), f.runtime->frozenViewRequest.active,
          unsigned(f.runtime->frozenViewRequest.reason), static_cast<unsigned long long>(f.runtime->frozenViewRequest.deadlineTick));
    }
    // DEC-136 containment oracles: the private preparation must never tear the served frozen reader and must end
    // by serving the root-backed successor S7.5b.5 waits for. The route's own signature -- a coverage build in
    // service while the bound frozen reader is owed, carrying **no position at all** -- is sampled on the lane
    // turn itself (the inner loop), because such a build completes inside one turn's worth of owned grains.
    const bool frozenReaderServed = current.complete && current.published &&
        current.source.origin == PlaybackWindowOrigin::Capture &&
        current.coverage.startPhase == Config::TICKS_PER_BAR;
    if (frozenReaderServed) {
      if (!frozenReaderBound) {
        frozenReaderBound = true;
        frozenReaderStorage = current.storageGeneration;
      } else {
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(frozenReaderStorage, current.storageGeneration,
            "private root preparation must not tear the served frozen reader");
      }
      TEST_ASSERT_TRUE_MESSAGE(current.coverage.contains(IntervalProjection::tickPhaseInProjectionCycle(
          clockManager.getCurrentTick(), f.track.getProjectionCycleStartTick(), 1536)),
          "the frozen reader must cover the playhead until the root successor takes over");
    }
    rootSuccessorServed = rootSuccessorServed ||
        (current.complete && current.published &&
         current.source.origin == PlaybackWindowOrigin::Committed &&
         current.source.snapshotIdentity == f.track.getLoop(0).publishedSnapshotIdentity());
    servedFrozen = servedFrozen || (f.runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
        current.complete && current.published &&
        (current.source.origin == PlaybackWindowOrigin::Capture ||
         current.source.origin == PlaybackWindowOrigin::Committed) &&
        current.coverage.loopLengthTicks == 1536 &&
        current.coverage.contains(IntervalProjection::tickPhaseInProjectionCycle(
            clockManager.getCurrentTick(), f.track.getProjectionCycleStartTick(), 1536)) &&
        current.storageGeneration == f.runtime->rolling.boundStorageGeneration);
  }
  TEST_ASSERT_TRUE_MESSAGE(privateOwedSeen,
      "the bound frozen reader's root-return demand must be prepared privately (DEC-136)");
  TEST_ASSERT_TRUE_MESSAGE(rootSuccessorServed,
      "the completed root-backed successor must take over the reader the job's release waits on (S7.5b.5)");
  TEST_ASSERT_TRUE_MESSAGE(servedFrozen, "real Stop/Snapshot grains must not prevent held frozen reader adoption");
  char state[480];
  const auto* set = f.runtime->viewSetIfPrewarmed();
  snprintf(state, sizeof state, "held phase=%u root=%u pendingRoot=%u frame=%u cur=%u ready=%u build=%u origin=%u rdOrigin=%u borrow=%u stop=%u req=%u kind=%u reason=%u debt=%llu cov=%lu/%lu prep=%u ownSource=%u grain=%u",
      f.runtime->captureStopOwner ? unsigned(f.runtime->captureStopOwner->phase) : 999u,
      f.track.getLoop(0).publishedSnapshotIsCurrent(), f.track.getLoop(0).pendingSnapshotSessionExitQueued(),
      unsigned(f.runtime->rolling.phase), set->lifecycle.current, set->lifecycle.ready, set->lifecycle.build,
      unsigned(f.current().source.origin), set->lifecycle.ready < set->views.size() ? unsigned(set->views[set->lifecycle.ready].source.origin) : 999u,
      f.runtime->viewBuild.ownsLiveCaptureBorrow(), f.runtime->hasBoundRecordReturnWindowForPendingJob(),
      f.runtime->pendingViewRequest_.active, unsigned(f.runtime->pendingViewRequest_.sourceKind),
      unsigned(f.runtime->pendingViewRequest_.reason), static_cast<unsigned long long>(f.runtime->pendingViewRequest_.deadlineTick),
      static_cast<unsigned long>(f.runtime->pendingViewRequest_.coverage.startPhase),
      static_cast<unsigned long>(f.runtime->pendingViewRequest_.coverage.lengthTicks),
      unsigned(f.runtime->coveragePreparation), f.runtime->ownsCoverageSource(), unsigned(f.runtime->viewBuild.nextGrain()));
  {
    const auto& p = f.runtime->pendingViewRequest_;
    const auto& loop0 = f.track.getLoop(0);
    printf("HELD DIAG pend=%u kind=%u reason=%u epoch=%u deadline=%llu admitted=%llu invalid=%u ll=%u cov=%lu/%lu phase=%u\n",
        p.active, unsigned(p.sourceKind), unsigned(p.reason), p.clockEpoch,
        static_cast<unsigned long long>(p.deadlineTick),
        static_cast<unsigned long long>(p.work.admittedTick), p.positionInvalidated,
        p.loopLengthTicks, static_cast<unsigned long>(p.coverage.startPhase),
        static_cast<unsigned long>(p.coverage.lengthTicks), p.requestedPhase);
    printf("HELD DIAG2 srcRev=%u srcGen=%u srcOrigin=%u srcSnap=%u rootRev=%u gen=%u snapEq=%u buildActive=%u owned=%u pendingJudge=%u rollPhase=%u rollEpoch=%u rollGen=%u rollLen=%u rollBound=%u\n",
        p.source.playbackRevision, p.source.sourceGeneration, unsigned(p.source.origin),
        p.source.snapshotIdentity != nullptr ? 1u : 0u, loop0.playbackRevision,
        TrackCaptureStopTestAccess::playbackGenerationOf(f.track),
        p.source.snapshotIdentity == loop0.publishedSnapshotIdentity() ? 1u : 0u,
        f.runtime->viewBuild.active() ? 1u : 0u, f.runtime->viewBuild.coverageOwned() ? 1u : 0u,
        f.runtime->completedPromisePendingJudgement ? 1u : 0u,
        unsigned(f.runtime->rolling.phase), f.runtime->rolling.epoch, f.runtime->rolling.generation,
        f.runtime->rolling.loopLength, f.runtime->rolling.boundView);
    printf("HELD DIAG3 liveJob=%u interval=%llu rootCurrent=%u exitQueued=%u servable=%u bound=%u audible=%u\n",
        TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0) ? 1u : 0u,
        static_cast<unsigned long long>(TrackCaptureStopTestAccess::headCaptureStopInterval(f.track)),
        loop0.publishedSnapshotIsCurrent() ? 1u : 0u,
        loop0.pendingSnapshotSessionExitQueued() ? 1u : 0u,
        f.runtime->hasServableDeferredRecordView() ? 1u : 0u,
        f.runtime->hasBoundRecordReturnWindowForPendingJob() ? 1u : 0u,
        f.track.isAudiblePlaybackActive() ? 1u : 0u);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0),
      state);
}
}

namespace {
/// DEC-136 cross-take: once the route's Stop job is gone, its position-free root-return demand must not
/// outlive it, and the next take's live Record demand must be created.
void privateRootReturnDemandDoesNotOutliveItsJobIntoNextTake() {
  RollingRecordCoverageFixture f;
  f.advance(728); f.input(true, 65);
  f.advance(920); f.input(false, 65);
  f.advance(3352);
  f.track.stopRecording(3352);
  for (unsigned turn = 0; turn < 512; ++turn) {
    f.owner();
    if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
      (void)TrackCaptureStopTestAccess::step(f.track);
  }
  while (clockManager.getCurrentTick() < 768) { pulses(0, 1); f.owner(); }
  for (unsigned pulse = 0; pulse < 1200 &&
       TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0); ++pulse) {
    pulses(0, 1);
    for (unsigned turn = 0; turn < 8; ++turn) {
      f.owner();
      if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
        (void)TrackCaptureStopTestAccess::step(f.track);
    }
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0),
      "the route's Stop job must have been released");
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto& pend = f.runtime->pendingViewRequest_;
  TEST_ASSERT_FALSE_MESSAGE(pend.active && pend.reason == PlaybackViewPendingReason::PrivateRootReturnPreparation,
      "a position-free root-return demand must not outlive its Stop job (pending)");
  TEST_ASSERT_FALSE_MESSAGE(f.runtime->frozenViewRequest.active &&
      f.runtime->frozenViewRequest.reason == PlaybackViewPendingReason::PrivateRootReturnPreparation,
      "a position-free root-return demand must not outlive its Stop job (frozen)");
  // Next take: clear the slot and record again; live frames must grow.
  f.track.clear();
  for (unsigned i = 0; i < 64; ++i) f.owner();
  TEST_ASSERT_TRUE(f.track.startRecording(clockManager.getCurrentTick() / Config::TICKS_PER_CLOCK * Config::TICKS_PER_CLOCK));
  for (unsigned i = 0; i < 64; ++i) f.owner();
  f.input(true, 67);
  f.advance(clockManager.getCurrentTick() + 96);
  f.input(false, 67);
  f.advance(clockManager.getCurrentTick() + 960);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::RollingPhase::Serving, f.runtime->rolling.phase,
      "the next take's live rolling frame must be adopted after a DEC-136 Stop");
  const auto& live = f.runtime->pendingViewRequest_;
  TEST_ASSERT_FALSE_MESSAGE(live.active && live.sourceKind != PlaybackViewPendingRequest::SourceKind::LiveCapture,
      "the next take's live demand must not be blocked by a stale committed demand");
}
}

namespace {
/// DEC-136 cross-take, clear while the route's job is still live (the device ordering).
void privateRootReturnClearWhileJobLiveThenNextTakeGrows() {
  RollingRecordCoverageFixture f;
  f.advance(728); f.input(true, 65);
  f.advance(920); f.input(false, 65);
  f.advance(3352);
  f.track.stopRecording(3352);
  for (unsigned turn = 0; turn < 512; ++turn) {
    f.owner();
    if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
      (void)TrackCaptureStopTestAccess::step(f.track);
  }
  while (clockManager.getCurrentTick() < 768) { pulses(0, 1); f.owner(); }
  bool seen = false;
  for (unsigned pulse = 0; pulse < 400 && !seen; ++pulse) {
    pulses(0, 1);
    for (unsigned turn = 0; turn < 8 && !seen; ++turn) {
      f.owner();
      if (f.runtime->coverageBuildNeedsService() ||
          f.runtime->frozenViewRequest.reason == PlaybackViewPendingReason::PrivateRootReturnPreparation ||
          f.runtime->pendingViewRequest_.reason == PlaybackViewPendingReason::PrivateRootReturnPreparation) seen = true;
      else if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
        (void)TrackCaptureStopTestAccess::step(f.track);
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(seen, "route must be in flight");
  f.track.clear();
  for (unsigned i = 0; i < 256; ++i) {
    f.owner();
    if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
      (void)TrackCaptureStopTestAccess::step(f.track);
  }
  TEST_ASSERT_TRUE(f.track.startRecording(clockManager.getCurrentTick() / Config::TICKS_PER_CLOCK * Config::TICKS_PER_CLOCK));
  for (unsigned i = 0; i < 64; ++i) f.owner();
  f.input(true, 67);
  f.advance(clockManager.getCurrentTick() + 96);
  f.input(false, 67);
  f.advance(clockManager.getCurrentTick() + 960);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  const auto& live = f.runtime->pendingViewRequest_;
  TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::RollingPhase::Serving, f.runtime->rolling.phase,
      "next take's live frame must grow after clearing during the route");
}
}

namespace {
/// DEC-136 device evidence (attempt 5, empty leg): a position-free committed demand (`requestActive=1,
/// requestEpoch=0`) left over from the previous take must not block the next take's live Record demand.
void staleCommittedDemandDoesNotBlockNextTakeLiveFrame() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  push(0, midi::Start);
  poll();
  pulses(0, 1, 0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  auto& stale = runtime->pendingViewRequest_;
  stale = {};
  stale.active = true;
  stale.sourceKind = PlaybackViewPendingRequest::SourceKind::CommittedSnapshot;
  stale.reason = PlaybackViewPendingReason::PrivateRootReturnPreparation;
  stale.coverage = {768, Config::TICKS_PER_BAR, 1536};
  stale.loopLengthTicks = 1536;
  TEST_ASSERT_TRUE(track.startRecording(0));
  RollingRecordCoverageFixture f(0, false, true);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  f.advance(192); f.input(true, 67);
  f.advance(288); f.input(false, 67);
  f.advance(1248);
  for (unsigned i = 0; i < 64; ++i) f.owner();
  TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::RollingPhase::Serving, f.runtime->rolling.phase,
      "a stale committed demand must not prevent the next take's live rolling frame");
}
}

namespace {
/// DEC-136 — private committed-root preparation before Capture-ready adoption. The route is narrow by
/// construction, so its classification must not fire for an ordinary slot: no bound Record job means there is
/// nothing whose release depends on a root successor, and a job whose root is not yet current has no
/// root-backed demand to prepare.
void privateRootReturnDemandIsOwedOnlyForTheBoundFrozenReader() {
  RollingRecordCoverageFixture f;
  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::rootReturnSuccessorIsOwedForSlot(
          f.track, f.runtime->pendingViewRequest_, clockManager.getPlaybackDeadlineClock()),
      "a slot with no bound Record job owes no private root-return preparation");
  f.advance(728); f.input(true, 65);
  f.advance(920); f.input(false, 65);
  f.advance(3352);
  f.track.stopRecording(3352);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0));
  // The Stop is bound but the checkpoint root is still private, so no root-backed demand exists yet.
  TEST_ASSERT_FALSE(f.track.getLoop(0).publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::rootReturnSuccessorIsOwedForSlot(
          f.track, f.runtime->pendingViewRequest_, clockManager.getPlaybackDeadlineClock()),
      "an unpublished root cannot back the successor S7.5b.5 needs");
}
}

namespace {
/// DEC-136 — the waiver is the envelope's own impossibility, not a bar count. This is the pure rule: for the
/// demand's own derived cost, a two-bar loop admits no legal lead while a six-bar loop does.
void ordinaryAdmissionCannotServeShortLoopRootReturnWork() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  CoverageServiceContract held = TrackCaptureStopTestAccess::declaredCoverageContract(track);
  held.deriveSnapshotGrainsFrom(1, 1, Track::kPlaybackViewFillQuantum, Track::kPlaybackViewOrderQuantum);
  held.requiredFillGrains = 0; // qualification already filled the private candidate
  held.requiredPromotionGrains = 2;
  TEST_ASSERT_TRUE_MESSAGE(
      TrackCaptureStopTestAccess::ordinaryAdmissionCannotServeDemand(track, held, 2 * Config::TICKS_PER_BAR),
      "the held two-bar geometry admits no legal lead at the demand's own cost");
  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::ordinaryAdmissionCannotServeDemand(track, held, 6 * Config::TICKS_PER_BAR),
      "a longer loop can serve the same cost through the ordinary envelope");
  CoverageServiceContract undeclared;
  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::ordinaryAdmissionCannotServeDemand(track, undeclared, 2 * Config::TICKS_PER_BAR),
      "an undeclared contract proves nothing and must never grant the waiver");
}
}
namespace {
/// DEC-136 completion (S7.5b.5 release edge): the held mismatch Stop's job must not retire
/// while the root-return successor it waits on is owed. That successor is invisible to every
/// existing hold -- while it is prepared its build is coverage-owned (not a frozen-window
/// build), and once complete it waits unpublished in the ready role as a Committed-origin
/// window (not the Capture origin the unpublished-ready hold names) -- and the S7.5b.5
/// served-window terms are keyed on an audible slot. A retirement grain landing in that gap
/// (a transport stop inside the correction window) therefore released the job mid-owe and
/// discarded the successor the bound frozen reader still needs. The hold is bounded, so a
/// successor that can no longer be adopted still releases the job.
void rollingRecordHeldStopHoldsOwedRootSuccessorAcrossTransportStop() {
  RollingRecordCoverageFixture f;
  f.advance(728); f.input(true, 65);
  f.advance(920); f.input(false, 65);
  f.advance(3352);
  f.track.stopRecording(3352);
  TEST_ASSERT_EQUAL_UINT32(1536, f.track.getLoop(0).loopLengthTicks);
  TEST_ASSERT_TRUE(f.runtime->hasBoundRecordReturnWindowForPendingJob());
  for (unsigned turn = 0; turn < 512; ++turn) {
    f.owner();
    if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
      (void)TrackCaptureStopTestAccess::step(f.track);
  }
  // Publish the corrected root and drive the route to its complete-but-unadopted successor:
  // sitting unpublished in the ready role, one boundary from adoption, while the frozen
  // reader keeps serving.
  bool successorOwed = false;
  for (unsigned pulse = 0; pulse < 400 && !successorOwed; ++pulse) {
    pulses(0, 1);
    for (unsigned turn = 0; turn < 8 && !successorOwed; ++turn) {
      f.owner();
      if (!TrackCaptureStopTestAccess::stepRecordSnapshotPublication(f.track))
        (void)TrackCaptureStopTestAccess::step(f.track);
      const auto* set = f.runtime->viewSetIfPrewarmed();
      successorOwed = set->lifecycle.ready < set->views.size() &&
          set->views[set->lifecycle.ready].complete &&
          !set->views[set->lifecycle.ready].published &&
          !set->views[set->lifecycle.ready].rawCoveragePrepared &&
          set->views[set->lifecycle.ready].source.origin == PlaybackWindowOrigin::Committed &&
          set->views[set->lifecycle.ready].source.snapshotIdentity ==
              f.track.getLoop(0).publishedSnapshotIdentity() &&
          set->views[set->lifecycle.ready].coverage.loopLengthTicks == 1536;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(successorOwed,
      "the held mismatch Stop must reach its complete-but-unadopted root-return successor");
  const uint8_t owedReady = f.runtime->viewSetIfPrewarmed()->lifecycle.ready;
  const uint64_t owedStorage =
      f.runtime->viewSetIfPrewarmed()->views[owedReady].storageGeneration;
  // The transient: the transport stops inside the correction window. The S7.5b.5 holds are
  // keyed on an audible slot, so only the release edge stands between the job and a
  // retirement that discards the owed successor.
  f.track.stopPlaying();
  TEST_ASSERT_FALSE(f.track.isAudiblePlaybackActive());
  (void)TrackCaptureStopTestAccess::step(f.track);  // one retirement-eligible grain
  TEST_ASSERT_TRUE_MESSAGE(f.runtime->hasBoundRecordReturnWindowForPendingJob(),
      "a retirement grain in the transport-stop transient must not retire the bound job while its root successor is unadopted");
  {
    const auto* set = f.runtime->viewSetIfPrewarmed();
    TEST_ASSERT_TRUE(set->lifecycle.ready < set->views.size());
    const auto& ready = set->views[set->lifecycle.ready];
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(owedReady, set->lifecycle.ready,
        "the unadopted root-return successor must keep the ready role through the transient");
    TEST_ASSERT_TRUE_MESSAGE(ready.complete && ready.storageGeneration == owedStorage &&
        ready.source.origin == PlaybackWindowOrigin::Committed &&
        ready.source.snapshotIdentity == f.track.getLoop(0).publishedSnapshotIdentity(),
        "the complete root-return successor must survive the transient unadopted");
  }
  // The hold is bounded: with the transport stopped the successor can never be adopted, so
  // the job must still release instead of stranding its raw range, its capture-stop owner
  // and its ring slot forever.
  bool released = false;
  for (unsigned grain = 0; grain < 1200 && !released; ++grain) {
    (void)TrackCaptureStopTestAccess::step(f.track);
    released = !TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(f.track, 0);
  }
  TEST_ASSERT_TRUE_MESSAGE(released,
      "a root-return successor that can no longer be adopted must still release the job within the bounded stall budget");
}
}
