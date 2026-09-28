// S4f.16: actual stop/worker/undo/storage owners; no copied stop implementation.
#define HARNESS_REAL_PLAYBACK_HOT_PATH
#define HARNESS_REAL_CAPTURE_STOP
#include "../test_support/capture_storage_harness.inc"
#include <cstdlib>
#if BYPASS_STOP_UNDO_SAVE
#error This fixture requires production undo and persistence admission
#endif

struct TrackCaptureStopTestAccess {
  static auto& bindRecordClosure(Track& t, uint8_t slot, uint32_t phaseTick, uint32_t length) {
    TEST_ASSERT_TRUE(t.prewarmCaptureStopJobs());
    auto* job = t.captureStopJobTail();
    TEST_ASSERT_NOT_NULL(job);
    job->owner = &t.getLoop(slot);
    job->slot = slot; job->loopId = job->owner->loopId;
    job->mode = CapturePhase::Record; job->interval = t.captureInterval_;
    job->phaseTick = phaseTick; job->loopLengthTicks = length;
    TEST_ASSERT_TRUE(job->owner->capture.store.sealCaptureRange(job->rawRange));
    job->ownsRawRange = true;
    TEST_ASSERT_TRUE(job->owner->borrowSealedCaptureRange(job->rawRange, &job->admission));
    job->pendingNotes.swap(t.pendingNotes);
    // Controlled binding only: production Record admission is a separate open gate.
    job->admission.pending = true;
    job->admission.frozenGeneration = 1;
    job->admission.stopTick = 1000;
    job->admission.phase = PendingStopPhase::ReservedClosure;
    job->admission.remainingClosureGrains = job->pendingNotes.size();
    auto* runtime = t.playbackRuntime.slotIfAllocated(slot);
    TEST_ASSERT_NOT_NULL(runtime);
    TEST_ASSERT_TRUE(runtime->prewarmCanonicalCaptureOutput());
    runtime->setCaptureStopOwner(job->admission, job->admission.frozenGeneration);
    // Production bind captures the interval's job-owned Record geometry receipt.
    t.captureRecordGeometryReceipt(*job);
    ++t.captureStopJobs_->count;
    return *job;
  }
  static auto closureGrain(Track& t) {
    return t.stepDeferredRecordClosure(*t.captureStopJobHead());
  }
  static auto stagingGrain(Track& t) {
    return t.stepDeferredRecordSourceStaging(*t.captureStopJobHead());
  }
  static auto normalizationGrain(Track& t) {
    return t.stepDeferredRecordNormalization(*t.captureStopJobHead());
  }
  static auto passTransferGrain(Track& t) {
    return t.stepDeferredRecordPassTransfer(*t.captureStopJobHead());
  }
  static auto publicationGrain(Track& t) {
    return t.stepDeferredRecordPublication(*t.captureStopJobHead());
  }
  static auto emptyGrain(Track& t) {
    return t.stepDeferredRecordEmptyResult(*t.captureStopJobHead());
  }
  static void clearGeometryReceipt(Track& t) {
    t.captureStopJobHead()->hasRecordGeometry = false;
  }
  static void restoreGeometryReceipt(Track& t, LoopId loopId) {
    auto& job = *t.captureStopJobHead();
    job.recordGeometryLoopId = loopId;
    job.hasRecordGeometry = true;
  }
  static bool ownsPendingPass(const Track& t) {
    const auto* job = t.captureStopJobHead();
    return job != nullptr && job->ownsPendingPass;
  }
  static bool hasRecordPass(const Track& t, uint8_t slot) {
    return t.getLoop(slot).passes.hasRecordPass();
  }
  static bool audibleSource(const Track& t, uint8_t slot) {
    return t.hasAudiblePlaybackSource(slot);
  }
  static LoopPlaybackRuntime& runtimeFor(Track& t, uint8_t slot) {
    return *t.playbackRuntime.slotIfAllocated(slot);
  }
  static uint32_t playbackGenerationOf(const Track& t) { return t.playbackGeneration; }
  static auto retirementGrain(Track& t) {
    return t.stepDeferredRecordRetirement(*t.captureStopJobHead());
  }
  static auto& closureRuntime(Track& t) {
    return *t.playbackRuntime.slotIfAllocated(t.captureStopJobHead()->slot);
  }
  static size_t heldCount(const Track& t) { return t.pendingNotes.size(); }
  static size_t stopQueueDepth(const Track& t) { return t.captureStopQueueDepth(); }
  static UndoLoopGeometry jobBeforeGeometry(Track& t) {
    return t.captureStopJobHead()->recordBeforeGeometry;
  }
  static void seedSuccessorRecordBaseline(Track& t, uint32_t length, uint32_t startLoop,
                                          uint32_t loopStart) {
    t.recordCaptureBaselineGeometry_ = {length, startLoop, loopStart};
    t.hasRecordCaptureBaselineGeometry_ = true;
  }
  static NoteId recordIdentity(const Track& t, uint8_t channel, uint8_t note) {
    const auto found = t.pendingNotes.find({note, channel});
    return found == t.pendingNotes.end() ? kInvalidNoteId : found->second.recordNoteId;
  }

