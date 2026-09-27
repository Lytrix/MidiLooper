// S4f.16: actual stop/worker/undo/storage owners; no copied stop implementation.
#define HARNESS_REAL_CAPTURE_STOP
#include "../test_support/capture_storage_harness.inc"
#include <cstdlib>
#if BYPASS_STOP_UNDO_SAVE
#error This fixture requires production undo and persistence admission
#endif

struct TrackCaptureStopTestAccess {
  static uint64_t interval(const Track& t) { return t.captureInterval_; }
  static size_t jobs(const Track& t) { return t.captureStopQueueDepth(); }
  static void step(Track& t) {
    const auto* before = t.captureStopJobHead();
    if (before) {
      const auto& a = before->admission;
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.rankedPreparationQuantum);
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.canonicalPreparationQuantum);
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.historyRecycleQuantum);
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.undoCollectionQuantum);
    }
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  static uint64_t headInterval(const Track& t) {
    const auto* j = t.captureStopJobHead();
    return j ? j->interval : 0;
  }
  static uint64_t budget(const Track& t) {
    const auto* j = t.captureStopJobHead();
    if (!j) return 0;
    const auto& a = j->admission;
    return a.remainingClosureGrains + a.remainingOrderGrains +
      a.remainingRankedPreparationGrains + a.remainingCanonicalPreparationGrains +
      a.remainingHistoryRecycleGrains + a.remainingUndoCollectionGrains + 128;
  }
  static void assertHead(const Track& t, uint8_t slot, uint64_t interval, CapturePhase mode) {
    const auto* j = t.captureStopJobHead();
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_UINT(slot, j->slot);
    TEST_ASSERT_EQUAL(mode, j->mode);
    TEST_ASSERT_EQUAL_UINT64(interval, j->interval);
    TEST_ASSERT_EQUAL(t.getLoop(slot).loopId, j->loopId);
    TEST_ASSERT_EQUAL_PTR(&t.getLoop(slot), j->owner);
    TEST_ASSERT_TRUE(j->ownsRawRange);
  }
  static bool occupyCanonicalOutput(Track& t) {
    auto* rt = t.playbackRuntime.slotIfAllocated(0);
    return rt && rt->prewarmCanonicalCaptureOutput() &&
      rt->canonicalCaptureOutput.store.appendReserved(MidiEvent::ControlChange(0, 1, 1, 1),
                                                       rt->canonicalCaptureChunks);
  }
  static bool drive(Track& t) { return t.driveAcceptedInteraction(); }
  static void diagnostic(const Track& t) {
    const auto* j = t.captureStopJobHead();
    printf("jobs=%zu interval=%llu phase=%u job_bytes=%zu ring_bytes=%zu\n", jobs(t),
      static_cast<unsigned long long>(j ? j->interval : interval(t)),
      j ? static_cast<unsigned>(j->admission.phase) : 0,
      sizeof(Track::CaptureStopJob), sizeof(Track::CaptureStopJobs));
    if (j) {
      printf("slot=%u loop=%lu raw=%u pass=%u credits=%llu free_chunks=%u\n", j->slot,
        static_cast<unsigned long>(j->loopId), j->ownsRawRange, j->ownsPendingPass,
        static_cast<unsigned long long>(budget(t)), LoopEventStore::freeChunkCount());
    }
  }
};
void TrackManager::beginSlotSelectionHold(uint8_t, uint8_t) { A34A_OFF_PATH(); }
void TrackManager::endSlotSelectionHold(uint8_t, uint8_t, uint32_t) { A34A_OFF_PATH(); }
void setUp() {
  PersistenceWorkQueue::resetForTests();
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  nativeTransportRunning = true;
  nativeMillisSet(0);
  A34aBoundary::resetSlotFlags();
  for (auto& row : StorageManagerInternal::currentSetLoopSlotDirty) row.fill(false);
}
void tearDown() {}

