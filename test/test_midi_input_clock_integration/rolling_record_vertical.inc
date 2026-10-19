// Functional production-order oracle, not a main-loop cadence or WCET proof.
// Real input and ClockManager advance time; only hardware inboxes are fakes.
namespace {
void rollingRecordVertical(uint8_t route, bool matchingStop = false) {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  push(route, midi::Start);
  poll();
  pulses(route, 1, 0);
  TEST_ASSERT_TRUE(track.startRecording(0));
  TEST_ASSERT_TRUE(track.isRecording());
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  const auto epoch = clockManager.getPlaybackDeadlineClock().epoch;
  TEST_ASSERT_NOT_EQUAL_UINT32(0, epoch);
  MidiServiceDrain::PlaybackHolderCursor cursor{};
  auto ownerTurn = [&]() {
    const auto now = clockManager.getPlaybackDeadlineClock();
    TEST_ASSERT_EQUAL_UINT32(epoch, now.epoch);
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
      auto& candidate = trackManager.getTrack(i);
      (void)candidate.serviceRollingPlaybackWindow(now);
      candidate.serviceAdmittedDeadlineMisses(now);
    }
    MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
      offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(0, now);
    uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
    if (MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers,
        Config::NUM_TRACKS, chosen) == MidiServiceDrain::PlaybackHolderSelection::Service)
      (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(0, nullptr, true, now);
  };
  // Cold startup has no late-content deadline. Every grant uses the real era.
  for (unsigned i = 0; i < 64; ++i) ownerTurn();
  const auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const uint8_t initialRole = set->lifecycle.current;
  TEST_ASSERT_TRUE(initialRole < set->views.size());
  const auto& initial = set->views[initialRole];
  TEST_ASSERT_TRUE(initial.complete && initial.published && !initial.rawCoveragePrepared);
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, initial.source.origin);
  const auto initialStorage = initial.storageGeneration;
  const auto initialToken = initial.source.captureContentGeneration;
  auto advanceTo = [&](uint32_t tick) {
    while (clockManager.getCurrentTick() < tick) {
      pulses(route, 1);
      // Controlled service opportunities between physical Clock packets. This
      // is deliberately not a claim about target input-return or owner WCET.
      for (unsigned i = 0; i < Config::TICKS_PER_CLOCK; ++i) ownerTurn();
    }
    TEST_ASSERT_EQUAL_UINT32(tick, clockManager.getCurrentTick());
  };
  advanceTo(704);
  push(route, midi::NoteOn, 60, 100);
  poll();
  for (unsigned i = 0; i < 8; ++i) ownerTurn();
  advanceTo(736);
  push(route, midi::NoteOff, 60, 0);
  poll();
  assertPair(track.getLoop(0), 704, 736, 60);
  const auto expectedNoteId = track.getLoop(0).capture.store.at(0).noteId;
  for (unsigned i = 0; i < 8; ++i) ownerTurn();
  TEST_ASSERT_EQUAL_UINT8(initialRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(initialStorage, initial.storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(initialToken, initial.source.captureContentGeneration);
  TEST_ASSERT_EQUAL_UINT32(0, initial.storage.eventCount);
  advanceTo(760);
  TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.ready < set->views.size(),
      "real nonzero-epoch Record must prepare a rolling successor before its edge");
  const auto& successor = set->views[set->lifecycle.ready];
  char progress[360];
  snprintf(progress, sizeof(progress), "ready=%u build=%u active=%u grain=%u rows=%u token=%llu wanted=%llu frontier=%zu extent=%zu ordered=%zu req=%u deadline=%llu recording=%u phase=%u committed=%u snap=%u frame=%u kind=%u", set->lifecycle.ready, set->lifecycle.build, runtime->viewBuild.active(), unsigned(runtime->viewBuild.nextGrain()), unsigned(successor.storage.eventCount), (unsigned long long)successor.source.captureContentGeneration, (unsigned long long)track.getLoop(0).captureContentGeneration(), runtime->viewBuild.liveCaptureSourceFrontier(), runtime->viewBuild.liveCaptureSourceExtent(), runtime->viewBuild.privateOrderedEventCount(), runtime->pendingViewRequest_.active, (unsigned long long)runtime->pendingViewRequest_.deadlineTick, track.isRecording(), unsigned(track.getLoop(0).capture.phase), track.getLoop(0).hasCommittedPasses(), track.getLoop(0).publishedSnapshotIsCurrent(), unsigned(runtime->rolling.phase), unsigned(runtime->pendingViewRequest_.sourceKind));
  if (runtime->pendingViewRequest_.positionInvalidated)
    snprintf(progress, sizeof(progress), "live recipe invalidated: frozenLength=%u actualLength=%u origin=%d phaseGeneration=%llu", unsigned(runtime->pendingViewRequest_.liveCaptureProjection.loopLengthTicks), unsigned(track.getLoop(0).loopLengthTicks), runtime->pendingViewRequest_.liveProjectionStart, (unsigned long long)runtime->pendingViewRequest_.liveCaptureProjection.phaseGeneration);
  TEST_ASSERT_TRUE_MESSAGE(successor.complete, progress);
  TEST_ASSERT_EQUAL_UINT32(2, successor.storage.eventCount);
  TEST_ASSERT_EQUAL_UINT64(track.getLoop(0).captureContentGeneration(),
      successor.source.captureContentGeneration);
  MidiSendCapture::reset(); // Ignore input monitoring, not scheduled playback.
  advanceTo(768);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
      static_cast<int>(runtime->rolling.phase));
  if (matchingStop) {
    const uint8_t role = set->lifecycle.current;
    const uint64_t storage = set->views[role].storageGeneration;
    const uint64_t token = set->views[role].source.captureContentGeneration;
    const auto cursorBefore = track.getLoop(0).nextEventIndex;
    const auto frameTick = runtime->rolling.lastTick;
    track.stopRecording(768);
    TEST_ASSERT_TRUE(track.isPlaying());
    TEST_ASSERT_EQUAL_UINT32(768, track.getLoop(0).loopLengthTicks);
    TEST_ASSERT_TRUE(track.getLoop(0).hasSealedCaptureBorrow());
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0));
    TEST_ASSERT_EQUAL_UINT8(role, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(storage, set->views[role].storageGeneration);
    TEST_ASSERT_EQUAL_UINT64(token, set->views[role].source.captureContentGeneration);
    TEST_ASSERT_EQUAL_UINT64(storage, runtime->rolling.boundStorageGeneration);
    TEST_ASSERT_EQUAL_UINT64(frameTick, runtime->rolling.lastTick);
    TEST_ASSERT_EQUAL_UINT32(cursorBefore, track.getLoop(0).nextEventIndex);
    TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  }
  TEST_ASSERT_EQUAL_UINT64(successor.storageGeneration, runtime->rolling.boundStorageGeneration);
  advanceTo(1472);
  snprintf(progress, sizeof(progress), "live output frame=%u last=%llu origin=%d recipeOrigin=%d index=%u lastPhase=%u len=%u current=%u bound=%u rows=%u invalid=%u", unsigned(runtime->rolling.phase), (unsigned long long)runtime->rolling.lastTick, track.getProjectionCycleStartTick(), runtime->pendingViewRequest_.liveProjectionStart, unsigned(track.getLoop(0).nextEventIndex), unsigned(track.getLoop(0).lastTickInLoop), unsigned(track.getLoop(0).loopLengthTicks), set->lifecycle.current, runtime->rolling.boundView, unsigned(set->views[set->lifecycle.current].storage.eventCount), runtime->pendingViewRequest_.positionInvalidated);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, MidiSendCapture::count(), progress);
  const auto on = MidiSendCapture::at(0);
  TEST_ASSERT_TRUE(on.isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(704, on.tick);
  TEST_ASSERT_EQUAL_UINT8(1, on.channel);
  TEST_ASSERT_EQUAL_UINT8(60, on.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(100, on.data.noteData.velocity);
  TEST_ASSERT_EQUAL_UINT32(expectedNoteId, on.noteId);
  TEST_ASSERT_NOT_EQUAL_UINT32(kInvalidNoteId, on.noteId);
  unsigned sounding = 0;
  runtime->ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
  TEST_ASSERT_EQUAL_UINT32(1, sounding);
  trackManager.updateAllTracks(1472);
  TEST_ASSERT_EQUAL_UINT32(1, MidiSendCapture::count());
  advanceTo(1504);
  TEST_ASSERT_EQUAL_UINT32(2, MidiSendCapture::count());
  const auto off = MidiSendCapture::at(1);
  TEST_ASSERT_TRUE(off.isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(736, off.tick);
  TEST_ASSERT_EQUAL_UINT8(1, off.channel);
  TEST_ASSERT_EQUAL_UINT8(60, off.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(0, off.data.noteData.velocity);
  TEST_ASSERT_EQUAL_UINT32(on.noteId, off.noteId);
  sounding = 0;
  runtime->ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
  TEST_ASSERT_EQUAL_UINT32(0, sounding);
  trackManager.updateAllTracks(1504);
  TEST_ASSERT_EQUAL_UINT32(2, MidiSendCapture::count());
  TEST_ASSERT_EQUAL(!matchingStop, track.isRecording());
  TEST_ASSERT_EQUAL(matchingStop, track.isPlaying());
}
void usbRollingRecordVertical() { rollingRecordVertical(0); }
void dinRollingRecordVertical() { rollingRecordVertical(1); }
void hostRollingRecordVertical() { rollingRecordVertical(2); }
void usbRollingRecordMatchingStop() { rollingRecordVertical(0, true); }
}

namespace {
void rollingRecordEpochChangeCancelsPrivateBorrow() {
  auto& track = beginRunningRecord(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  const auto stamp = clockManager.getPlaybackDeadlineClock();
  for (unsigned i = 0; i < 64; ++i)
    (void)track.stepPlaybackViewMaintenance(0, nullptr, true, stamp);
  pulses(0, 1);
  const auto* set = runtime->viewSetIfPrewarmed();
  const uint8_t role = set->lifecycle.current;
  const uint64_t storage = set->views[role].storageGeneration;
  const uint64_t token = set->views[role].source.captureContentGeneration;
  push(0, midi::NoteOn, 63, 100);
  poll();
  for (unsigned i = 0; i < 8 && !runtime->viewBuild.ownsLiveCaptureBorrow(); ++i)
    (void)track.stepPlaybackViewMaintenance(0, nullptr, true, clockManager.getPlaybackDeadlineClock());
  TEST_ASSERT_TRUE(runtime->viewBuild.ownsLiveCaptureBorrow());
  const auto owed = runtime->pendingViewRequest_.deadlineTick;
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick());
  const auto rebased = clockManager.getPlaybackDeadlineClock();
  TEST_ASSERT_NOT_EQUAL_UINT32(stamp.epoch, rebased.epoch);
  TEST_ASSERT_EQUAL(Track::RollingOutcome::Rejected, track.serviceRollingPlaybackWindow(rebased));
  for (unsigned i = 0; i < 8; ++i)
    (void)track.stepPlaybackViewMaintenance(0, nullptr, true, rebased);
  TEST_ASSERT_FALSE(runtime->viewBuild.ownsLiveCaptureBorrow());
  TEST_ASSERT_FALSE(runtime->viewBuild.active());
  TEST_ASSERT_TRUE(runtime->pendingViewRequest_.positionInvalidated);
  TEST_ASSERT_EQUAL_UINT64(owed, runtime->pendingViewRequest_.deadlineTick);
  TEST_ASSERT_EQUAL_UINT8(role, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(storage, set->views[role].storageGeneration);
  TEST_ASSERT_EQUAL_UINT64(token, set->views[role].source.captureContentGeneration);
  TEST_ASSERT_TRUE(set->views[role].published && set->views[role].complete);
  TEST_ASSERT_EQUAL_UINT32(1, track.getLoop(0).capture.store.size());
}
}