  static void assertDeniedUndoRetainsObligation(Track& t) {
    auto& job = *t.captureStopJobHead();
    const auto phase = job.undoCollection.phase;
    const bool started = job.undoCollectionStarted, collected = job.undoCollected;
    const auto credits = job.admission.remainingUndoCollectionGrains;
    TEST_ASSERT_GREATER_THAN(0, credits);
    job.undoCollectionStarted = job.undoCollected = true;
    job.undoCollection.phase = Loop::SessionUndoCollection::Phase::Denied;
    step(t);
    const auto actualPhase = job.admission.phase;
    const auto actualCredits = job.admission.remainingUndoCollectionGrains;
    job.undoCollection.phase = phase;
    job.undoCollectionStarted = started; job.undoCollected = collected;
    TEST_ASSERT_EQUAL(PendingStopPhase::UndoCollection, actualPhase);
    TEST_ASSERT_EQUAL(credits, actualCredits);
    TEST_ASSERT_FALSE(job.undoPublicationStarted);
  }
  static void assertDeniedUndoPublicationRetries(Track& t) {
    auto& job = *t.captureStopJobHead();
    auto& stack = t.getGlobalUndoStack();
    const auto nextId = stack.nextEntryId;
    stack.nextEntryId = 0; // Refuse begin without moving the prepared entry.
    for (unsigned i = 0; i < 64 && !job.undoCollected; ++i) step(t);
    step(t); // A second refusal must remain in the same phase.
    const auto phase = job.admission.phase;
    const bool ready = job.undoPublicationReady, started = job.undoPublicationStarted;
    const auto retainedPass = job.undoEntry.passId;
    stack.nextEntryId = nextId; // Restore the admission before assertions/retry.
    TEST_ASSERT_EQUAL(PendingStopPhase::UndoCollection, phase);
    TEST_ASSERT_FALSE(ready); TEST_ASSERT_FALSE(started);
    TEST_ASSERT_EQUAL(job.pendingPass.id, retainedPass);
  }
  static PendingCapturePass& pass(Track& t) { return t.captureStopJobHead()->pendingPass; }
  static Capture& output(Track& t) {
    return t.playbackRuntime.slotIfAllocated(t.captureStopJobHead()->slot)->canonicalCaptureOutput;
  }
  static void assertRetiredCursors(const Track& t) {
    for (const auto& job : t.captureStopJobs_->entries) {
      TEST_ASSERT_FALSE(job.passTransfer.active());
      TEST_ASSERT_FALSE(job.passTransfer.complete());
    }
  }
  static bool hasReadablePass(Track& t) { return t.ownedPendingCapturePass() != nullptr; }
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
  static PendingStopPhase phase(const Track& t) {
    const auto* j = t.captureStopJobHead();
    TEST_ASSERT_NOT_NULL(j);
    return j->admission.phase;
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
      printf("credits closure=%u order=%llu ranked=%llu canonical=%llu undo=%llu history=%llu\n",
        j->admission.remainingClosureGrains,
        (unsigned long long)j->admission.remainingOrderGrains,
        (unsigned long long)j->admission.remainingRankedPreparationGrains,
        (unsigned long long)j->admission.remainingCanonicalPreparationGrains,
        (unsigned long long)j->admission.remainingUndoCollectionGrains,
        (unsigned long long)j->admission.remainingHistoryRecycleGrains);
      auto* rt = t.playbackRuntime.slotIfAllocated(j->slot);
      auto& loop = *j->owner;
      printf("commit=%u joint=%u verified=%u matches=%u gen=%u prefixGen=%u pending=%u\n",
        j->admission.canCommit(j->admission.transportCommandEpoch, j->admission.destination),
        loop.canCommitPreparedCaptureAndCompanions(j->admission.intervalToken(), &j->pendingPass),
        loop.preparedCompanionCaptureVerified(rt->canonicalPassInputs,j->admission.intervalToken()),
        rt->canonicalPassInputs.matchesCanonicalOwner(loop.passes,
          {loop.playbackRevision, t.playbackGeneration, loop.loopLengthTicks, loop.canonicalStorageGeneration}),
        t.playbackGeneration,rt->canonicalPrefixSource.sourceGeneration,j->pendingPass.preparationPending);
      printf("slot=%u loop=%lu raw=%u pass=%u credits=%llu free_chunks=%u\n", j->slot,
        static_cast<unsigned long>(j->loopId), j->ownsRawRange, j->ownsPendingPass,
        static_cast<unsigned long long>(budget(t)), LoopEventStore::freeChunkCount());
    }
  }
};
void TrackManager::beginSlotSelectionHold(uint8_t, uint8_t) { A34A_OFF_PATH(); }
void TrackManager::endSlotSelectionHold(uint8_t, uint8_t, uint32_t) { A34A_OFF_PATH(); }
void setUp() {
  noteMinLengthTicks = Config::DEFAULT_NOTE_MIN_LENGTH_TICKS;
  noteMinLengthRemoveEnabled = Config::DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED;
  PersistenceWorkQueue::resetForTests();
  StorageManagerInternal::storageSession.currentWorkspaceSave.pending = false;
  StorageManagerInternal::storageSession.currentWorkspaceSave.urgentRequested = false;
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
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::currentSetLoopSlotDirty[0][slot],
    "completed interval must request persistence for its own slot");
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
      TEST_ASSERT_EQUAL(PersistWorkState::Queued, PersistenceWorkQueue::workState(
        PersistWorkType::LoopPersist, persistKeyForSlot(0, slot)));
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
// Publication is observed by phase, not an arbitrary count of maintenance turns.
void advanceToPhase(Track& t, PendingStopPhase phase) {
  const uint64_t budget = TrackCaptureStopTestAccess::budget(t);
  for (uint64_t grain = 0; grain < budget && grain < 20000; ++grain) {
    if (TrackCaptureStopTestAccess::phase(t) == phase) return;
    TrackCaptureStopTestAccess::step(t);
  }
  TrackCaptureStopTestAccess::diagnostic(t);
  TEST_FAIL_MESSAGE("publication phase not reached within admitted work");
}
void assertNoPersistenceRequest() {
  TEST_ASSERT_FALSE(StorageManagerInternal::storageSession.currentWorkspaceSave.pending);
  TEST_ASSERT_EQUAL_UINT(0, PersistenceWorkQueue::queueDepth());
  for (const auto& row : StorageManagerInternal::currentSetLoopSlotDirty)
    for (bool dirty : row) TEST_ASSERT_FALSE(dirty);
}
void deferredPersistence(bool stopped, bool superseded, bool stale) {
  LoopEventStore::initPool();
  // Use a non-selected track: persistence must resolve the owner, not UI focus.
  Track& t = trackManager.getTrack(2);
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, true);
  prepareSlot(t, 1, true);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  if (stopped) t.stopOverdubbingToStopped(); else t.stopOverdubbing();
  TEST_ASSERT_EQUAL_UINT(1, TrackCaptureStopTestAccess::jobs(t));
  if (stopped) t.forceSetState(TRACK_PLAYING);
  start(t, 1, true, 768, 62);
  assertNoPersistenceRequest();
  advanceToPhase(t, PendingStopPhase::AtomicTransfer);
  assertNoPersistenceRequest();
  Loop& owner = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT(0, owner.passes.overdubPasses.size());
  if (stale) {
    const LoopId original = owner.loopId;
    ++owner.loopId;
    for (unsigned i = 0; i < 3; ++i) TrackCaptureStopTestAccess::step(t);
    const auto phaseWhileStale = TrackCaptureStopTestAccess::phase(t);
    owner.loopId = original; // restore even when the assertion below aborts this case
    assertNoPersistenceRequest();
    TEST_ASSERT_EQUAL(PendingStopPhase::AtomicTransfer, phaseWhileStale);
    TEST_ASSERT_EQUAL_UINT(0, owner.passes.overdubPasses.size());
  }
  if (superseded) ++owner.playbackRevision; // fault-inject a replaced causal base
  advanceToPhase(t, PendingStopPhase::DeferredCleanup);
  TEST_ASSERT_EQUAL_UINT(1, owner.passes.overdubPasses.size());
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[2][0]);
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[2][1]);
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  TEST_ASSERT_TRUE(StorageManagerInternal::storageSession.currentWorkspaceSave.pending);
  TEST_ASSERT_TRUE(StorageManagerInternal::storageSession.currentWorkspaceSave.urgentRequested);
  TEST_ASSERT_EQUAL_UINT(2, PersistenceWorkQueue::queueDepth());
  for (auto type : {PersistWorkType::SlotMeta, PersistWorkType::LoopPersist})
    TEST_ASSERT_EQUAL(PersistWorkState::Queued,
      PersistenceWorkQueue::workState(type, persistKeyForSlot(2, 0)));
  // Acknowledge work at the I/O boundary so coalescing cannot hide repeat requests.
  PersistWorkItem item;
  while (PersistenceWorkQueue::beginWriteQueuedItem(item)) PersistenceWorkQueue::markItemPersisted(item);
  StorageManagerInternal::storageSession.currentWorkspaceSave.pending = false;
  StorageManagerInternal::currentSetLoopSlotDirty[2][0] = false;
  const uint64_t budget = TrackCaptureStopTestAccess::budget(t);
  for (uint64_t grain = 0; t.hasStopInFlight() && grain < budget && grain < 20000; ++grain)
    TrackCaptureStopTestAccess::step(t);
  TEST_ASSERT_FALSE(t.hasStopInFlight());
  for (unsigned i = 0; i < 10; ++i) TrackCaptureStopTestAccess::step(t);
  assertNoPersistenceRequest();
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(1).capture.store.size());
  TEST_ASSERT_EQUAL_UINT(1, owner.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(PersistWorkState::Persisted,
    PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, persistKeyForSlot(2, 0)));
}
void test_deferred_publication_requests_own_slot_once() { deferredPersistence(false, false, false); }
void test_stopped_publication_requests_own_slot_once() { deferredPersistence(true, false, false); }
void test_superseded_publication_requests_own_slot_once() { deferredPersistence(false, true, false); }
void test_stale_publication_waits_before_persistence() { deferredPersistence(false, false, true); }
void test_record_closures_keep_identity_after_raw_source_freezes() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 60, 90, 120);
  Loop& loop = t.getLoop(0);
  const NoteId physicalId = loop.capture.store.at(0).noteId;
  const NoteId stopId = loop.capture.store.at(1).noteId;
  TEST_ASSERT_NOT_EQUAL(physicalId, stopId);
  t.noteOn(1, 60, 80, 130); // rejected duplicate must not replace the receipt
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(loop.capture.store.sealCaptureRange(frozen));
  // Another producer row cannot supply the frozen interval's closure identity.
  MidiEvent other = MidiEvent::NoteOn(30, 1, 60, 70);
  other.noteId = loop.allocateNoteId();
  TEST_ASSERT_TRUE(loop.capture.store.append(other));
  t.noteOff(1, 60, 0, 180);
  TEST_ASSERT_EQUAL(physicalId, loop.capture.store.at(1).noteId);
  loop.loopLengthTicks = 192;
  t.finalizePendingNotes(400);
  TEST_ASSERT_EQUAL(3, loop.capture.store.size());
  TEST_ASSERT_EQUAL(stopId, loop.capture.store.at(2).noteId);
  TEST_ASSERT_EQUAL(191, loop.capture.store.at(2).tick);
  TEST_ASSERT_EQUAL(2, loop.capture.store.at(2).channel);
  TEST_ASSERT_EQUAL(2, frozen.size());
  MidiEvent original; bool end = false;
  TEST_ASSERT_TRUE(frozen.read(frozen.generation(), 0, 0, original, end));
  TEST_ASSERT_EQUAL(physicalId, original.noteId);
}