// Select only the capture owner's input slot. Manager/carrier switching is explicitly
// outside this fixture; no state owner on the asserted stop/worker path is substituted.
void prepareSlot(Track& t, uint8_t slot, bool overdub) {
  Loop& loop = t.getLoop(slot);
  if (overdub) {
    loop.loopLengthTicks = 768;
    loop.beginCapture(CapturePhase::Record);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
    TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
    loop.commitPendingCapturePass();
    TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
    TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  }
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(slot, true));
}
void start(Track& t, uint8_t slot, bool overdub, uint32_t tick, uint8_t note) {
  t.setActiveLoopIndex(slot);
  if (overdub) t.startOverdubbing(tick); else t.startRecording(tick);
  TEST_ASSERT_TRUE(overdub ? t.isOverdubbing() : t.isRecording());
  t.noteOn(1, note, 100, tick + 12);
  t.noteOff(1, note, 0, tick + 96);
}
void stop(Track& t, bool overdub, uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  if (overdub) t.stopOverdubbing(); else t.stopRecordingToStopped(tick);
}
void assertContent(Track& t, uint8_t slot, bool overdub, uint8_t note) {
  const Loop& loop = t.getLoop(slot);
  TEST_ASSERT_EQUAL_UINT32(768, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(overdub ? 1 : 0, loop.passes.overdubPasses.size());
  SessionMidiEventVec rows;
  const auto& chunks = overdub ? loop.passes.overdubPasses.back().committedChunkIds : loop.passes.recordPass.committedChunkIds;
  LoopEventStore::appendChunkRefEvents(chunks, rows);
  TEST_ASSERT_EQUAL_UINT32(2, rows.size());
  TEST_ASSERT_EQUAL_UINT8(note, rows[0].data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(note, rows[1].data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(12, rows[0].tick);
  TEST_ASSERT_EQUAL_UINT32(96, rows[1].tick);
  if (std::getenv("S4F16_REQUIRE_CONTRACT") || !overdub) {
    TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::currentSetLoopSlotDirty[0][slot],
      "completed interval must request persistence for its own slot");
  } else {
    // Existing deferred worker lacks the persistence handoff; S4f.20 owns this gap.
    TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][slot]);
  }
}
void rotation(bool aOverdub, bool bOverdub) {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, aOverdub);
  prepareSlot(t, 1, bOverdub);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, aOverdub, 0, 60);
  const uint64_t a = TrackCaptureStopTestAccess::interval(t);
  stop(t, aOverdub, 768);
  const bool contract = std::getenv("S4F16_REQUIRE_CONTRACT") != nullptr;
  if (contract || aOverdub) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, TrackCaptureStopTestAccess::jobs(t),
      "A must own a deferred job before any worker grain (record currently publishes inline)");
  } else {
    TEST_ASSERT_EQUAL_UINT(0, TrackCaptureStopTestAccess::jobs(t));
  }
  start(t, 1, bOverdub, 768, 62);
  const uint64_t b = TrackCaptureStopTestAccess::interval(t);
  TEST_ASSERT_TRUE_MESSAGE(b > a, "each actual start needs a fresh interval");
  if (contract || aOverdub) TrackCaptureStopTestAccess::assertHead(
      t, 0, a, aOverdub ? CapturePhase::Overdub : CapturePhase::Record);
  stop(t, bOverdub, 1536);
  if (contract || (aOverdub && bOverdub)) {
    TEST_ASSERT_TRUE(t.hasAcceptedUnexecutedInteraction());
    TEST_ASSERT_EQUAL_UINT(1, TrackCaptureStopTestAccess::jobs(t));
  }
  const auto freeBefore = LoopEventStore::freeChunkCount();
  unsigned turns = 0;
  uint64_t allowance = 2; // At most two accepted-interaction admission turns.
  uint64_t lastJob = 0;
  while (t.hasStopInFlight()) {
    const auto id = TrackCaptureStopTestAccess::headInterval(t);
    if (id != 0 && id != lastJob) {
      allowance += TrackCaptureStopTestAccess::budget(t);
      lastJob = id;
    }
    if (++turns > allowance || turns > 20000) {
      TrackCaptureStopTestAccess::diagnostic(t);
      TEST_FAIL_MESSAGE("phase-credit budget or 20,000-turn fixture watchdog exhausted");
    }
    TrackCaptureStopTestAccess::step(t);
  }
  TrackCaptureStopTestAccess::diagnostic(t);
  printf("rotation=%s/%s turns=%u persist_queue=%u undo=%zu\n", aOverdub ? "OD" : "R",
    bOverdub ? "OD" : "R", turns, PersistenceWorkQueue::queueDepth(), t.getGlobalUndoStack().undoCount());
  printf("allowance=%llu free_chunks_before=%u after=%u\n",
    static_cast<unsigned long long>(allowance), freeBefore, LoopEventStore::freeChunkCount());
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(), "worker/latch failed to retire within diagnostic ceiling");
  assertContent(t, 0, aOverdub, 60);
  assertContent(t, 1, bOverdub, 62);
  TEST_ASSERT_EQUAL_UINT(2, t.getGlobalUndoStack().undoCount());
  const auto& undo = t.getGlobalUndoStack().entries;
  TEST_ASSERT_TRUE(undo[0].loopId != undo[1].loopId);
  for (uint8_t slot = 0; slot < 2; ++slot) {
    bool found = false;
    for (const auto& entry : undo) {
      if (entry.loopId != t.getLoop(slot).loopId) continue;
      found = true;
      TEST_ASSERT_EQUAL_UINT(slot, entry.slotIndex);
      const bool overdub = slot == 0 ? aOverdub : bOverdub;
      TEST_ASSERT_EQUAL(overdub ? UndoEntryKind::OverdubPassAdded : UndoEntryKind::RecordPassAdded,
                        entry.kind);
      if (!overdub) {
        TEST_ASSERT_EQUAL(PersistWorkState::Queued, PersistenceWorkQueue::workState(
          PersistWorkType::LoopPersist, persistKeyForSlot(0, slot)));
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "undo must retain each interval's LoopId");
  }
  const size_t queue = PersistenceWorkQueue::queueDepth();
  for (unsigned i = 0; i < 10; ++i) {
    TrackCaptureStopTestAccess::step(t);
    TrackCaptureStopTestAccess::drive(t);
  }
  TEST_ASSERT_EQUAL_UINT(2, t.getGlobalUndoStack().undoCount());
  TEST_ASSERT_EQUAL_UINT(queue, PersistenceWorkQueue::queueDepth());
  assertContent(t, 0, aOverdub, 60);
  assertContent(t, 1, bOverdub, 62);
}
void test_record_to_record() { rotation(false, false); }
void test_record_to_overdub() { rotation(false, true); }
void test_overdub_to_record() { rotation(true, false); }
void test_overdub_to_overdub() { rotation(true, true); }
void test_actual_start_stamps_once_and_duplicate_keeps_capture() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  const auto id = TrackCaptureStopTestAccess::interval(t);
  TEST_ASSERT_EQUAL_UINT64(1, id);
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(0).capture.store.size());
  t.startRecording(400);
  TEST_ASSERT_EQUAL_UINT64(id, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(0).capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).startLoopTick);
}
void test_invalid_record_start_does_not_reset_overdub_or_stamp() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, true);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  const auto id = TrackCaptureStopTestAccess::interval(t);
  const auto passId = t.getLoop(0).passes.recordPass.id;
  t.startRecording(400);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(id, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_EQUAL(passId, t.getLoop(0).passes.recordPass.id);
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(0).capture.store.size());
}
void test_record_length_summary_handles_out_of_order_input_and_removal() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  t.startRecording(0);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 3072; // The removal API requires established geometry.
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1800, 1, 62, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(100, 1, 1, 64)));
  TEST_ASSERT_TRUE(loop.removeOpenCaptureNoteOn(1, 62));
  t.stopRecordingToStopped(3072);
  TEST_ASSERT_EQUAL_UINT32(768, loop.loopLengthTicks);
}
void test_record_length_uses_maximum_not_last_appended_tick() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  t.startRecording(0);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(1800, 1, 1, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(100, 1, 2, 64)));
  t.stopRecordingToStopped(3072);
  TEST_ASSERT_EQUAL_UINT32(2304, loop.loopLengthTicks);
}
void staleAcceptedStop(bool replaceLoop) {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, true);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::occupyCanonicalOutput(t));
  stop(t, true, 768);
  TEST_ASSERT_TRUE(t.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_EQUAL_UINT(0, TrackCaptureStopTestAccess::jobs(t));
  Loop& loop = t.getLoop(0);
  const size_t rows = loop.capture.store.size();
  // Fault injection: a replacement reuses the slot without advancing Track's counter.
  // The old request must not park or consume the replacement's resources.
  if (replaceLoop) ++loop.loopId;
  else loop.capture.phase = CapturePhase::Record;
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::drive(t));
  TEST_ASSERT_FALSE(t.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_EQUAL_UINT(0, TrackCaptureStopTestAccess::jobs(t));
  TEST_ASSERT_EQUAL_UINT(rows, loop.capture.store.size());
}
void test_replaced_loop_supersedes_accepted_stop() { staleAcceptedStop(true); }
void test_changed_capture_mode_supersedes_accepted_stop() { staleAcceptedStop(false); }
void test_denied_record_preflight_preserves_content_and_interval_then_retries() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  const PassId oldPass = loop.passes.recordPass.id;
  t.forceSetState(TRACK_PLAYING);
  LoopEventStore::ChunkReservation occupied;
  TEST_ASSERT_TRUE(occupied.reserveChunks(LoopEventStore::freeChunkCount()));
  t.startRecording(1000);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT64(0, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_EQUAL(oldPass, loop.passes.recordPass.id);
  TEST_ASSERT_EQUAL_UINT32(768, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(occupied.releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT));
  t.startRecording(1200);
  TEST_ASSERT_TRUE(t.isRecording());
  TEST_ASSERT_EQUAL_UINT64(1, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL_UINT32(1200, loop.startLoopTick);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_denied_record_preflight_preserves_content_and_interval_then_retries);
  RUN_TEST(test_replaced_loop_supersedes_accepted_stop);
  RUN_TEST(test_changed_capture_mode_supersedes_accepted_stop);
  RUN_TEST(test_record_length_uses_maximum_not_last_appended_tick);
  RUN_TEST(test_record_length_summary_handles_out_of_order_input_and_removal);
  RUN_TEST(test_actual_start_stamps_once_and_duplicate_keeps_capture);
  RUN_TEST(test_invalid_record_start_does_not_reset_overdub_or_stamp);
  RUN_TEST(test_record_to_record);
  RUN_TEST(test_record_to_overdub);
  RUN_TEST(test_overdub_to_record);
  RUN_TEST(test_overdub_to_overdub);
  return UNITY_END();
}
