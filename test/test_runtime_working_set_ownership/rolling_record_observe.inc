// Read-only qualification snapshots through the actual Track owner.
void test_rolling_record_observation_absent_and_empty_preserves_mask() {
  for (uint32_t incoming : {0u, 1u}) {
    PlaybackSourceMaskFixture::reset(incoming);
    const auto absent = Track::readRollingRecordStatus();
    TEST_ASSERT_FALSE(absent.valid);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, absent.currentRole);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, absent.readyRole);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, absent.buildRole);
    TEST_ASSERT_EQUAL_UINT8(0, absent.readyFlags | absent.buildFlags | absent.workerFlags);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, absent.firstTick);
    TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, absent.firstNoteId);
    TEST_ASSERT_NULL(TrackCaptureStopTestAccess::runtimeIfAllocated(trackManager.getSelectedTrack(), 0));
    TEST_ASSERT_EQUAL_UINT32(incoming, PlaybackSourceMaskFixture::primask);
    TEST_ASSERT_EQUAL_UINT32(0, PlaybackSourceMaskFixture::depth);
  }
  LoopEventStore::initPool();
  auto& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  nativePlaybackDeadlineClock = {0, 1};
  TEST_ASSERT_TRUE(track.startRecording(0));
  for (unsigned i = 0; i < 32; ++i)
    (void)track.stepPlaybackViewMaintenance(0, nullptr, true, nativePlaybackDeadlineClock);
  trackManager.updateAllTracks(0);
  for (uint32_t incoming : {0u, 1u}) {
    PlaybackSourceMaskFixture::reset(incoming);
    const auto empty = Track::readRollingRecordStatus();
    TEST_ASSERT_TRUE(empty.valid && empty.complete && empty.published && empty.recording);
    TEST_ASSERT_EQUAL_UINT32(0, empty.rows);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, empty.firstTick);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, empty.lastTick);
    TEST_ASSERT_EQUAL_UINT32(0, empty.ledgerCount);
    TEST_ASSERT_EQUAL_UINT32(incoming, PlaybackSourceMaskFixture::primask);
  }
  PlaybackSourceMaskFixture::reset();
}