void test_record_stop_commits_exact_held_note_closures() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 61, 90, 300);
  Loop& loop = t.getLoop(0);
  const NoteId first = loop.capture.store.at(0).noteId;
  const NoteId second = loop.capture.store.at(1).noteId;
  t.stopRecordingToStopped(868);
  TEST_ASSERT_EQUAL(768, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::heldCount(t));
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  // The existing stopped-record transition projects this boundary to phase zero;
  // full stop normalization adds invalid-ID terminal Offs at length-1. Preserve
  // that current six-row content here; this slice changes identity lookup only.
  TEST_ASSERT_EQUAL(6, rows.size());
  unsigned ons[2] = {}, offs[2] = {};
  for (const auto& row : rows) {
    TEST_ASSERT_TRUE(row.channel == 1 || row.channel == 2);
    const unsigned channel = row.channel - 1;
    TEST_ASSERT_EQUAL(channel == 0 ? 60 : 61, row.data.noteData.note);
    if (row.isNoteOn()) {
      ++ons[channel];
      TEST_ASSERT_EQUAL(channel == 0 ? 12 : 200, row.tick);
      TEST_ASSERT_EQUAL(channel == 0 ? first : second, row.noteId);
    } else {
      TEST_ASSERT_TRUE(row.isNoteOff());
      ++offs[channel];
      TEST_ASSERT_TRUE(row.tick == 0 || row.tick == 767);
      TEST_ASSERT_EQUAL(row.tick == 767 ? kInvalidNoteId : (channel == 0 ? first : second), row.noteId);
    }
  }
  TEST_ASSERT_EQUAL(1, ons[0]); TEST_ASSERT_EQUAL(1, ons[1]);
  TEST_ASSERT_EQUAL(2, offs[0]); TEST_ASSERT_EQUAL(2, offs[1]);
}

void test_record_identity_requires_accepted_input_and_handles_preroll() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  TEST_ASSERT_TRUE(t.setState(TRACK_ARMED));
  t.noteOn(1, 60, 100, 12);
  t.startRecording(100);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  TEST_ASSERT_EQUAL(0, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(loop.capture.store.at(0).noteId,
      TrackCaptureStopTestAccess::recordIdentity(t, 1, 60));
  const NoteId first = loop.capture.store.at(0).noteId;
  // Existing phase-denial seam: rejected input must not leave a closure receipt.
  loop.capture.phase = CapturePhase::None;
  t.noteOn(1, 62, 100, 120);
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::heldCount(t));
  TEST_ASSERT_EQUAL(kInvalidNoteId, TrackCaptureStopTestAccess::recordIdentity(t, 1, 62));
  loop.capture.phase = CapturePhase::Record;
  t.noteOn(1, 62, 100, 130);
  const NoteId retried = loop.capture.store.at(1).noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, retried);
  TEST_ASSERT_NOT_EQUAL(first, retried);
  TEST_ASSERT_EQUAL(retried, TrackCaptureStopTestAccess::recordIdentity(t, 1, 62));
  t.noteOff(1, 62, 0, 160);
  TEST_ASSERT_EQUAL(retried, loop.capture.store.at(2).noteId);
  TEST_ASSERT_EQUAL(kInvalidNoteId, TrackCaptureStopTestAccess::recordIdentity(t, 1, 62));
  printf("PendingNote bytes=%zu recordId offset=%zu\n", sizeof(PendingNote), offsetof(PendingNote, recordNoteId));
}

void test_record_stop_closure_does_not_search_reused_producer() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  Loop& loop = t.getLoop(0);
  const NoteId identity = loop.capture.store.at(0).noteId;
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(loop.capture.store.sealCaptureRange(frozen));
  MidiEvent unrelated = MidiEvent::NoteOn(40, 1, 60, 90);
  unrelated.noteId = loop.allocateNoteId();
  TEST_ASSERT_TRUE(loop.capture.store.append(unrelated));
  loop.loopLengthTicks = 192;
  t.finalizePendingNotes(400);
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  TEST_ASSERT_EQUAL(identity, loop.capture.store.at(1).noteId);
  TEST_ASSERT_EQUAL(191, loop.capture.store.at(1).tick);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::heldCount(t));
  TEST_ASSERT_EQUAL(1, frozen.size());
}

void test_record_job_closures_preserve_a_live_successor() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); prepareSlot(t, 1, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112); t.noteOn(2, 61, 90, 120);
  const NoteId first = t.getLoop(0).capture.store.at(0).noteId;
  const NoteId second = t.getLoop(0).capture.store.at(1).noteId;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  t.forceSetState(TRACK_PLAYING); // binding transition controlled by fixture
  t.setActiveLoopIndex(1); t.startRecording(1100);
  TEST_ASSERT_TRUE(t.isRecording());
  t.noteOn(3, 76, 80, 1112);
  const auto chunks = LoopEventStore::usedChunkCount();
  for (unsigned i = 0; i < 2; ++i) {
    job.admission.destination = i == 0 ? PendingStopDestination::Playing : PendingStopDestination::Stopped;
    TEST_ASSERT_EQUAL(i == 0 ? LoopEventStoreRewriteResult::Pending : LoopEventStoreRewriteResult::Complete,
        TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_EQUAL(i + 1, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(1 - i, job.pendingNotes.size());
    TEST_ASSERT_EQUAL(1 - i, job.admission.remainingClosureGrains);
    TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::heldCount(t));
    TEST_ASSERT_EQUAL(1, t.getLoop(1).capture.store.size());
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
    TEST_ASSERT_EQUAL(2, job.rawRange.size());
    for (size_t raw = 0; raw < 2; ++raw) {
      MidiEvent event; bool end = false;
      TEST_ASSERT_TRUE(job.rawRange.read(job.rawRange.generation(), 0, raw, event, end));
      TEST_ASSERT_FALSE(end); TEST_ASSERT_TRUE(event.isNoteOn());
      TEST_ASSERT_EQUAL(raw == 0 ? first : second, event.noteId);
      TEST_ASSERT_EQUAL(raw == 0 ? 12 : 20, event.tick);
    }
    if (i == 0) {
      // Rebinding the runtime at the same address must not adopt old output.
      ++job.admission.frozenGeneration; ++runtime.captureStopOwnerGeneration;
      TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
      TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.size());
      TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
      --job.admission.frozenGeneration; --runtime.captureStopOwnerGeneration;
      PendingStopAdmission anotherDescriptor;
      anotherDescriptor.frozenGeneration = job.admission.frozenGeneration;
      runtime.frozenCaptureRewrite.sourceOwner = &anotherDescriptor;
      TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
      TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.size());
      TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
      runtime.frozenCaptureRewrite.sourceOwner = &job.admission;
    }
  }
  TEST_ASSERT_EQUAL(CapturePhase::Record, runtime.canonicalCaptureOutput.phase);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordSourceStaging, job.admission.phase);
  for (size_t i = 0; i < 2; ++i) {
    const auto& row = runtime.canonicalCaptureOutput.store.at(i);
    TEST_ASSERT_TRUE(row.isNoteOff()); TEST_ASSERT_EQUAL(767, row.tick);
    TEST_ASSERT_EQUAL(row.channel == 1 ? first : second, row.noteId);
    TEST_ASSERT_EQUAL(row.channel == 1 ? 60 : 61, row.data.noteData.note);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(2, runtime.canonicalCaptureOutput.store.size());
}

