// Real gesture -> manager pending dispatcher -> Track admission, no retry FSM copy.
void test_queued_record_busy_preparation_preserves_preroll_and_retries_exactly_once() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlot(track,0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0,true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track,0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1,true));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track,1);
  TEST_ASSERT_NOT_NULL(runtime);
  Loop& target = track.getLoop(1);
  const auto content = target.captureContentGeneration();
  const auto revision = target.captureDisplayRevision;
  clockManager.assignCurrentTickSilently(120);
  TrackCaptureStopTestAccess::selectSlotIndex(tidx,1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,1));
  TrackCaptureStopTestAccess::seedPreRoll(track);
  {
    RuntimePlaybackViewBuild::PreparationTurn held(runtime->viewBuild);
    TEST_ASSERT_TRUE(static_cast<bool>(held));
    const uint32_t begin = clockManager.getCurrentTick();
    for (uint32_t tick = begin+1; tick <= begin+3*Config::TICKS_PER_BAR; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      TrackCaptureStopTestAccess::startPendingRecord(tick);
    }
    TEST_ASSERT_FALSE(track.isRecording());
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,1));
    TEST_ASSERT_EQUAL_UINT32(0,target.loopLengthTicks);
    TEST_ASSERT_EQUAL_UINT32(0,target.startLoopTick);
    TEST_ASSERT_EQUAL_UINT32(0,target.loopStartTick);
    TEST_ASSERT_EQUAL_UINT64(content,target.captureContentGeneration());
    TEST_ASSERT_EQUAL_UINT64(revision,target.captureDisplayRevision);
    TEST_ASSERT_EQUAL_UINT32(0,target.capture.store.size());
    TEST_ASSERT_EQUAL_UINT32(1,TrackCaptureStopTestAccess::preRollCount(track));
  }
  const uint32_t resume = clockManager.getCurrentTick();
  for (uint32_t tick = resume+1; tick <= resume+3*Config::TICKS_PER_BAR && !track.isRecording(); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_EQUAL_UINT8(1,track.getActiveLoopIndex());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,1));
  TEST_ASSERT_EQUAL_UINT32(0,TrackCaptureStopTestAccess::preRollCount(track));
  const uint32_t origin = TrackCaptureStopTestAccess::takeOriginTick(track);
  TEST_ASSERT_EQUAL_UINT32(1,target.capture.store.size()); // exactly one pre-roll On
  TEST_ASSERT_TRUE(target.capture.store.at(0).isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(65,target.capture.store.at(0).data.noteData.note);
  inputNote(72,origin+8,origin+32);
  TEST_ASSERT_EQUAL_UINT32(3,target.capture.store.size());
  const uint32_t after = clockManager.getCurrentTick();
  for (uint32_t tick = after+1; tick <= after+2*Config::TICKS_PER_BAR; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_EQUAL_UINT32(origin,TrackCaptureStopTestAccess::takeOriginTick(track));
  TEST_ASSERT_EQUAL_UINT32(3,target.capture.store.size());
  TEST_ASSERT_EQUAL_UINT8(72,target.capture.store.at(1).data.noteData.note);
  TEST_ASSERT_TRUE(target.capture.store.at(1).isNoteOn());
  TEST_ASSERT_TRUE(target.capture.store.at(2).isNoteOff());
}

void test_direct_record_preparation_refusal_preserves_geometry_and_preroll() {
  LoopEventStore::initPool();
  auto& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track,0);
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0,true));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track,0);
  auto& loop = track.getLoop(0);
  loop.loopLengthTicks = 3*Config::TICKS_PER_BAR;
  loop.startLoopTick = 123;
  loop.loopStartTick = 19;
  const auto content = loop.captureContentGeneration();
  TrackCaptureStopTestAccess::seedPreRoll(track);
  for (unsigned lane = 0; lane < 2; ++lane) {
    RuntimePlaybackViewBuild::PreparationTurn held(lane ? runtime->bridgeWindowBuild : runtime->viewBuild);
    TEST_ASSERT_TRUE(static_cast<bool>(held));
    TEST_ASSERT_FALSE(track.startRecording(256));
    TEST_ASSERT_FALSE(track.isRecording());
    TEST_ASSERT_EQUAL_UINT32(3*Config::TICKS_PER_BAR,loop.loopLengthTicks);
    TEST_ASSERT_EQUAL_UINT32(123,loop.startLoopTick);
    TEST_ASSERT_EQUAL_UINT32(19,loop.loopStartTick);
    TEST_ASSERT_EQUAL_UINT64(content,loop.captureContentGeneration());
    TEST_ASSERT_EQUAL_UINT32(1,TrackCaptureStopTestAccess::preRollCount(track));
  }
}

