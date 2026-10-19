// Actual USB-device input -> ClockManager -> queued Record retry -> accepted Capture.
void usbQueuedRecordPreparationRetriesAtRealClockBoundary() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::activateSlot(0,0);
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0,true));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track,0);
  TEST_ASSERT_NOT_NULL(runtime);
  TrackCaptureStopTestAccess::toggleRecordForSlot(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(0,0));
  const auto content = track.getLoop(0).captureContentGeneration();
  {
    RuntimePlaybackViewBuild::PreparationTurn held(runtime->viewBuild);
    TEST_ASSERT_TRUE(static_cast<bool>(held));
    push(0,midi::Start); poll();
    pulses(0,1,0);
    pulses(0,96,Config::TICKS_PER_BAR);
    TEST_ASSERT_FALSE(track.isRecording());
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::recordQueuedForSlot(0,0));
    TEST_ASSERT_EQUAL_UINT64(content,track.getLoop(0).captureContentGeneration());
    TEST_ASSERT_EQUAL_UINT32(0,track.getLoop(0).capture.store.size());
  }
  pulses(0,96,2*Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::recordQueuedForSlot(0,0));
  const auto origin = TrackCaptureStopTestAccess::takeOriginTick(track);
  TEST_ASSERT_EQUAL_UINT32(2*Config::TICKS_PER_BAR,origin);
  push(0,midi::NoteOn,72,100); poll();
  pulses(0,1);
  push(0,midi::NoteOff,72,0); poll();
  assertPair(track.getLoop(0),0,Config::TICKS_PER_CLOCK,72);
  pulses(0,96);
  TEST_ASSERT_EQUAL_UINT32(origin,TrackCaptureStopTestAccess::takeOriginTick(track));
  assertPair(track.getLoop(0),0,Config::TICKS_PER_CLOCK,72);
  TEST_ASSERT_TRUE(track.isRecording());
}