void test_record_source_staging_merges_the_frozen_take_after_closure() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 61, 90, 130);
  const NoteId first = t.getLoop(0).capture.store.at(0).noteId;
  const NoteId second = t.getLoop(0).capture.store.at(1).noteId;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  while (result == LoopEventStoreRewriteResult::Pending) {
    result = TrackCaptureStopTestAccess::closureGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordSourceStaging, job.admission.phase);
  TEST_ASSERT_EQUAL(2, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(2, job.rawRange.size());

  // A runtime owner generation that no longer matches the job must not stage.
  ++runtime.captureStopOwnerGeneration;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::stagingGrain(t));
  TEST_ASSERT_EQUAL(2, runtime.canonicalCaptureOutput.store.size());
  --runtime.captureStopOwnerGeneration;

  result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 4096) {
    result = TrackCaptureStopTestAccess::stagingGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalOrder, job.admission.phase);

  // The closure Offs stay at the head and the frozen take follows with its identities.
  const auto& store = runtime.canonicalCaptureOutput.store;
  TEST_ASSERT_EQUAL(4, store.size());
  TEST_ASSERT_TRUE(store.at(0).isNoteOff());
  TEST_ASSERT_TRUE(store.at(1).isNoteOff());
  TEST_ASSERT_TRUE(store.at(2).isNoteOn());
  TEST_ASSERT_TRUE(store.at(3).isNoteOn());
  bool sawFirst = false, sawSecond = false;
  for (size_t i = 2; i < 4; ++i) {
    sawFirst = sawFirst || store.at(i).noteId == first;
    sawSecond = sawSecond || store.at(i).noteId == second;
  }
  TEST_ASSERT_TRUE(sawFirst);
  TEST_ASSERT_TRUE(sawSecond);
}

void test_record_normalization_orders_and_identity_completes_the_merged_store() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 61, 90, 120);
  const NoteId first = t.getLoop(0).capture.store.at(0).noteId;
  const NoteId second = t.getLoop(0).capture.store.at(1).noteId;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  while (result == LoopEventStoreRewriteResult::Pending) {
    result = TrackCaptureStopTestAccess::closureGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 4096) {
    result = TrackCaptureStopTestAccess::stagingGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalOrder, job.admission.phase);
  TEST_ASSERT_EQUAL(4, runtime.canonicalCaptureOutput.store.size());

  // A runtime binding that no longer matches the job must not normalize.
  ++runtime.captureStopOwnerGeneration;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::normalizationGrain(t));
  TEST_ASSERT_EQUAL(4, runtime.canonicalCaptureOutput.store.size());
  --runtime.captureStopOwnerGeneration;

  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 65536) {
    result = TrackCaptureStopTestAccess::normalizationGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);

  // Canonical order: the two staged NoteOns by tick, then the terminal Offs.
  const auto& store = runtime.canonicalCaptureOutput.store;
  TEST_ASSERT_EQUAL(4, store.size());
  TEST_ASSERT_TRUE(store.at(0).isNoteOn());
  TEST_ASSERT_EQUAL(12, store.at(0).tick);
  TEST_ASSERT_TRUE(store.at(1).isNoteOn());
  TEST_ASSERT_EQUAL(20, store.at(1).tick);
  TEST_ASSERT_TRUE(store.at(2).isNoteOff());
  TEST_ASSERT_TRUE(store.at(3).isNoteOff());
  TEST_ASSERT_EQUAL(767, store.at(2).tick);
  TEST_ASSERT_EQUAL(767, store.at(3).tick);
  bool sawFirst = false, sawSecond = false;
  for (size_t i = 0; i < 2; ++i) {
    sawFirst = sawFirst || store.at(i).noteId == first;
    sawSecond = sawSecond || store.at(i).noteId == second;
  }
  TEST_ASSERT_TRUE(sawFirst);
  TEST_ASSERT_TRUE(sawSecond);

  // The terminal phase must not re-run preparation: it waits for publication.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::normalizationGrain(t));
  TEST_ASSERT_EQUAL(4, runtime.canonicalCaptureOutput.store.size());

  // Bounded pass transfer into the job-owned receipt; nothing is published yet.
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 65536) {
    result = TrackCaptureStopTestAccess::passTransferGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_FALSE(job.pendingPass.committedChunkIds.empty());
  TEST_ASSERT_EQUAL(CapturePassPhase::Record, job.pendingPass.phase);
  TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());

  // The terminal wait refuses to transfer twice.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::passTransferGrain(t));

  // Atomic publication: the staged pass becomes canonical and its Record undo is pushed.
  // A successor record start rewrites the Track's inline baseline first; the publication
  // must still use this interval's job-owned receipt.
  const UndoLoopGeometry jobBefore = TrackCaptureStopTestAccess::jobBeforeGeometry(t);
  TrackCaptureStopTestAccess::seedSuccessorRecordBaseline(t, 4242, 4243, 4244);
  TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 4096) {
    result = TrackCaptureStopTestAccess::publicationGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, job.admission.phase);
  TEST_ASSERT_TRUE(t.getLoop(0).passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1, TrackUndo::getUndoCount(t));
  const auto& tip = t.getGlobalUndoStack().entries.back();
  TEST_ASSERT_EQUAL(UndoEntryKind::RecordPassAdded, tip.kind);
  TEST_ASSERT_EQUAL(jobBefore.loopLengthTicks, tip.beforeGeometry.loopLengthTicks);
  TEST_ASSERT_EQUAL(jobBefore.startLoopTick, tip.beforeGeometry.startLoopTick);
  TEST_ASSERT_EQUAL(jobBefore.loopStartTick, tip.beforeGeometry.loopStartTick);
  TEST_ASSERT_NOT_EQUAL(4242u, tip.beforeGeometry.loopLengthTicks);
  TEST_ASSERT_EQUAL(t.getLoop(0).loopLengthTicks, tip.afterGeometry.loopLengthTicks);

  // Bounded retirement releases the frozen source and returns the ring entry.
  const size_t depthBefore = TrackCaptureStopTestAccess::stopQueueDepth(t);
  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 65536) {
    result = TrackCaptureStopTestAccess::retirementGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(depthBefore - 1, TrackCaptureStopTestAccess::stopQueueDepth(t));
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}