namespace AcquisitionRecordFixture {
inline bool attempted;
inline bool startedDuringReset;
void dispatchDuringReset() {
  PlaybackSourceMaskFixture::observeUnmasked = nullptr;
  attempted = true;
  TrackCaptureStopTestAccess::startPendingRecord(Config::TICKS_PER_BAR);
  startedDuringReset = trackManager.getSelectedTrack().isRecording();
}
}
void test_reset_transaction_excludes_clock_record_until_destructive_work_finishes() {
  using namespace AcquisitionRecordFixture;
  LoopEventStore::initPool();
  auto& track = trackManager.getSelectedTrack();
  const auto tidx = trackManager.getSelectedTrackIndex();
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track,0);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0,true));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track,0);
  clockManager.assignCurrentTickSilently(120);
  TrackCaptureStopTestAccess::toggleRecordForSlot(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,0));
  clockManager.assignCurrentTickSilently(Config::TICKS_PER_BAR);
  attempted = startedDuringReset = false;
  PlaybackSourceMaskFixture::observeUnmasked = dispatchDuringReset;
  TEST_ASSERT_TRUE(runtime->reset(false));
  TEST_ASSERT_TRUE(attempted);
  TEST_ASSERT_FALSE_MESSAGE(startedDuringReset,"reset must own admission throughout destructive work");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,0));
  TEST_ASSERT_FALSE(track.isRecording());
  clockManager.assignCurrentTickSilently(2*Config::TICKS_PER_BAR);
  TrackCaptureStopTestAccess::startPendingRecord(2*Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,0));
  auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.retained < set->views.size());
  TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].complete);
  TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].rawCoveragePrepared);
}

void test_slot_release_keeps_record_excluded_through_lease_retirement() {
  using namespace AcquisitionRecordFixture;
  LoopEventStore::initPool();
  auto& track = trackManager.getSelectedTrack();
  const auto tidx = trackManager.getSelectedTrackIndex();
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track,0);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0,true));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track,0);
  TEST_ASSERT_EQUAL(WorkingSetOutcome::Acquired,
      runtime->acquireWorkingSetLease(WorkingSetRole::PlaybackWindow));
  const auto lease = runtime->workingSetGeneration();
  clockManager.assignCurrentTickSilently(120);
  TrackCaptureStopTestAccess::toggleRecordForSlot(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,0));
  clockManager.assignCurrentTickSilently(Config::TICKS_PER_BAR);
  attempted = startedDuringReset = false;
  PlaybackSourceMaskFixture::observeUnmasked = dispatchDuringReset;
  TEST_ASSERT_EQUAL(WorkingSetOutcome::Released,
      TrackCaptureStopTestAccess::releaseSlot(track,0,lease,false));
  TEST_ASSERT_TRUE(attempted);
  TEST_ASSERT_FALSE(startedDuringReset);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,0));
  TEST_ASSERT_EQUAL(WorkingSetRole::None,runtime->workingSetRoles());
  TEST_ASSERT_NOT_EQUAL(lease,runtime->workingSetGeneration());
  clockManager.assignCurrentTickSilently(2*Config::TICKS_PER_BAR);
  TrackCaptureStopTestAccess::startPendingRecord(2*Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx,0));
  TEST_ASSERT_TRUE(runtime->holdsWorkingSetRole(WorkingSetRole::LiveCapture));
  auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.retained < set->views.size());
  TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].complete);
}