void test_rolling_record_observation_growth_and_current_is_nonmutating() {
  LoopEventStore::initPool();
  auto& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  nativePlaybackDeadlineClock = {0, 1};
  TEST_ASSERT_TRUE(track.startRecording(0));
  auto advance = [&](uint32_t tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
    trackManager.updateAllTracks(tick);
    for (unsigned i = 0; i < 32; ++i)
      (void)track.stepPlaybackViewMaintenance(0, nullptr, true, nativePlaybackDeadlineClock);
  };
  advance(0);
  advance(8); track.noteOn(0, 60, 100, 8);
  advance(16); track.noteOff(0, 60, 0, 16);
  advance(24);
  advance(768);
  advance(832); track.noteOn(0, 62, 100, 832);
  advance(864); track.noteOff(0, 62, 0, 864);
  advance(872);
  advance(1536);
  advance(1544); // source On8 is now held in the shared ledger.
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  auto* set = runtime->viewSetIfPrewarmed();
  const auto role = set->lifecycle.current;
  const auto storage = set->views[role].storageGeneration;
  const auto source = set->views[role].source;
  const auto cursor = track.getLoop(0).nextEventIndex;
  const auto rawToken = track.getLoop(0).captureContentGeneration();
  const auto rawRows = track.getLoop(0).capture.store.size();
  const auto ledger = runtime->ledger;
  const auto pending = runtime->pendingViewRequest_;
  const auto frozen = runtime->frozenViewRequest;
  const auto frame = runtime->rolling;
  for (uint32_t incoming : {0u, 1u}) {
    PlaybackSourceMaskFixture::reset(incoming);
    const auto s = Track::readRollingRecordStatus();
    TEST_ASSERT_EQUAL_UINT32(incoming, PlaybackSourceMaskFixture::primask);
    TEST_ASSERT_EQUAL_UINT32(0, PlaybackSourceMaskFixture::depth);
    TEST_ASSERT_TRUE(s.valid && s.complete && s.published && s.recording);
    TEST_ASSERT_EQUAL_UINT8(TRACK_RECORDING, s.state);
    TEST_ASSERT_EQUAL_UINT32(1536, s.framePeriod);
    TEST_ASSERT_EQUAL_INT32(1536, s.frameOrigin);
    TEST_ASSERT_EQUAL_UINT64(1544, s.clockTick);
    TEST_ASSERT_EQUAL_UINT32(1, s.epoch);
    TEST_ASSERT_EQUAL_UINT32(2, s.rows);
    TEST_ASSERT_EQUAL_UINT32(8, s.firstTick);
    TEST_ASSERT_EQUAL_UINT32(16, s.lastTick);
    TEST_ASSERT_EQUAL_UINT32(track.getLoop(0).capture.store.at(0).noteId, s.firstNoteId);
    TEST_ASSERT_EQUAL_UINT32(s.firstNoteId, s.lastNoteId);
    TEST_ASSERT_EQUAL_UINT32(1, s.ledgerCount);
    TEST_ASSERT_EQUAL_UINT32(4, s.rawCount);
    TEST_ASSERT_EQUAL_UINT32(864, s.rawMax);
    TEST_ASSERT_FALSE(s.stopResponsibility);
    TEST_ASSERT_EQUAL_UINT8(set->lifecycle.ready, s.readyRole);
    TEST_ASSERT_EQUAL_UINT8(set->lifecycle.build, s.buildRole);
    if (s.readyRole < set->views.size()) {
      const auto& ready = set->views[s.readyRole];
      TEST_ASSERT_EQUAL(ready.complete, bool(s.readyFlags & 1));
      TEST_ASSERT_EQUAL(ready.published, bool(s.readyFlags & 2));
      TEST_ASSERT_EQUAL(ready.rawCoveragePrepared, bool(s.readyFlags & 4));
      TEST_ASSERT_EQUAL(ready.source == pending.source, bool(s.readyFlags & 8));
    }
    TEST_ASSERT_EQUAL(runtime->viewBuild.active(), bool(s.workerFlags & 1));
    TEST_ASSERT_EQUAL(runtime->viewBuild.ownsLiveCaptureBorrow(), bool(s.workerFlags & 2));
    TEST_ASSERT_EQUAL_UINT8(role, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(storage, set->views[role].storageGeneration);
    TEST_ASSERT_TRUE(set->views[role].source == source);
    TEST_ASSERT_EQUAL_UINT32(cursor, track.getLoop(0).nextEventIndex);
    TEST_ASSERT_EQUAL_UINT64(rawToken, track.getLoop(0).captureContentGeneration());
    TEST_ASSERT_EQUAL_UINT32(rawRows, track.getLoop(0).capture.store.size());
    TEST_ASSERT_EQUAL_MEMORY(&ledger, &runtime->ledger, sizeof ledger);
    TEST_ASSERT_EQUAL_MEMORY(&pending, &runtime->pendingViewRequest_, sizeof pending);
    TEST_ASSERT_EQUAL_MEMORY(&frozen, &runtime->frozenViewRequest, sizeof frozen);
    TEST_ASSERT_EQUAL_MEMORY(&frame, &runtime->rolling, sizeof frame);
  }
  PlaybackSourceMaskFixture::reset();
}

void test_rolling_record_observation_playing_stop_responsibility() {
  LoopEventStore::initPool();
  auto& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  track.forceSetState(TRACK_STOPPED);
  nativePlaybackDeadlineClock = {0, 1};
  TEST_ASSERT_TRUE(track.startRecording(0));
  track.noteOn(0, 60, 100, 12);
  track.noteOff(0, 60, 0, 240);
  for (unsigned i = 0; i < 32; ++i)
    (void)track.stepPlaybackViewMaintenance(0, nullptr, true, nativePlaybackDeadlineClock);
  clockManager.assignCurrentTickSilently(768);
  nativePlaybackDeadlineClock = {768, 1};
  track.stopRecording(768);
  const auto before = Track::readRollingRecordStatus();
  TEST_ASSERT_TRUE(before.stopResponsibility);
  TEST_ASSERT_FALSE(before.recording);
  TEST_ASSERT_EQUAL_UINT8(TRACK_PLAYING, before.state);
  for (uint32_t incoming : {0u, 1u}) {
    PlaybackSourceMaskFixture::reset(incoming);
    const auto after = Track::readRollingRecordStatus();
    TEST_ASSERT_EQUAL_UINT32(incoming, PlaybackSourceMaskFixture::primask);
    TEST_ASSERT_EQUAL_UINT8(before.currentRole, after.currentRole);
    TEST_ASSERT_EQUAL_UINT64(before.currentStorage, after.currentStorage);
    TEST_ASSERT_EQUAL_UINT64(before.captureContentToken, after.captureContentToken);
    TEST_ASSERT_EQUAL_UINT32(before.cursor, after.cursor);
    TEST_ASSERT_EQUAL_UINT32(before.ledgerCount, after.ledgerCount);
    TEST_ASSERT_TRUE(after.stopResponsibility);
    TEST_ASSERT_TRUE(track.getLoop(0).hasSealedCaptureBorrow());
  }
  PlaybackSourceMaskFixture::reset();
}