void test_record_job_closure_denial_retains_obligation_for_retry() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100); t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 96, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  auto denied = [&]() {
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
    TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
    TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
  };
  job.mode = CapturePhase::Overdub; denied(); job.mode = CapturePhase::Record;
  ++runtime.captureStopOwnerGeneration; denied(); --runtime.captureStopOwnerGeneration;
  ++job.loopId; denied(); --job.loopId;
  const auto id = job.pendingNotes.begin()->second.recordNoteId;
  job.pendingNotes.begin()->second.recordNoteId = kInvalidNoteId; denied();
  job.pendingNotes.begin()->second.recordNoteId = id;
  TEST_ASSERT_TRUE(runtime.canonicalCaptureChunks.releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT));
  denied();
  TEST_ASSERT_TRUE(runtime.canonicalCaptureChunks.reserveChunks(1));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(id, runtime.canonicalCaptureOutput.store.at(0).noteId);
  TEST_ASSERT_EQUAL(96, runtime.canonicalCaptureOutput.store.at(0).tick);
}

void test_record_job_closure_rejects_reused_raw_and_replaced_output() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100);
  t.noteOn(1, 60, 100, 112); t.noteOn(1, 61, 100, 120);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 96, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending, TrackCaptureStopTestAccess::closureGrain(t));
  runtime.canonicalCaptureOutput.store.clear();
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
  TEST_ASSERT_TRUE(job.rawRange.releaseGrain(1));
  TEST_ASSERT_TRUE(job.rawRange.reserveTail());
  TEST_ASSERT_TRUE(job.owner->capture.store.append(MidiEvent::NoteOn(12, 1, 60, 90)));
  TEST_ASSERT_TRUE(job.owner->capture.store.sealCaptureRange(job.rawRange));
  // Remove the output fault so the saved raw generation is the remaining denial.
  runtime.discardFrozenCaptureCanonicalOrder();
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.prewarmCaptureDirectory());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
}

void test_record_job_closures_exceed_overdub_hold_limit_without_staging() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100);
  constexpr unsigned count = LoopEventStoreConfig::CHUNK_CAPACITY + 1;
  static_assert(count > RuntimePlaybackViewConfig::kMaxCaptureNoteReservations, "exercise Record envelope");
  std::array<NoteId, count> identities{};
  for (unsigned i = 0; i < count; ++i) {
    t.noteOn(1 + i / 128, i % 128, 100, 112 + i);
    identities[i] = t.getLoop(0).capture.store.at(i).noteId;
  }
  TEST_ASSERT_EQUAL(count, TrackCaptureStopTestAccess::heldCount(t));
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 700, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  const auto chunks = LoopEventStore::usedChunkCount();
  for (unsigned i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL(i + 1 == count ? LoopEventStoreRewriteResult::Complete : LoopEventStoreRewriteResult::Pending,
        TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_EQUAL(i + 1, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(count - i - 1, job.pendingNotes.size());
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
  }
  TEST_ASSERT_EQUAL(count, job.rawRange.size());
  std::array<bool, count> seen{};
  for (unsigned i = 0; i < count; ++i) {
    const auto& row = runtime.canonicalCaptureOutput.store.at(i);
    const size_t input = (row.channel - 1) * 128 + row.data.noteData.note;
    TEST_ASSERT_LESS_THAN(count, input);
    TEST_ASSERT_FALSE(seen[input]); seen[input] = true;
    TEST_ASSERT_TRUE(row.isNoteOff()); TEST_ASSERT_EQUAL(700, row.tick);
    TEST_ASSERT_EQUAL(identities[input], row.noteId);
  }
}

void test_record_job_without_held_notes_emits_no_closures() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100);
  t.noteOn(1, 60, 100, 112); t.noteOff(1, 60, 0, 180);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 96, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(0, job.admission.remainingClosureGrains);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
}

// ---------------------------------------------------------------------------
// S4f-CLOSURE-PLAN Step 1: preparation/publication production-worker boundaries.
// ---------------------------------------------------------------------------

using GrainFn = LoopEventStoreRewriteResult (*)(Track&);

static LoopEventStoreRewriteResult driveGrains(Track& t, GrainFn grain, size_t limit = 200000) {
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < limit) {
    result = grain(t);
  }
  return result;
}

/// Drive closure -> staging -> normalization to the prepared terminal.
static void driveToPrepared(Track& t) {
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
}

void test_record_publication_preflight_denies_before_the_irreversible_commit() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  driveToPrepared(t);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_FALSE(job.pendingPass.committedChunkIds.empty());

  // A missing job-owned geometry receipt must deny BEFORE the irreversible commit:
  // committing the pass first would publish content with no `RecordPassAdded` undo
  // entry, no persistence, and no retryable receipt.
  TrackCaptureStopTestAccess::clearGeometryReceipt(t);
  const size_t queueBefore = PersistenceWorkQueue::queueDepth();
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::publicationGrain(t));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsPendingPass(t),
                           "the job-owned pass must survive a preflight denial");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasRecordPass(t, 0),
                            "no content may be published before the preflight passes");
  TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
  TEST_ASSERT_EQUAL(queueBefore, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);

  // The same binding retries once the receipt is back: exactly one publish.
  TrackCaptureStopTestAccess::restoreGeometryReceipt(t, job.loopId);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, job.admission.phase);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
  TEST_ASSERT_EQUAL(1, TrackUndo::getUndoCount(t));
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
}

void test_record_pass_transfer_continues_after_the_store_empties() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  auto& output = runtime.canonicalCaptureOutput.store;
  // Fill the prepared private output with enough chunks that one bounded transfer
  // grain moves every chunk and still has destination-order reversals left: the
  // store empties while the transfer is provably Pending. 24 chunks at the
  // production 32-unit preparation quantum (24 moves + 8 of 12 swaps) is exactly
  // that shape; if the quantum moves, this fixture fails loudly rather than
  // silently dropping the case.
  constexpr size_t kChunks = 24;
  std::vector<uint16_t> sourceOrder;
  LoopEventStore::enterEphemeralSeal();
  for (size_t i = 0; i < kChunks * LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(output.append(MidiEvent::NoteOn(
        static_cast<uint32_t>(i), 1, 60, static_cast<uint32_t>(i + 1))));
  }
  LoopEventStore::leaveEphemeralSeal();
  TEST_ASSERT_EQUAL(kChunks, output.chunkIds().size());
  sourceOrder.assign(output.chunkIds().begin(), output.chunkIds().end());

  // The transfer destination is the prewarmed ownership row; the production
  // transfer sizes it from the prepared output, so this fixture must observe an
  // accepted take far beyond the fixed overdub transfer row.
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  job.admission.phase = PendingStopPhase::RecordPrepared;

  bool sawEmptyPending = false;
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  for (size_t turn = 0; turn < 200 && result == LoopEventStoreRewriteResult::Pending; ++turn) {
    result = TrackCaptureStopTestAccess::passTransferGrain(t);
    if (result == LoopEventStoreRewriteResult::Pending && output.empty()) {
      sawEmptyPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(sawEmptyPending,
                           "the fixture must observe an empty store mid-transfer");
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_TRUE(job.pendingPass.preparationPending);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);
  TEST_ASSERT_EQUAL_UINT32(kChunks, job.pendingPass.committedChunkIds.size());

  // The owned continuation must finish rather than deny on the emptied store.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_FALSE(job.pendingPass.preparationPending);
  TEST_ASSERT_EQUAL_UINT32(kChunks, job.pendingPass.committedChunkIds.size());
  // Exact directory order: the bounded reversal restores the source order.
  size_t index = 0;
  for (uint16_t id : job.pendingPass.committedChunkIds) {
    TEST_ASSERT_EQUAL_UINT32(sourceOrder[index++], id);
  }
  TEST_ASSERT_EQUAL_UINT32(kChunks, index);
  TEST_ASSERT_FALSE(LoopEventStore::isEphemeralSeal());
}

void test_record_pass_transfer_capacity_refusal_fails_closed() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  auto& output = runtime.canonicalCaptureOutput.store;
  // A take beyond the fixed overdub transfer row forces the ownership-row capacity
  // admission before the bounded transfer can start.
  constexpr size_t kChunks = PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS + 2;
  LoopEventStore::enterEphemeralSeal();
  for (size_t i = 0; i < kChunks * LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(output.append(MidiEvent::NoteOn(
        static_cast<uint32_t>(i), 1, 60, static_cast<uint32_t>(i + 1))));
  }
  LoopEventStore::leaveEphemeralSeal();
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  job.admission.phase = PendingStopPhase::RecordPrepared;

  // Forced allocation refusal: the admission must report failure. `std::vector`
  // growth cannot report a null allocate, so an unguarded push_back would write
  // through the refused block (reproduced as an ASan null-write).
  LoopEventStore::setCommittedChunkIdProbeRefusedForTests(true);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::passTransferGrain(t));
  LoopEventStore::setCommittedChunkIdProbeRefusedForTests(false);
  TEST_ASSERT_FALSE_MESSAGE(job.ownsPendingPass,
                            "a refused allocation must not begin the pass transfer");
  TEST_ASSERT_FALSE(job.pendingPass.preparationPending);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);
  TEST_ASSERT_EQUAL(kChunks * LoopEventStoreConfig::CHUNK_CAPACITY, output.size());

  // The same binding retries once the allocation is available again.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_EQUAL(kChunks, job.pendingPass.committedChunkIds.size());
}

void test_empty_record_take_all_dropped_retires_without_content() {
    // All rows dropped by the stop projection: a non-note row beyond the final
    // length leaves the closure and the staging output empty.
    Track& t = trackManager.getTrack(0);
    prepareSlot(t, 0, false);
    t.startRecording(100);
    TEST_ASSERT_TRUE(t.getLoop(0).capture.store.append(MidiEvent::ControlChange(900, 1, 7, 1)));
    auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
    auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
    driveToPrepared(t);
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordEmpty, job.admission.phase);
    TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_FALSE(job.ownsPendingPass);

    // A live successor on another slot must keep its producer untouched.
    Track& t1 = trackManager.getTrack(1);
    prepareSlot(t1, 0, false);
    t1.setActiveLoopIndex(0);
    t1.startRecording(200);
    TEST_ASSERT_TRUE(t1.getLoop(0).capture.store.append(MidiEvent::NoteOn(212, 1, 70, 100)));
    const size_t successorRows = t1.getLoop(0).capture.store.size();

    const size_t queueBefore = PersistenceWorkQueue::queueDepth();
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::emptyGrain));
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, job.admission.phase);
    TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
    TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
    TEST_ASSERT_EQUAL(queueBefore, PersistenceWorkQueue::queueDepth());
    TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
    TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).loopLengthTicks);
    TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).startLoopTick);
    TEST_ASSERT_EQUAL(successorRows, t1.getLoop(0).capture.store.size());
    TEST_ASSERT_EQUAL(CapturePhase::Record, t1.getLoop(0).capture.phase);

    const size_t depthBefore = TrackCaptureStopTestAccess::stopQueueDepth(t);
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::retirementGrain));
    TEST_ASSERT_EQUAL(depthBefore - 1, TrackCaptureStopTestAccess::stopQueueDepth(t));
    TEST_ASSERT_EQUAL(successorRows, t1.getLoop(0).capture.store.size());
    TEST_ASSERT_EQUAL(CapturePhase::Record, t1.getLoop(0).capture.phase);
}

void test_empty_record_take_all_filtered_retires_without_content() {
    // All rows filtered away: a pair shorter than the minimum-length policy leaves
    // a non-empty prepared store that the filter compacts to nothing.
    Track& t = trackManager.getTrack(0);
    prepareSlot(t, 0, false);
    t.startRecording(100);
    t.noteOn(1, 60, 100, 112);
    t.noteOff(1, 60, 0, 124);  // 12 ticks: survives the capture-time minimum
    auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
    // The preparation policy now removes the pair, so a non-empty staged take is
    // compacted to nothing rather than being denied.
    noteMinLengthTicks = 48;
    auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
    TEST_ASSERT_EQUAL_UINT32(2, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordEmpty, job.admission.phase);
    TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::emptyGrain));
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, job.admission.phase);
    TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
    TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::retirementGrain));
    TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}

void test_deferred_record_interval_is_audible_only_with_a_bound_window() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  // The production record start prepares and installs the retained bar-1 window.
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  // The stop edge stamps the interval's final geometry; the deferred worker then binds
  // the retained window to it.
  loop.loopLengthTicks = 768;
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  }
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 32; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  }

  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
    TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].complete);
    TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].rawCoveragePrepared);
  }
  // A pending job whose window is still raw has no audible source: the interval must
  // stay silent rather than fabricate one from nothing.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::audibleSource(t, 0));
  // The closure grain binds the window to this interval's geometry and frozen source.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->firstRecordReturnWindowIndex());
  }
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::audibleSource(t, 0));

  // Another slot has no source of its own.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::audibleSource(t, 1));
  // Retiring the job ends the unpublished interval's eligibility; committed content
  // owns the slot from then on.
  job.admission.pending = false;
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::audibleSource(t, 0));
}

void test_private_grains_never_hold_the_global_ephemeral_seal() {
  Track& t0 = trackManager.getTrack(0);
  prepareSlot(t0, 0, false);
  t0.startRecording(100);
  // Enough closed pairs that staging needs many bounded grains and its private
  // output seals more than one chunk.
  for (uint32_t pair = 0; pair < 160; ++pair) {
    const uint32_t tick = 4 + pair * 4;
    t0.noteOn(1 + (pair % 8), static_cast<uint8_t>(pair % 128), 100, tick);
    t0.noteOff(1 + (pair % 8), static_cast<uint8_t>(pair % 128), 0, tick + 2);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t0, 0, 768, 768);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t0, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordSourceStaging, job.admission.phase);

  // A second track's normal capture, appended between two private grains, must
  // still reach normal persistence admission: a long-held global seal would make
  // `sealChunk` skip it.
  Track& t1 = trackManager.getTrack(1);
  prepareSlot(t1, 0, false);
  t1.setActiveLoopIndex(0);
  t1.startRecording(200);
  const size_t queueBefore = PersistenceQueue::queueDepth();
  t1.noteOn(1, 60, 100, 212);
  while (t1.getLoop(0).capture.store.size() < LoopEventStoreConfig::CHUNK_CAPACITY) {
    TEST_ASSERT_TRUE(t1.getLoop(0).capture.store.append(MidiEvent::ControlChange(
        static_cast<uint32_t>(212 + t1.getLoop(0).capture.store.size()), 1, 7, 1)));
  }
  TEST_ASSERT_EQUAL(queueBefore + 1, PersistenceQueue::queueDepth());
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Queued),
                    static_cast<int>(PersistenceQueue::chunkState(
                        t1.getLoop(0).capture.store.chunkIds().front())));

  // Every private grain must return with the global seal absent, and the private
  // output chunks (which seal past the staging ceiling) must never be admitted.
  const size_t queueAfterNormal = PersistenceQueue::queueDepth();
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 200000) {
    result = TrackCaptureStopTestAccess::stagingGrain(t0);
    TEST_ASSERT_FALSE(LoopEventStore::isEphemeralSeal());
    TEST_ASSERT_EQUAL(queueAfterNormal, PersistenceQueue::queueDepth());
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_GREATER_THAN(1, TrackCaptureStopTestAccess::closureRuntime(t0)
                                  .canonicalCaptureOutput.store.chunkIds().size());
  for (uint16_t id : TrackCaptureStopTestAccess::closureRuntime(t0)
                         .canonicalCaptureOutput.store.chunkIds()) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::NotScheduled),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
  TEST_ASSERT_EQUAL(queueAfterNormal, PersistenceQueue::queueDepth());
}

// Independent inventories for the deferred Record normalizer migration. These
// call the real stop entry points; they do not copy normalization into a fixture.
void assertRecordRows(const Loop& loop, const SessionMidiEventVec& expected) {
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  TEST_ASSERT_EQUAL(expected.size(), rows.size());
  for (const auto& want : expected) {
    size_t matches = 0;
    for (const auto& actual : rows) {
      const bool payload = want.type == midi::ControlChange
          ? actual.data.ccData.cc == want.data.ccData.cc && actual.data.ccData.value == want.data.ccData.value
          : actual.data.noteData.note == want.data.noteData.note && actual.data.noteData.velocity == want.data.noteData.velocity;
      if (actual.type == want.type && actual.tick == want.tick &&
          actual.channel == want.channel && actual.noteId == want.noteId && payload) ++matches;
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, matches, "record normalization inventory mismatch");
  }
}

void recordNormalizationInventory(bool playing, bool removeShort) {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  noteMinLengthTicks = 48; noteMinLengthRemoveEnabled = removeShort;
  constexpr uint32_t origin = 768;
  t.startRecording(origin);
  Loop& loop = t.getLoop(0);
  SessionMidiEventVec expected;
  auto pair = [&](uint8_t channel, uint8_t note, uint32_t on, uint32_t off,
                  uint32_t expectedOn, uint32_t expectedOff, bool synthetic, bool keep) {
    t.noteOn(channel, note, 100, origin + on);
    const NoteId id = TrackCaptureStopTestAccess::recordIdentity(t, channel, note);
    TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, id);
    t.noteOff(channel, note, 0, origin + off);
    if (!keep) return;
    auto a = MidiEvent::NoteOn(expectedOn, channel, note, 100); a.noteId = id;
    auto b = MidiEvent::NoteOff(expectedOff, channel, note, 0); b.noteId = synthetic ? kInvalidNoteId : id;
    expected.push_back(a); expected.push_back(b);
  };
  pair(1, 60, 780, 820, 12, 132, true, true);
  pair(2, 61, 700, 800, 700, 767, true, true);
  pair(3, 62, 50, 70, 50, 70, false, !removeShort);
  pair(4, 63, 100, 148, 100, 148, false, true);
  const auto cc = MidiEvent::ControlChange(30, 1, 7, 91);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(cc)); expected.push_back(cc);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(780, 1, 7, 19)));
  TEST_ASSERT_EQUAL(10, loop.capture.store.size());
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::heldCount(t));
  clockManager.assignCurrentTickSilently(origin + 900);
  if (playing) t.stopRecording(origin + 900); else t.stopRecordingToStopped(origin + 900);
  TEST_ASSERT_EQUAL(768, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(0, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(playing ? TRACK_PLAYING : TRACK_STOPPED, t.getState());
  assertRecordRows(loop, expected);
}
void test_record_normalization_stopped_inventory() { recordNormalizationInventory(false, true); }
void test_record_normalization_playing_inventory() { recordNormalizationInventory(true, true); }
void test_record_normalization_disabled_minimum_inventory() { recordNormalizationInventory(false, false); }

void test_record_normalization_preserves_take_larger_than_overdub_staging() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(768);
  noteMinLengthRemoveEnabled = false;
  constexpr size_t pairs = RuntimePlaybackViewConfig::kCaptureStagingEvents / 2 + 4;
  static_assert(pairs * 2 > RuntimePlaybackViewConfig::kCaptureStagingEvents, "cross staging envelope");
  SessionMidiEventVec expected;
  for (size_t i = 0; i < pairs; ++i) {
    const uint32_t on = static_cast<uint32_t>(i * 48 + 12);
    const auto channel = static_cast<uint8_t>(1 + (i / 128) % 16);
    const auto note = static_cast<uint8_t>(i % 128);
    t.noteOn(channel, note, 100, 768 + on);
    const NoteId id = TrackCaptureStopTestAccess::recordIdentity(t, channel, note);
    TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, id);
    t.noteOff(channel, note, 0, 768 + on + 24);
    auto a = MidiEvent::NoteOn(on, channel, note, 100); a.noteId = id;
    auto b = MidiEvent::NoteOff(on + 24, channel, note, 0); b.noteId = id;
    expected.push_back(a); expected.push_back(b);
  }
  TEST_ASSERT_EQUAL(pairs * 2, t.getLoop(0).capture.store.size());
  const uint32_t length = static_cast<uint32_t>(((pairs * 48 + 767) / 768) * 768);
  clockManager.assignCurrentTickSilently(768 + length);
  t.stopRecordingToStopped(768 + length);
  TEST_ASSERT_EQUAL(length, t.getLoop(0).loopLengthTicks);
  assertRecordRows(t.getLoop(0), expected);
}

void test_record_normalization_filtered_empty_resets_geometry() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(768);
  noteMinLengthTicks = 48; noteMinLengthRemoveEnabled = true;
  t.noteOn(1, 60, 100, 780); t.noteOff(1, 60, 0, 800);
  TEST_ASSERT_EQUAL(2, t.getLoop(0).capture.store.size());
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecordingToStopped(1536);
  TEST_ASSERT_FALSE(t.getLoop(0).passes.hasRecordPass());
  TEST_ASSERT_EQUAL(0, t.getLoop(0).loopLengthTicks);
  TEST_ASSERT_EQUAL(0, t.getLoop(0).capture.store.size());
  TEST_ASSERT_EQUAL(TRACK_EMPTY, t.getState());
}

void test_job_pass_staging_yields_and_preserves_successor() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, true); prepareSlot(t, 1, false);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  for (unsigned i = 0; i < 140; ++i) {
    const uint8_t channel = 2 + i / 128, note = i % 128;
    t.noteOn(channel, note, 100, 12); t.noteOff(channel, note, 0, 96);
  }
  stop(t, true, 768);
  advanceToPhase(t, PendingStopPhase::PrepareTransfer);
  auto& source = TrackCaptureStopTestAccess::output(t);
  const size_t chunks = source.store.chunkIds().size();
  TEST_ASSERT_GREATER_THAN(1, chunks);
  const size_t rows = source.store.size();
  TEST_ASSERT_EQUAL(282, rows);
  TrackCaptureStopTestAccess::step(t); // scalar admission
  auto& pass = TrackCaptureStopTestAccess::pass(t);
  const PassId id = pass.id;
  TEST_ASSERT_TRUE(pass.preparationPending);
  TEST_ASSERT_TRUE(pass.committedChunkIds.empty());
  TEST_ASSERT_EQUAL(rows, source.store.size());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasReadablePass(t));
  TEST_ASSERT_FALSE(t.getLoop(0).hasPendingCapturePass());
  start(t, 1, false, 768, 72);
  const auto successor = TrackCaptureStopTestAccess::interval(t);
  for (size_t grain = 0; grain < chunks + chunks / 2; ++grain) {
    const auto before = source.store.chunkIds().size();
    const auto moved = pass.committedChunkIds.size();
    TEST_ASSERT_TRUE(pass.preparationPending);
    PreparedCapturePassTransfer transfer;
    TEST_ASSERT_FALSE(t.getLoop(0).transferCapturePass(pass, transfer));
    assertNoPersistenceRequest();
    TrackCaptureStopTestAccess::step(t);
    TEST_ASSERT_EQUAL(id, pass.id);
    TEST_ASSERT_EQUAL(before ? before - 1 : 0, source.store.chunkIds().size());
    TEST_ASSERT_EQUAL(moved + (before ? 1 : 0), pass.committedChunkIds.size());
    TEST_ASSERT_EQUAL(successor, TrackCaptureStopTestAccess::interval(t));
    TEST_ASSERT_TRUE(t.isRecording());
    TEST_ASSERT_EQUAL(2, t.getLoop(1).capture.store.size());
  }
  TEST_ASSERT_FALSE(pass.preparationPending);
  TEST_ASSERT_EQUAL(CapturePhase::None, source.phase);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasReadablePass(t));
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalTail, TrackCaptureStopTestAccess::phase(t));
  advanceToPhase(t, PendingStopPhase::UndoCollection);
  TrackCaptureStopTestAccess::assertDeniedUndoRetainsObligation(t);
  TrackCaptureStopTestAccess::assertDeniedUndoPublicationRetries(t);
  assertNoPersistenceRequest();
  advanceToPhase(t, PendingStopPhase::DeferredCleanup);
  TEST_ASSERT_EQUAL(1, t.getLoop(0).passes.overdubPasses.size());
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(t.getLoop(0).passes.overdubPasses[0].committedChunkIds, committed);
  TEST_ASSERT_EQUAL(rows, committed.size());
  for (unsigned i = 0; i < 140; ++i) {
    unsigned ons = 0, offs = 0;
    for (const auto& event : committed) {
      if (event.channel != 2 + i / 128 || event.data.noteData.note != i % 128) continue;
      if (event.type == midi::NoteOn) { ++ons; TEST_ASSERT_EQUAL(12, event.tick); }
      if (event.type == midi::NoteOff) { ++offs; TEST_ASSERT_EQUAL(96, event.tick); }
    }
    TEST_ASSERT_EQUAL(1, ons); TEST_ASSERT_EQUAL(1, offs);
  }
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][1]);
  const auto budget = TrackCaptureStopTestAccess::budget(t);
  for (uint64_t i = 0; i < budget && TrackCaptureStopTestAccess::jobs(t); ++i)
    TrackCaptureStopTestAccess::step(t);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::jobs(t));
  TrackCaptureStopTestAccess::assertRetiredCursors(t);
  const auto persisted = PersistenceWorkQueue::queueDepth();
  const auto undoCount = t.getGlobalUndoStack().undoCount();
  TEST_ASSERT_EQUAL(1, undoCount);
  for (unsigned i = 0; i < 10; ++i) TrackCaptureStopTestAccess::step(t);
  TEST_ASSERT_EQUAL(persisted, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_EQUAL(undoCount, t.getGlobalUndoStack().undoCount());
  TEST_ASSERT_EQUAL(1, t.getLoop(0).passes.overdubPasses.size());
  TEST_ASSERT_TRUE(t.isRecording());
  TEST_ASSERT_EQUAL(2, t.getLoop(1).capture.store.size());
}

void test_job_pass_staging_denial_and_receipt_binding() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, true);
  auto& loop = t.getLoop(0);
  Capture source; source.phase = CapturePhase::Overdub;
  TEST_ASSERT_TRUE(source.store.append(MidiEvent::ControlChange(1, 1, 1, 64)));
  PendingCapturePass pass, other;
  LoopEventStoreCommitCursor cursor;
  uint64_t generation = 4;
  TEST_ASSERT_EQUAL(SealOutcome::AlreadyPending,
    loop.beginPreparedCapturePassFromActiveAdmission(loop.capture, pass, cursor, generation, 768));
  TEST_ASSERT_EQUAL(SealOutcome::FailedValidation,
    loop.beginPreparedCapturePassFromActiveAdmission(source, pass, cursor, generation, 768));
  TEST_ASSERT_TRUE(loop.hasActiveOverdubTransferAdmission());
  TEST_ASSERT_EQUAL(kInvalidPassId, pass.id);
  TEST_ASSERT_EQUAL(1, source.store.size());
  TEST_ASSERT_TRUE(source.store.prewarmCaptureDirectory());
  TEST_ASSERT_EQUAL(SealOutcome::Ok,
    loop.beginPreparedCapturePassFromActiveAdmission(source, pass, cursor, generation, 768));
  const auto id = pass.id;
  TEST_ASSERT_EQUAL(SealOutcome::AlreadyPending,
    loop.beginPreparedCapturePassFromActiveAdmission(source, pass, cursor, generation, 768));
  other.id = id;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
    loop.stepPreparedCapturePass(source, other, cursor, 1));
  TEST_ASSERT_TRUE(pass.preparationPending);
  ++generation;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
    loop.stepPreparedCapturePass(source, pass, cursor, 1));
  TEST_ASSERT_EQUAL(1, source.store.size());
  TEST_ASSERT_TRUE(pass.committedChunkIds.empty());
  TEST_ASSERT_FALSE(loop.canTransferCapturePassWithoutAllocation(pass));
  source.store.clear();
}

int main() {
  UNITY_BEGIN();
      RUN_TEST(test_record_normalization_orders_and_identity_completes_the_merged_store);
RUN_TEST(test_record_source_staging_merges_the_frozen_take_after_closure);
RUN_TEST(test_job_pass_staging_yields_and_preserves_successor);
  RUN_TEST(test_job_pass_staging_denial_and_receipt_binding);
  RUN_TEST(test_record_normalization_stopped_inventory);
  RUN_TEST(test_record_normalization_playing_inventory);
  RUN_TEST(test_record_normalization_disabled_minimum_inventory);
  RUN_TEST(test_record_normalization_preserves_take_larger_than_overdub_staging);
  RUN_TEST(test_record_normalization_filtered_empty_resets_geometry);
  RUN_TEST(test_record_job_closure_rejects_reused_raw_and_replaced_output);
  RUN_TEST(test_record_job_closures_exceed_overdub_hold_limit_without_staging);
  RUN_TEST(test_record_job_without_held_notes_emits_no_closures);
  RUN_TEST(test_record_publication_preflight_denies_before_the_irreversible_commit);
  RUN_TEST(test_record_pass_transfer_continues_after_the_store_empties);
  RUN_TEST(test_record_pass_transfer_capacity_refusal_fails_closed);
  RUN_TEST(test_empty_record_take_all_dropped_retires_without_content);
  RUN_TEST(test_empty_record_take_all_filtered_retires_without_content);
  RUN_TEST(test_deferred_record_interval_is_audible_only_with_a_bound_window);
  RUN_TEST(test_private_grains_never_hold_the_global_ephemeral_seal);
  RUN_TEST(test_record_job_closures_preserve_a_live_successor);
  RUN_TEST(test_record_job_closure_denial_retains_obligation_for_retry);
  RUN_TEST(test_record_stop_closure_does_not_search_reused_producer);
  RUN_TEST(test_record_stop_commits_exact_held_note_closures);
  RUN_TEST(test_record_identity_requires_accepted_input_and_handles_preroll);
  RUN_TEST(test_record_closures_keep_identity_after_raw_source_freezes);
  RUN_TEST(test_deferred_publication_requests_own_slot_once);
  RUN_TEST(test_stopped_publication_requests_own_slot_once);
  RUN_TEST(test_superseded_publication_requests_own_slot_once);
  RUN_TEST(test_stale_publication_waits_before_persistence);
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
