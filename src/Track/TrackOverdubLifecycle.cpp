//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "StorageManager.h"
#include "TickPhase.h"
#include "TrackUndo.h"
#include "TrackUndoPublication.h"
#include "TrackPreparedStopCommit.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/RuntimeTimingTelemetry.h"

namespace {

constexpr uint16_t kDeferredStopOrderQuantum = 32;
constexpr StopAdmissionContract kDeferredStopContract{
    PendingStopAdmission::kMaximumClosureGrains,
    3072,  // 2x fixed orders + rewrite/trim upper bound for 2,048 staging rows at 32 entries.
    1,
    kDeferredStopOrderQuantum,
};

constexpr uint16_t kDeferredStopPreparationQuantum = 32;
/// Bounded cold reservation for the protected undo publication. Deliberately far
/// below Config::ABSOLUTE_MAX_UNDO_ENTRIES: the eager per-track 512-entry reserve
/// broke device boot (see prewarmProtectedUndo).
constexpr uint16_t kProtectedUndoPrewarmEntries = 32;
/// Bounded cold reservation for one job's undo-collection output lists.
/// `PassIdList` / `EditPassIdList` are InternalHeapFirstAllocator (EditPass.h), so
/// reserving RuntimePlaybackViewConfig::kRecordsPerBridge (2048) for all four ring
/// entries across eight tracks demands about 512 KB of the ~340 KB internal heap
/// and aborted boot (captures/task23-recover-verify, 2026-09-16). A session longer
/// than this bound simply publishes no prepared undo entry and takes the plain
/// commit.
constexpr size_t kUndoCollectionReserveRows = 64;

}  // namespace

TRACK_COLD_MEM void Track::CaptureStopJobsDeleter::operator()(CaptureStopJobs* jobs) const {
  if (!jobs) return;
  jobs->~CaptureStopJobs();
  ExternalMemoryFirstAllocator<CaptureStopJobs>{}.deallocate(jobs, 1);
}

TRACK_COLD_MEM bool Track::prewarmCaptureStopJobs() {
  if (!captureStopJobs_) {
    auto* storage = ExternalMemoryFirstAllocator<CaptureStopJobs>{}.allocate(1);
    if (!storage) return false;
    captureStopJobs_.reset(new (storage) CaptureStopJobs{});
  }
  // Cold admission of every descriptor's sealed-range destination directory.
  // A descriptor that already owns a sealed extent keeps it; a released one is
  // re-admitted here so the action edge never reserves storage.
  for (size_t index = 0; index < CaptureStopJobs::kCapacity; ++index) {
    CaptureStopJob& entry = captureStopJobs_->entries[index];
    if (!entry.rawRange.ready() && !entry.rawRange.reserveTail()) return false;
    // The bounded undo collection requires caller-supplied, pre-reserved private
    // outputs; it never grows them itself (a growth would be a denied collection).
    const size_t rows = std::min<size_t>(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                         kUndoCollectionReserveRows);
    if (entry.undoPassIds.capacity() < rows) entry.undoPassIds.reserve(rows);
    if (entry.undoCompanionIds.capacity() < rows) entry.undoCompanionIds.reserve(rows);
  }
  return prewarmProtectedUndo();
}

TRACK_COLD_MEM bool Track::retireJobCaptureSourceGrain(CaptureStopJob& job, Loop& loop,
                                                       bool closeCapture) {  if (!job.ownsRawRange) {
    // Legacy producer path: this job never took ownership of a sealed extent.
    return loop.retireCopiedCaptureSourceGrain(closeCapture);
  }
  const bool complete = job.rawRange.releaseGrain(1);
  if (!complete) return false;
  // The owned extent is fully released. Drop the Loop's read borrow only while it
  // still identifies this exact job, so a stale job can never unbind a successor.
  (void)loop.releaseSealedCaptureRangeBorrow(&job.admission);
  job.ownsRawRange = false;
  // Re-arm this descriptor's reserved destination directory for a future
  // admission. Bounded cold work in a deferred turn (one pool chunk plus the
  // retained directory); if it fails, the next admission denies explicitly
  // instead of reserving at the action edge.
  (void)job.rawRange.reserveTail();
  if (closeCapture) loop.closeProducerCapturePhase();
  return true;
}

TRACK_COLD_MEM Track::CaptureStopJob* Track::captureStopJobHead() {
  if (!captureStopJobs_ || captureStopJobs_->count == 0) return nullptr;
  return &captureStopJobs_->entries[captureStopJobs_->head];
}

TRACK_COLD_MEM const Track::CaptureStopJob* Track::captureStopJobHead() const {
  if (!captureStopJobs_ || captureStopJobs_->count == 0) return nullptr;
  return &captureStopJobs_->entries[captureStopJobs_->head];
}

TRACK_COLD_MEM Track::CaptureStopJob* Track::captureStopJobTail() {
  if (!captureStopJobs_) return nullptr;
  if (captureStopJobs_->count >= CaptureStopJobs::kCapacity) return nullptr;
  const size_t tail =
      (captureStopJobs_->head + captureStopJobs_->count) % CaptureStopJobs::kCapacity;
  return &captureStopJobs_->entries[tail];
}

TRACK_COLD_MEM Track::CaptureStopJob& Track::captureStopJob() {
  return captureStopJobs_->entries[captureStopJobs_->head];
}

TRACK_COLD_MEM const Track::CaptureStopJob& Track::captureStopJob() const {
  return captureStopJobs_->entries[captureStopJobs_->head];
}

#if defined(SESSION_CAPTURE)
TRACK_COLD_MEM void Track::printStoppedCaptureContentForHitl() {
  if (clockManager.isTransportRunning() || hasPendingDeferredOverdubStop() ||
      !loopsAllocated() || isRecording() || isOverdubbing()) {
    Serial.println("CAPTURE_CONTENT,denied");
    return;
  }
  const auto& loop = getActiveLoop();
  Serial.printf("CAPTURE_CONTENT,begin,track=%u,slot=%u,length=%lu\r\n",
      midiChannel, activeLoopIndex, static_cast<unsigned long>(loop.loopLengthTicks));
  SessionMidiEventVec rows;
  auto printRows = [&rows](const char* kind, unsigned pass) {
    Serial.printf("CAPTURE_CONTENT,rows,%s,%u,%u\r\n", kind, pass,
                  static_cast<unsigned>(rows.size()));
    for (const auto& e : rows) {
      Serial.printf("CAPTURE_CONTENT,event,%s,%u,%lu,%u,%u,%u,%u,%lu\r\n",
          kind, pass, static_cast<unsigned long>(e.tick), static_cast<unsigned>(e.type),
          e.channel, e.data.noteData.note, e.data.noteData.velocity,
          static_cast<unsigned long>(e.noteId));
    }
  };
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  printRows("record", 0);
  for (size_t i = 0; i < loop.passes.overdubPasses.size(); ++i) {
    rows.clear();
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses[i].committedChunkIds, rows);
    printRows("overdub", static_cast<unsigned>(i));
  }
  loop.passes.materializeToEventVector(rows, loop.loopLengthTicks);
  printRows("effective", 0);
  Serial.println("CAPTURE_CONTENT,end");
}

TRACK_COLD_MEM void Track::printStoppedServiceStatusForHitl() {
  if (clockManager.isTransportRunning() || !captureStopJobs_) return;
  const auto& stop = captureStopJob().admission;
  const auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  Serial.printf("#STOP_OUTPUT,track=%u,slot=%u,muted=%u,audible=%u,enabled=%u,slot_muted=%u,barrier=%u,tick=%lu,last=%lu\r\n",
      trackIndex, activeLoopIndex, isMuted(), trackManager.isTrackAudible(trackIndex),
      trackManager.isSlotEnabled(trackIndex, activeLoopIndex),
      trackManager.isSlotMuted(trackIndex, activeLoopIndex), transportStopOutputBarrier_,
      static_cast<unsigned long>(clockManager.getCurrentTick()),
      static_cast<unsigned long>(loopsAllocated() ? getActiveLoop().lastTickInLoop : UINT32_MAX));
  Serial.printf("#STOP_STATUS,us=%lu,state=%s,pending=%u,phase=%u,dest=%u,order=%llu,ranked=%llu,canonical=%llu,prefix=%u,tail=%u,bridge=%u,wrap=%u,queue=%u/%u\r\n",
      static_cast<unsigned long>(micros()), getStateName(getState()), stop.pending,
      static_cast<unsigned>(stop.phase), static_cast<unsigned>(stop.destination),
      static_cast<unsigned long long>(stop.remainingOrderGrains),
      static_cast<unsigned long long>(stop.remainingRankedPreparationGrains),
      static_cast<unsigned long long>(stop.remainingCanonicalPreparationGrains),
      runtime ? static_cast<unsigned>(runtime->canonicalPrefixPhase) : 255u,
      runtime ? static_cast<unsigned>(runtime->canonicalTailPhase) : 255u,
      runtime ? static_cast<unsigned>(runtime->captureBridgePhase) : 255u,
      loopsAllocated() && getActiveLoop().hasDeferredVisualCacheWrapAppend(),
      static_cast<unsigned>(captureStopQueueDepth()),
      static_cast<unsigned>(captureStopQueueCapacity()));
  if (loopsAllocated() && runtime) {
    const auto* set = runtime->viewSetIfPrewarmed();
    Serial.printf("#STOP_PREFIX,phase=%u,window=%u,attached=%u,inputs=%u,apply=%u,passes=%u,build_active=%u,index=%u,current=%u,ready=%u,build=%u,retained=%u\r\n",
        static_cast<unsigned>(runtime->canonicalPrefixPhase), static_cast<unsigned>(runtime->canonicalWindowPhase),
        runtime->canonicalPassInputs.captureAttached(), runtime->canonicalPassInputs.ready(),
        runtime->frozenCompanionApply.available(), runtime->frozenCompanionPasses.idle(), runtime->viewBuild.active(),
        runtime->canonicalWindowIndex, set ? set->lifecycle.current : 255u,
        set ? set->lifecycle.ready : 255u, set ? set->lifecycle.build : 255u,
        set ? set->lifecycle.retained : 255u);
    const auto& loop = getActiveLoop();
    const auto& work = loop.preparedCompanionHistory_;
    Serial.printf("#STOP_HISTORY,phase=%u,storage=%llu,current=%llu,history=%u,current_history=%u,rows=%u,prewarmed=%u,pass=%u,chunks=%u\r\n",
        static_cast<unsigned>(work.phase),
        static_cast<unsigned long long>(work.storageGeneration),
        static_cast<unsigned long long>(loop.canonicalStorageGeneration),
        static_cast<unsigned>(work.historyCount), static_cast<unsigned>(loop.passes.editPasses.size()),
        static_cast<unsigned>(runtime->frozenCaptureCompanions.size()),
        loop.preparedCompanionHistoryPrewarmed(runtime->frozenCaptureCompanions.size()),
        loop.hasPendingCapturePass(), static_cast<unsigned>(loop.pendingCapturePass_.committedChunkIds.size()));
  }
}
#endif

TRACK_COLD_MEM bool Track::commitPreparedOverdubStopBridge(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred) {
  return TrackPreparedStopCommit::commit(loop, runtime, stop, generation, cursor, transferred);
}

TRACK_COLD_MEM bool Track::beginDeferredOverdubStop(PendingStopDestination destination,
                                                     uint32_t currentTick) {
  if (!captureStopJobs_ || !isOverdubbing() || hasPendingDeferredOverdubStop()) {
    return false;
  }
  // The ring is the FIFO admission authority. A new stop interval always takes
  // the tail descriptor; a full ring is an explicit exhaustion denial, never
  // ordinary finalization latency.
  CaptureStopJob* admission = captureStopJobTail();
  if (admission == nullptr || !admission->pendingNotes.empty()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       admission != nullptr, captureStopQueueDepth(), "queue_full");
    return false;
  }
  CaptureStopJob& job = *admission;
  Loop& loop = getActiveLoop();
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  // A selected bridge is intentionally retained across PLAYING so it can
  // stream the just-published pass. Its completed build workers are no longer
  // an active stop transaction, however. Retire those workers before admitting
  // a later record/overdub-button stop; the selected bridge survives discard.
  if (runtime != nullptr &&
      runtime->captureBridgePhase == LoopPlaybackRuntime::CaptureBridgePhase::Complete) {
    runtime->discardFrozenCaptureBridge();
  }
  CaptureAdmissionStaging* views =
      runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
  if (runtime == nullptr || views == nullptr) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       views != nullptr, loop.hasActiveOverdubTransferAdmission(), "storage_denied");
    return false;
  }
  // DEC-067: this is the admission boundary that owns the finalization resources.
  // They are checked here, and not at the interval's START, because none of them is
  // used before this edge. Each denial is explicit and non-destructive: no stop
  // descriptor is taken, and the interval stays the live recording interval.
  if (runtime->captureStopOwner != nullptr) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1,
                       loop.hasActiveOverdubTransferAdmission(), "finalizer_busy");
    return false;
  }
  if (!loop.hasActiveOverdubTransferAdmission()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "transfer_denied");
    return false;
  }
  if (!runtime->canonicalCaptureOutputAdmitted()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "canonical_output_busy");
    return false;
  }
  if (!loop.canHandoffSessionExitSnapshot()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "session_exit_handoff_full");
    return false;
  }
  CaptureAdmissionStaging& staging = *views;
  const size_t openReservations = staging.openReservationCount();
  const size_t finalFrozenCount = staging.storage.eventCount + openReservations;
  const uint32_t generation = staging.captureGeneration;
  const size_t pendingChangeCount = std::min(
      loop.pendingNoteChanges().size(), RuntimePlaybackViewConfig::kRecordsPerBridge);
  const size_t historyCount = std::min(
      loop.passes.editPasses.size(), RuntimePlaybackViewConfig::kRecordsPerBridge);
  const size_t snapshotCount = loop.pendingSnapshotCompanionIds() == nullptr
      ? 0 : loop.pendingSnapshotCompanionIds()->size();
  const size_t captureChunks = finalFrozenCount == 0 ? 1 :
      1 + (finalFrozenCount - 1) / LoopEventStoreConfig::CHUNK_CAPACITY;
  const bool stoppedDestination = destination == PendingStopDestination::Stopped;
  const PlaybackViewSourceIdentity baseSource{
      loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  // Causal predecessor identity. Resolve and pin it here, before the raw-range
  // ownership transfer, so a later grain resolves this interval's successor
  // semantics against the source it actually succeeded. A published identity that
  // cannot be pinned must deny admission rather than be recorded as a dangling
  // pointer: nothing after the transfer may fail.
  ImmutableLoopSnapshotRef causalBaseSnapshot;
  if (baseSource.snapshotIdentity) {
    causalBaseSnapshot = loop.publishedSnapshot();
    if (!causalBaseSnapshot ||
        static_cast<const void*>(causalBaseSnapshot.get()) != baseSource.snapshotIdentity) {
      SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                         finalFrozenCount, openReservations, "causal_base_unavailable");
      return false;
    }
  }
  // Rolling emission windows do not describe the canonical candidate horizon.
  // Use the prewarmed owner, even when the last capture contains only one event.
  uint64_t rankedCredits = stoppedDestination || finalFrozenCount == 0 ? 0 :
      runtime->requiredStopRankedGrains(baseSource,
          std::min(loop.passes.overdubPasses.size() + 2,
                   RuntimePlaybackViewConfig::kRecordsPerBridge),
          historyCount, pendingChangeCount, kDeferredStopPreparationQuantum);
  if (rankedCredits != UINT64_MAX && rankedCredits &&
      runtime->selectedFullLoopBridge(baseSource, loop.loopLengthTicks)) {
    rankedCredits += RuntimePlaybackBridgeBuild::requiredRetainedClosureGrains(
        ActiveNoteLedger::kMaxOpenNotes, kDeferredStopPreparationQuantum);
  }
  const uint64_t canonicalRequired = stoppedDestination || finalFrozenCount == 0 ? 0 :
      Loop::requiredPreparedCompanionHistoryGrains(
          historyCount, 0, pendingChangeCount, kDeferredStopPreparationQuantum,
          snapshotCount, finalFrozenCount, captureChunks);
  const uint64_t canonicalCredits = canonicalRequired == UINT64_MAX ? UINT64_MAX :
      canonicalRequired + 1;
  const uint64_t recycleRequired = stoppedDestination || finalFrozenCount == 0 ? 0 :
      Loop::requiredPreparedCompanionRecycleGrains(
          historyCount + pendingChangeCount, 0, pendingChangeCount, snapshotCount,
          kDeferredStopPreparationQuantum);
  const uint64_t recycleCredits = recycleRequired == UINT64_MAX ? UINT64_MAX :
      recycleRequired + 1;
  // Bounded undo-identity collection for the prepared PLAYING route. The STOPPED
  // and empty routes skip companion preparation, so they carry no such estimate.
  const uint64_t undoCollectRequired = stoppedDestination || finalFrozenCount == 0 ? 0 :
      Loop::requiredSessionStopUndoCollectionGrains(
          std::min(loop.passes.overdubPasses.size() + 2,
                   RuntimePlaybackViewConfig::kRecordsPerBridge),
          pendingChangeCount, snapshotCount, kDeferredStopPreparationQuantum);
  const uint64_t undoCollectionCredits =
      undoCollectRequired == UINT64_MAX ? UINT64_MAX : undoCollectRequired;
  if (rankedCredits == UINT64_MAX || canonicalCredits == UINT64_MAX ||
      recycleCredits == UINT64_MAX || undoCollectionCredits == UINT64_MAX) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       rankedCredits, canonicalCredits, "preparation_service_unavailable");
    return false;
  }
  // Prove the raw ownership transfer before any admission mutation. A denied
  // admission must perform no seal and no transfer. An empty capture owns no
  // extent and stays on the legacy producer path. The destination is a
  // *reserved* range (not a sealed one), so this checks its receivability.
  const bool hasRawExtent = !loop.capture.store.empty();
  if (hasRawExtent && !loop.capture.store.canSealCaptureRange(job.rawRange)) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       job.ownsRawRange, static_cast<uint32_t>(loop.capture.store.size()),
                       "raw_range_unavailable");
    return false;
  }
  // Pure predicate: resolve it before the seal so no fallible step follows the
  // ownership transfer.
  if (!isValidStateTransition(TRACK_OVERDUB_STOPPING)) {
    return false;
  }
  if (!job.admission.begin(generation, currentTick, 0, openReservations,
                                   finalFrozenCount, kDeferredStopContract,
                                   rankedCredits, canonicalCredits, recycleCredits,
                                   undoCollectionCredits) ||
      !staging.freeze(generation)) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       finalFrozenCount, openReservations, "freeze_or_contract_denied");
    job.admission.reset();
    return false;
  }
  // The live role now holds the job's immutable frozen rows; the previous
  // frozen role becomes clean ingress ownership for the next interval. This is
  // a constant-work index exchange: neither staging is copied or shared with
  // the other, and the job's reserved ranges stay intact.
  runtime->exchangeCaptureStagingRoles();
  // There is no closure grain when every captured NoteOn already has its
  // matching NoteOff. Advance directly to canonical ordering; otherwise the
  // deferred owner would remain in ReservedClosure forever waiting for a
  // reservation that cannot exist (the clean normal-stop hang).
  if (openReservations == 0) {
    job.admission.setPhase(PendingStopPhase::CanonicalPrefix);
  }
  job.admission.destination = destination;
  job.owner = &loop;
  runtime->captureStopOwner = &job.admission;
  // DEC-067: the predecessor now owns the frozen role and the live role is clean, so a
  // successor interval may start even though this job is still pending. The human log line
  // is the *timely* edge for the HITL probe (`#CAP` DIAG rows queue behind the deferred
  // stop's flood and arrive seconds late); the DIAG row is the evidence-tier record.
  logger.log(CAT_TRACK, LOG_INFO, "Reentry window open slot=%u",
             static_cast<unsigned>(activeLoopIndex));
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::successorStartWindowOpen(activeLoopIndex, currentTick,
                                                loop.loopLengthTicks);
#endif
  job.loopId = loop.loopId;
  job.slot = activeLoopIndex;
  job.phaseTick = capturePhaseTick(currentTick);
  job.loopLengthTicks = loop.loopLengthTicks;
  job.interval = captureInterval_;
  job.causalBase = baseSource;
  job.causalBaseSnapshot = std::move(causalBaseSnapshot);
  if (!job.causalBase.snapshotIdentity) job.causalBaseSnapshot = {};
  job.causalBaseRecorded = true;
  job.pendingNotes.swap(pendingNotes);
  ++captureStopJobs_->count;
  // Ownership transfer. The precheck proved this cut and nothing between it and
  // here mutates the producer or the destination directory, so admission has no
  // fallible step left. If the cut still fails, the job stays on the legacy
  // producer path instead of publishing an interval without its owned input.
  if (hasRawExtent && loop.capture.store.sealCaptureRange(job.rawRange)) {
    job.ownsRawRange = true;
    (void)loop.borrowSealedCaptureRange(job.rawRange, &job.admission);
  } else {
    job.ownsRawRange = false;
    if (hasRawExtent) {
      SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                         finalFrozenCount, openReservations, "raw_seal_unexpected");
    }
  }
  // Job-owned pending note changes (Add/Shorten/Hide intents). Constant-work
  // swap: this job resolves against its own set while the live session keeps an
  // independent, prewarmed set for a successor interval.
  loop.swapPendingNoteChanges(job.pendingChanges);
  (void)loop.borrowPendingNoteChanges(job.pendingChanges, &job.admission);
  // Job-owned capture preview: the deferred cleanup seals this interval's preview
  // even if a successor interval has already replaced the live one.
  loop.swapCapturePreview(job.preview);
  (void)loop.borrowCapturePreview(job.preview, &job.admission);
  // Job-owned overdub-session undo baseline. The successor session's
  // beginOverdubSession sets its own; without this move it would overwrite the
  // baseline this interval's OverdubPassAdded entry must record.
  if (hasOverdubSessionBeforePublished_) {
    job.beforePublished = std::move(overdubSessionBeforePublished_);
    job.hasBeforePublished = true;
    overdubSessionBeforePublished_ = {};
    hasOverdubSessionBeforePublished_ = false;
  }
  // Button A closes input at this boundary. The sealed frozen capture owns all
  // remaining canonical/companion work, while the existing prepared playback
  // window remains the active source. Publish PLAYING immediately so the
  // control surface never waits behind a resumable preparation pass.
  // `job.admission` remains the capture-admission and transport-stop
  // barrier until the joint commit and cleanup retire it.
  trackState = TRACK_OVERDUB_STOPPING;
#if RUNTIME_TIMING_ENABLED
  RuntimeTimingTelemetry::recordStopStateTransition(
      resolveTrackIndexForPersistence(*this), activeLoopIndex,
      TRACK_OVERDUBBING, TRACK_OVERDUB_STOPPING, job.admission.stopTick, micros());
#endif
  if (destination == PendingStopDestination::Playing) {
    trackState = TRACK_PLAYING;
#if RUNTIME_TIMING_ENABLED
    RuntimeTimingTelemetry::recordStopStateTransition(
        resolveTrackIndexForPersistence(*this), activeLoopIndex,
        TRACK_OVERDUB_STOPPING, TRACK_PLAYING, job.admission.stopTick, micros());
#endif
    armPlayingMidiDrainAfterOverdubStop();
  }
  return true;
}

TRACK_COLD_MEM bool Track::prewarmProtectedUndo() {
  if (!protectedUndo_) {
    auto* storage = ExternalMemoryFirstAllocator<TrackUndoPublication>{}.allocate(1);
    if (!storage) return false;
    protectedUndo_.reset(new (storage) TrackUndoPublication{});
  }
  if (!protectedUndo_->prewarm(kProtectedUndoPrewarmEntries)) return false;
  // DEC-063: the stack's own entry capacity is part of the same cold admission.
  // The atomic publish must never grow it, and `begin` rejects a transaction whose
  // cursor has reached capacity, so this is a publication precondition.
  //
  // Bounded, NOT the 512-entry policy maximum: reserving
  // Config::ABSOLUTE_MAX_UNDO_ENTRIES per track at boot (8 x ~512 UndoEntry rows,
  // ~1 MB) made the device stop streaming after `boot_reset` and report
  // `crash=1` on one boot (captures/task23-undo-crash, 2026-09-16). The policy
  // maximum needs a pressure-aware or lazily admitted strategy before it can be
  // reserved eagerly. Past this many retained entries a stop simply publishes no
  // protected entry (plain commit), which keeps the stop correct.
  GlobalUndoStack& stack = getGlobalUndoStack();
  if (stack.entries.capacity() < kProtectedUndoPrewarmEntries) {
    stack.entries.reserve(kProtectedUndoPrewarmEntries);
  }
  return true;
}

TRACK_COLD_MEM TrackUndoPublication* Track::protectedUndo() { return protectedUndo_.get(); }

TRACK_COLD_MEM void Track::ProtectedUndoDeleter::operator()(TrackUndoPublication* undo) const {
  if (!undo) return;
  undo->~TrackUndoPublication();
  ExternalMemoryFirstAllocator<TrackUndoPublication>{}.deallocate(undo, 1);
}

TRACK_COLD_MEM bool Track::stepProtectedUndoDrain() {
  TrackUndoPublication* undo = protectedUndo();
  if (undo == nullptr || !undo->protectedState()) return true;
  // Bounded: one release/reclaim/trim quantum per owner turn. The disabled-pass
  // reclaim continuation remains a separate DEC-063 item; trim pressure is
  // therefore measured without it, which can only trim at least as much as the
  // fully wired policy would.
  switch (undo->phase()) {
    case TrackUndoPublication::Phase::DetachRedo:
    case TrackUndoPublication::Phase::Ready:
      // Prepared but never published (publishability was not proven): restore the
      // detached redo so the stack is whole again, then finish.
      if (!undo->beginRestore()) return true;
      return false;
    case TrackUndoPublication::Phase::Restore:
      if (!undo->stepRestore(kDeferredStopPreparationQuantum)) return false;
      // Hand the prepared entry back before finishing: finish() refuses while a
      // private unpublished entry with a valid pass id is still held. The
      // recovered entry is discarded here because the stop already committed
      // without a protected publication.
      {
        UndoEntry recovered;
        (void)undo->recoverPreparedEntry(recovered);
      }
      (void)undo->finish();
      return true;
    case TrackUndoPublication::Phase::Published:
    case TrackUndoPublication::Phase::ReleaseRedo:
      if (undo->stepReleaseRedo(kDeferredStopPreparationQuantum)) {
        return undo->phase() == TrackUndoPublication::Phase::Complete
                   ? (undo->finish(), true)
                   : false;
      }
      return false;
    case TrackUndoPublication::Phase::AwaitReclaim:
      if (!undo->beginTrimAfterReclaim()) return true;
      return false;
    case TrackUndoPublication::Phase::Trim:
      if (undo->stepTrim(kDeferredStopPreparationQuantum)) return (undo->finish(), true);
      return false;
    case TrackUndoPublication::Phase::Complete:
      return (undo->finish(), true);
    default:
      return true;
  }
}

TRACK_COLD_MEM bool Track::stepDeferredUndoCollection(CaptureStopJob& job, Loop& loop,
                                                      PendingStopAdmission& stop) {
  if (!job.undoCollectionStarted) {
    job.undoCollectionStarted = true;
    job.undoCollectable = loop.beginCollectOverdubStopUndoPasses(
        job.undoCollection, job.undoPassIds, job.undoCompanionIds,
        loop.pendingCapturePass().id, true, job.admission.intervalToken());
    (void)stop.consumeUndoCollectionGrain();
    if (!job.undoCollectable) {
      // No prepared session identities to record for this interval.
      job.undoCollected = true;
      return true;
    }
    // Collection setup is its own admitted owner turn.
    return false;
  }
  if (job.undoCollected) return true;
  const Loop::SessionUndoCollection::Phase phase =
      loop.stepCollectOverdubSessionUndoPasses(job.undoCollection,
                                               kDeferredStopPreparationQuantum);
  (void)stop.consumeUndoCollectionGrain();
  if (phase == Loop::SessionUndoCollection::Phase::Denied) {
    job.undoCollected = true;
    return true;
  }
  if (phase != Loop::SessionUndoCollection::Phase::Complete) return false;
  // Build the entry from the job's own slot and baseline, never the live session's.
  job.undoCollected = TrackUndo::prepareOverdubSessionEntry(
      loop, job.undoCollection, job.slot, job.beforePublished, job.undoEntry);
  return job.undoCollected;
}

TRACK_COLD_MEM bool Track::stepDeferredUndoPublication(CaptureStopJob& job,
                                                       PendingStopAdmission& stop) {
  TrackUndoPublication* undo = protectedUndo();
  if (undo == nullptr) {
    job.undoPublicationReady = false;
    return true;
  }
  if (!job.undoPublicationStarted) {
    job.undoPublicationStarted = true;
    // The transaction is exclusive: a predecessor must have finished draining.
    job.undoPublicationReady = undo->begin(getGlobalUndoStack(), job.undoEntry);
    (void)stop.consumeUndoCollectionGrain();
    // A rejected begin leaves the entry intact and means "nothing to publish".
    // A successful begin still owes its prepare turns before it can publish.
    if (!job.undoPublicationReady) return true;
    return false;
  }
  if (!undo->protectedState()) {
    job.undoPublicationReady = false;
    return true;
  }
  if (undo->phase() == TrackUndoPublication::Phase::Ready) {
    job.undoPublicationReady = true;
    return true;
  }
  if (!undo->stepPrepare(kDeferredStopPreparationQuantum)) return false;
  job.undoPublicationReady = undo->phase() == TrackUndoPublication::Phase::Ready;
  return job.undoPublicationReady;
}

TRACK_COLD_MEM void Track::requestDeferredOverdubStopToStopped() {
  if (!hasPendingDeferredOverdubStop()) return;
  captureStopJob().admission.requestTransportStop();
  if (auto* runtime = playbackRuntime.slotIfAllocated(captureStopJob().slot)) {
    (void)TrackPreparedStopCommit::retargetStoppedRecovery(*runtime, captureStopJob().admission);
  }
}

TRACK_COLD_MEM bool Track::processDeferredOverdubStopGrain() {
  // Bind the FIFO head once for the whole grain. The head only advances at the
  // end of this function, so a single stable reference is the contract: no
  // intermediate step may re-resolve the ring and pick up a different job.
  CaptureStopJob* head = captureStopJobHead();
  if (head == nullptr) return false;
  CaptureStopJob& job = *head;
  auto& stop = job.admission;
  if (!stop.pending) {
    return false;
  }
  // Never reinterpret a saved job through the currently selected slot.
  if (!job.owner || !loopsAllocated() ||
      loopIdForSlot(job.slot) != job.loopId ||
      &getLoop(job.slot) != job.owner ||
      job.owner->loopLengthTicks != job.loopLengthTicks) return false;
  Loop& loop = *job.owner;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(job.slot);
  CaptureAdmissionStaging* views = runtime != nullptr ? runtime->captureStagingIfPrewarmed() : nullptr;
  if (runtime == nullptr || views == nullptr ||
      views->frozenGeneration != stop.frozenGeneration) {
    return false;
  }
  CaptureAdmissionStaging& staging = *views;
  const uint32_t phaseTick = job.phaseTick;
  // Approved superseded rule. The recorded predecessor is the *supersession
  // reference*, not the resolver: the canonical pipeline binds its owner identity
  // by address to the live Loop scalars and the ordering phases need one coherent
  // live epoch, so preparation always resolves live. Only the published revision
  // and root decide whether a successor window may still be published;
  // `sourceGeneration` is a live cache epoch that advances during finalization.
  const bool causalBaseCurrent = !job.causalBaseRecorded ||
      (job.causalBase.playbackRevision == loop.playbackRevision &&
       job.causalBase.snapshotIdentity == loop.publishedSnapshotIdentity());

  // A clean stop commonly follows a completed wrap with no successor rows.
  // There is no canonical or audible ordering work in that candidate; advance
  // directly to the admitted transfer tail instead of invoking zero-row
  // builders on the active transport path.
  if (staging.storage.eventCount == 0 &&
      (stop.phase == PendingStopPhase::CanonicalPrefix ||
       stop.phase == PendingStopPhase::CanonicalOrder)) {
    stop.setPhase(PendingStopPhase::PrepareTransfer);
    return false;
  }

  if (stop.phase == PendingStopPhase::ReservedClosure) {
    CaptureNoteReservation* reservation = staging.firstOpenReservation();
    if (reservation == nullptr) {
      if (staging.openReservationCount() == 0) {
        stop.setPhase(PendingStopPhase::CanonicalOrder);
      }
      return false;
    }
    if (!appendReservedCaptureNoteOffForOwner(loop, staging, *reservation, phaseTick)) {
      return false;
    }
    (void)stop.consumeClosureGrain();
    if (stop.remainingClosureGrains == 0) {
      stop.setPhase(
          stop.destination == PendingStopDestination::Stopped
              ? PendingStopPhase::CanonicalOrder : PendingStopPhase::CanonicalPrefix);
    }
    return false;
  }
  const PlaybackViewSourceIdentity baseSource{
      loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  const uint32_t targetRevisionDelta = runtime->frozenCaptureCompanions.ready(staging)
      ? (runtime->frozenCaptureCompanions.size() == 0 ? 1u : 2u)
      : (loop.hasPendingNoteChanges() ? 2u : 1u);
  const PlaybackViewSourceIdentity targetSource{
      loop.playbackRevision + targetRevisionDelta, playbackGeneration, nullptr};
  const RuntimeCanonicalPassInputs::CurrentSource currentSource{
      loop.playbackRevision, playbackGeneration, loop.loopLengthTicks,
      loop.canonicalStorageGeneration};
  const RuntimeCanonicalPassInputs::FrozenSource frozenSource{
      loop.playbackRevision, playbackGeneration,
      stop.transportCommandEpoch, loop.loopLengthTicks, staging};

  if (stop.phase == PendingStopPhase::CanonicalPrefix) {
    if (stop.destination == PendingStopDestination::Stopped) {
      stop.setPhase(PendingStopPhase::CanonicalOrder);
      return false;
    }
    const auto anchor = runtime->currentCycleCursor(phaseTick, baseSource.sourceGeneration);
    if (runtime->stepCanonicalBase(loop.passes, baseSource, currentSource, anchor,
                                   kDeferredStopPreparationQuantum) ==
        RuntimePlaybackOrderBuildResult::Complete)
      stop.setPhase(PendingStopPhase::CanonicalOrder);
    return false;
  }
  if (stop.phase == PendingStopPhase::CanonicalOrder) {
    if (!runtime->beginFrozenCaptureCanonicalOrder(stop.frozenGeneration)) {
      return false;
    }
    const RuntimePlaybackOrderBuildResult result =
        runtime->stepFrozenCaptureCanonicalOrder(kDeferredStopOrderQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      return false;
    }
    (void)stop.consumeOrderGrain();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      stop.setPhase(PendingStopPhase::CanonicalRewrite);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::CanonicalRewrite) {
    const LoopEventStoreRewriteResult result = runtime->stepFrozenCaptureCanonicalCopy(
        stop.frozenGeneration, kDeferredStopOrderQuantum);
    if (result == LoopEventStoreRewriteResult::Denied) {
      return false;
    }
    (void)stop.consumeOrderGrain();
    if (result == LoopEventStoreRewriteResult::Complete) {
      const size_t count = runtime->frozenCaptureCanonicalOrder.orderedEventCount();
      // The prepared output owns different physical chunks. Retire only one
      // copied raw chunk per turn; never rewrite sealed persistence input.
      if (!retireJobCaptureSourceGrain(job, loop, false)) return false;
      // An empty successor capture has no audible rows to project.  Skip the
      // zero-work audible-order builder and continue with the already-admitted
      // transfer tail; this keeps a clean stop from parking on an unnecessary
      // owner turn after canonical rewrite.
      stop.setPhase(count == 0 ? PendingStopPhase::PrepareTransfer
                                                : PendingStopPhase::AudibleOrder);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::AudibleOrder) {
    if (!runtime->beginFrozenCaptureOrder(loop.loopLengthTicks,
                                          stop.frozenGeneration)) {
      return false;
    }
    const RuntimePlaybackOrderBuildResult result =
        runtime->stepFrozenCaptureOrder(kDeferredStopOrderQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      return false;
    }
    (void)stop.consumeOrderGrain();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      stop.setPhase(PendingStopPhase::PrepareTransfer);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::PrepareTransfer) {
    // Ordering/rewrite have finished. Unused estimates are not service work.
    if (!stop.retireCompletedPreparation()) return false;
    const SealOutcome staged = runtime->canonicalCaptureOutput.store.empty()
                                   ? SealOutcome::SkippedEmpty
                                   : loop.stagePreparedCapturePassFromActiveAdmission(
                                         runtime->canonicalCaptureOutput, stop.stopTick,
                                         stop.destination == PendingStopDestination::Playing
                                             ? CommitReason::OverdubStop
                                             : CommitReason::OverdubStopToStopped);
    if (staged != SealOutcome::Ok && staged != SealOutcome::SkippedEmpty) {
      return false;
    }
    // A zero-event stop has no canonical tail or companion bridge. Keep the
    // existing bounded transfer path for that case; populated captures use
    // the prepared joint handoff below.
    if (staged == SealOutcome::SkippedEmpty ||
        stop.destination == PendingStopDestination::Stopped) {
      if (!stop.retireSkippedCompanionPreparation(
              staged == SealOutcome::SkippedEmpty)) return false;
      stop.setPhase(PendingStopPhase::AtomicTransfer);
    } else {
      // The control boundary already released the track to the current
      // prepared PLAYING window. Continue only the frozen companion work here.
      stop.setPhase(PendingStopPhase::CanonicalTail);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::CanonicalTail) {
    if (!runtime->frozenCanonicalTailReady(targetSource)) {
      if (runtime->beginFrozenCanonicalTail(frozenSource, job.pendingChanges, targetSource)) {
        return false;
      }
      const auto result = runtime->stepReservedFrozenCanonicalTail(
          stop, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      if (result != RuntimePlaybackOrderBuildResult::Complete) return false;
    }
    stop.setPhase(PendingStopPhase::CompanionHistory);
    return false;
  }
  if (stop.phase == PendingStopPhase::CompanionHistory) {
    if (!loop.preparedCompanionHistoryReady(job.admission.intervalToken())) {
      if (!loop.preparedCompanionHistoryCanStep(runtime->frozenCaptureCompanions,
                                                staging, runtime->canonicalPassInputs,
                                                job.admission.intervalToken())) {
        if (!loop.beginPreparedCompanionHistory(runtime->frozenCaptureCompanions,
                                                 staging, &runtime->canonicalPassInputs, job.admission.intervalToken())) {
          return false;
        }
        return false; // Setup is its own admitted owner turn.
      }
      const auto result = TrackPreparedStopCommit::stepPreparation(
          loop, *runtime, stop, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      return false;
    }
    if (!stop.retireCompletedPreparation()) return false;
#if defined(SESSION_CAPTURE)
    const auto* windowBase = runtime->preparedCanonicalWindow(baseSource);
    const auto* publishedBase = windowBase ? windowBase : runtime->publishedFullLoopView(baseSource, loop.loopLengthTicks);
    SC_ODUB_STOP_STAGE("bridge_base", 0, 0, UINT32_MAX, UINT32_MAX,
        publishedBase != nullptr, publishedBase && publishedBase->passOriginsReady,
        windowBase ? "prepared_window" : publishedBase ? "present" : "missing_canonical_base");
#endif
    stop.setPhase(PendingStopPhase::UndoCollection);
    return false;
  }
  if (stop.phase == PendingStopPhase::UndoCollection) {
    // Bounded, resumable collection of the session's undo identities, then the
    // prepared OverdubPassAdded entry built from this job's slot and baseline,
    // then the protected transaction's prepare (begin + detach redo).
    if (!stepDeferredUndoCollection(job, loop, stop)) return false;
    if (!stepDeferredUndoPublication(job, stop)) return false;
    stop.setPhase(PendingStopPhase::CompanionBridge);
    return false;
  }
  if (stop.phase == PendingStopPhase::CompanionBridge) {
    if (stop.destination == PendingStopDestination::Stopped) {
      // History is complete; a stopped destination has no audible bridge.
      if (!stop.retireCompletedPreparation()) return false;
      stop.setPhase(PendingStopPhase::AtomicTransfer);
      return false;
    }
    if (!causalBaseCurrent) {
      // Retire the ranked (bridge) estimate: no bridge will be built. The atomic
      // transfer then commits the accepted capture without a successor window.
      if (!stop.retireSupersededPreparation()) return false;
      stop.setPhase(PendingStopPhase::AtomicTransfer);
      return false;
    }
    const auto anchor = runtime->currentCycleCursor(
        loop.lastTickInLoop == UINT32_MAX ? phaseTick : loop.lastTickInLoop, targetSource.sourceGeneration);
    if (!runtime->canSelectFrozenCaptureBridge(
            stop.frozenGeneration,
            stop.transportCommandEpoch, loop.loopLengthTicks, anchor)) {
      if (runtime->refreshFrozenCompanionBridge(anchor)) return false;
      if (runtime->beginFrozenCompanionBridge(
              baseSource, targetSource, stop.frozenGeneration,
              stop.transportCommandEpoch, loop.loopLengthTicks, anchor)) {
        return false;
      }
      const auto result = runtime->stepReservedFrozenCaptureBridge(
          stop, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      return false;
    }
    if (!stop.retireCompletedPreparation()) return false;
    stop.setPhase(PendingStopPhase::AtomicTransfer);
    return false;
  }
  if (stop.phase == PendingStopPhase::AtomicTransfer) {
    if (!causalBaseCurrent) {
      if (!stop.retireSupersededPreparation()) return false;
      SC_ODUB_STOP_STAGE("atomic_transfer", 0, 0, UINT32_MAX, UINT32_MAX,
                         stop.remainingHistoryRecycleGrains, job.causalBase.playbackRevision,
                         "causal_base_superseded");
      PreparedCapturePassTransfer supersededTransfer{};
      TrackUndoPublication* supersededUndo = protectedUndo();
      const bool supersededPublishable = job.undoPublicationReady && supersededUndo != nullptr &&
          supersededUndo->canPublish(getGlobalUndoStack(), loop.loopId,
                                     loop.pendingCapturePass().id);
      if (!TrackPreparedStopCommit::commitSupersededCausalBase(
              loop, stop, supersededTransfer,
              supersededPublishable ? supersededUndo : nullptr))
        return false;
      // The unpublished companion preparation returns to its prewarmed horizon
      // here, so the next interval never needs a STOPPED-only re-prewarm.
      if (!loop.restoreSupersededPreparedCompanionHistory(job.admission.intervalToken())) {
        loop.discardPreparedCompanionHistory(job.admission.intervalToken());
      }
      return false;
    }
    const auto anchor = runtime->currentCycleCursor(
        loop.lastTickInLoop == UINT32_MAX ? phaseTick : loop.lastTickInLoop, targetSource.sourceGeneration);
    if (TrackPreparedStopCommit::requiresBridgeRefresh(
            loop, *runtime, stop, anchor)) {
      stop.setPhase(PendingStopPhase::CompanionBridge);
      return false;
    }
    PreparedCapturePassTransfer transferred{};
    TrackUndoPublication* undo = protectedUndo();
    // The caller must prove publishability before entering the undo path; a
    // Ready-but-unpublishable transaction is restored by the drain instead.
    const bool publishable = job.undoPublicationReady && undo != nullptr &&
        undo->canPublish(getGlobalUndoStack(), loop.loopId, loop.pendingCapturePass().id);
    if (publishable) {
      (void)TrackPreparedStopCommit::advanceAtomicTransferWithUndo(
          loop, *runtime, stop, playbackGeneration, anchor, transferred,
          getGlobalUndoStack(), *undo);
    } else {
      if (job.undoPublicationReady) {
        SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                           job.undoPublicationReady, undo != nullptr, "undo_not_publishable");
      }
      (void)TrackPreparedStopCommit::advanceAtomicTransfer(
          loop, *runtime, stop, playbackGeneration, anchor, transferred);
    }
    return false;
  }

  // Drain the protected undo transaction before final cleanup retires the job.
  // Bounded: one release/reclaim/trim quantum per owner turn, so protectedState()
  // is Idle again before the next interval can admit.
  if (!stepProtectedUndoDrain()) return false;

  if (stop.historyRecycleOwner == &loop) {
    if (!stop.historyRecycleStarted) {
      if (!TrackPreparedStopCommit::beginHistoryRecycle(loop, stop)) return false;
      return false;
    }
    const auto result = TrackPreparedStopCommit::stepHistoryRecycle(
        loop, stop, kDeferredStopPreparationQuantum);
    if (result != RuntimePlaybackOrderBuildResult::Complete) return false;
    stop.historyRecycleOwner = nullptr;
  }

  // One retired pending-note node per turn; never clear the live ingress map.
  if (!job.pendingNotes.empty()) {
    job.pendingNotes.erase(job.pendingNotes.begin());
    return false;
  }
  // Restore one consumed physical output credit per owner turn. If physical
  // storage is exhausted, finish this accepted job; the next admission must
  // explicitly deny until a later refill succeeds.
  if (runtime->replenishCanonicalCaptureChunk()) return false;
  const bool ownsTrackState = job.interval == captureInterval_ &&
      job.slot == activeLoopIndex;
  const PendingStopDestination destination = stop.destination;
  if (!TrackPreparedStopCommit::completeDeferredCleanup(loop, *runtime, stop, job.preview))
    return false;
  // Retire this job's immutable frozen admission rows now that no consumer
  // borrows them. The role then returns to clean ingress ownership on the next
  // exchange; no live ingress state is touched here.
  views->reset();
  // The deferred completion is the normal route for an overdub stop (both destinations).
  // The synchronous fallback `commitCaptureForStop(OverdubStopToStopped, ...)` requests the
  // stored-MIDI verification from `finalizeCommitSideEffects`; mirror it here so both commit
  // routes request the dump from the one place that owns the overdub-stop commit. Without
  // this the transport-stop-during-overdub path never queues the dump at all (capture
  // `sevt-diag-stop` showed zero `stored_verify` markers despite a completed stop grain).
  queueDeferredStoredMidiVerification();
  (void)loop.releasePendingNoteChangesBorrow(&job.admission);
  (void)loop.releaseCapturePreviewBorrow(&job.admission);
  // A baseline left unconsumed by the commit must not leak into a later interval.
  job.hasBeforePublished = false;
  job.beforePublished = {};
  job.causalBase = {};
  job.causalBaseSnapshot = {};
  job.causalBaseRecorded = false;
  job.owner = nullptr;
  job.loopId = kInvalidLoopId;
  captureStopJobs_->head = (captureStopJobs_->head + 1) % CaptureStopJobs::kCapacity;
  --captureStopJobs_->count;
  if (!ownsTrackState) return true;
  (void)retireJobCaptureSourceGrain(job, loop, true);
  if (destination == PendingStopDestination::Stopped) {
    silenceTrackMidiOutput();
    setState(TRACK_STOPPED);
    resetPlaybackState(clockManager.getCurrentTick());
  } else {
    // Playback continued during finalization. Keep its cursor and held-note
    // ledger so cleanup cannot truncate a note or replay earlier events.
    // Normal Button A release has already published PLAYING.
    if (!isPlaying()) {
      setState(TRACK_PLAYING);
    }
    armPlayingMidiDrainAfterOverdubStop();
  }
  return true;
}

bool Track::handleNoteEditFold(bool endInPlaying, uint32_t currentTick, uint32_t closeTick,
                               uint32_t stopStartUs) {
  (void)closeTick;
  if (!editManager.isNoteEditActive()) {
    return false;
  }
  Loop& loop = getActiveLoop();
#if defined(SESSION_CAPTURE)
  const bool probeVisualDirtyBeforeStop = loop.visualCacheDirty;
  const uint16_t probeDirtyBarsBeforeStop = countVisualCacheDirtyBars(loop);
  const bool probeSavePendingBeforeStop = StorageManager::hasDeferredSaveWork();
#endif
  finalizePendingNotes(currentTick);
  if (!loop.capture.store.empty() && loop.loopLengthTicks > 0) {
    loop.ensureCaptureEventsSorted();
    loop.assignMissingNoteIdsInStore(loop.capture.store);
    loop.finalizeCaptureWrapWindowAtStop(currentTick);
  }
  editManager.foldLiveCaptureIntoNoteEditSession(*this);
  pendingNotes.clear();
  if (endInPlaying) {
    const uint32_t stateStartUs = micros();
    silenceTrackMidiOutput();
    playbackRuntime.clearAllLedgers();
    pendingNotes.clear();
    resetPlaybackState(currentTick);
    setState(TRACK_PLAYING);
    logOverdubStopStage(loop, stopStartUs, "set_state", micros() - stateStartUs,
                        DebugSessionCapture::kUnsampledHeapBytes,
                        DebugSessionCapture::kUnsampledHeapBytes, "in_edit");
    const uint32_t flushStartUs = micros();
    SC_REC_FLUSH_PENDING_REVTS(128);
    logOverdubStopStage(loop, stopStartUs, "flush", micros() - flushStartUs,
                        DebugSessionCapture::kUnsampledHeapBytes,
                        DebugSessionCapture::kUnsampledHeapBytes, "ok");
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    logger.logTrackEvent("Overdubbing stopped", currentTick);
    logger.info("Overdub stopped (in-edit fold): events=%d, undo_entries=%d",
                static_cast<int>(loop.displayEventCountHint()), TrackUndo::getUndoCount(*this));
    const uint32_t storagePhaseTickAtStop =
        (loop.loopLengthTicks > 0)
            ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
            : 0;
    displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
    emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
    logOverdubStopStage(loop, stopStartUs, "display", 0,
                        DebugSessionCapture::kUnsampledHeapBytes,
                        DebugSessionCapture::kUnsampledHeapBytes, "hold");
#if defined(SESSION_CAPTURE)
    armOverdubStopDebtProbe(probeVisualDirtyBeforeStop, probeDirtyBarsBeforeStop,
                            probeSavePendingBeforeStop, loop);
#endif
    HotPathTelemetry::requestDeferredSummary("overdub_stop");
    armPlayingMidiDrainAfterOverdubStop();
  } else {
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    setState(TRACK_STOPPED);
    resetPlaybackState(currentTick);
    const uint32_t storagePhaseTickAtStop =
        (loop.loopLengthTicks > 0)
            ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
            : 0;
    // RC5e: same promote/preserve handoff as overdub→PLAYING (session_20260811_174742).
    displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
    displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
    logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
    HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
  }
  return true;
}
void Track::startOverdubbing(uint32_t currentTick) {
  // A pending predecessor is not by itself a reason to refuse live ingress: its
  // frozen stop candidate owns the *frozen* admission role, while this interval
  // only ever resets the *live* role below. The predecessor-pending refusal that
  // used to sit here predated the dual-role split. What still bounds reentry is
  // capacity: this start needs the live role and the frozen role is what a *stop*
  // needs, so a successor may record while the predecessor finalizes but its own
  // stop is denied until the predecessor retires (`beginDeferredOverdubStop`).
  Loop& loopRef = getActiveLoop();
  if (trackState == TRACK_OVERDUBBING && loopRef.capture.phase == CapturePhase::Overdub) {
    return;
  }
  // Capture staging is provisioned by capture admission, not by boot (DEC-065). This
  // is the overdub operation's own cold admission turn: reserve both roles *before*
  // any mutation and deny explicitly if the pool and the heap cannot supply them.
  // Reserving both keeps punch-out allocation-free (the live role is frozen while the
  // successor uses the other, already-reserved role), and the checks below are the
  // require step that runs after the reserve.
  LoopPlaybackRuntime* stagingAdmission = playbackRuntime.trySlot(activeLoopIndex);
  bool stagingAdmitted = false;
  if (stagingAdmission != nullptr) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCaptureStaging);
    stagingAdmitted = stagingAdmission->prewarmCaptureStaging();
    ExternalPoolAccounting::endTag();
  }
  if (!stagingAdmitted) {
    MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::OverdubStart);
    return;
  }
  // The companion-row reserve is derived from the slot's canonical content, which can grow
  // when a pass commits; re-grant it here so this interval's transform set cannot outgrow a
  // stale grant. Cold point only - this admission turn - never a deferred grain.
  // `ensureCapacity` only grows, so a sparse slot keeps the smaller reserve.
  bool companionRowsAdmitted = false;
  if (stagingAdmission != nullptr) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCompanionHistory);
    companionRowsAdmitted = stagingAdmission->frozenCaptureCompanions.ensureCapacity(
        loopRef.companionRowHorizonBound());
    ExternalPoolAccounting::endTag();
  }
  if (!companionRowsAdmitted) {
    MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::OverdubStart);
    return;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  CaptureAdmissionStaging* viewSet =
      runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
  if (viewSet == nullptr || !captureStopJobs_) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: capture staging or stop descriptors are not prewarmed");
    return;
  }
  // DEC-067: START owns the live interval only. The canonical copy target, the
  // active transfer admission and the session-exit receipt slot are *finalization*
  // resources -- none of them is used before this interval's own stop edge -- so
  // `beginDeferredOverdubStop` answers them, with its own denial outcome. A pending
  // predecessor may hold all three while this interval records.
  // Memory admission, and it sits before every mutation below on purpose: on
  // 2026-09-17 an admitted overdub reached `ExternalMemoryFirstAllocator::abort()`
  // about two seconds in and took the transport down with it. Exhaustion is an
  // operation-level denial, so refuse here with the loop still playing and no
  // ownership, reservation or state change to unwind. Emits `#MEM_DENIED` with the
  // operation, the reason, the requested margin and the measured availability.
  if (!MemoryMonitor::admitOperationMemory(MemoryMonitor::OperationKind::OverdubStart, 0)) {
    return;
  }
  // These two builders are per slot, not per interval: a pending predecessor is
  // mid order/rewrite on them. Only clear them when no job owns them.
  const bool stopOwnerLive =
      runtime->captureStopOwner != nullptr && runtime->captureStopOwner->pending;
  if (!hasPendingDeferredOverdubStop() && !stopOwnerLive) {
    runtime->discardFrozenCaptureOrder();
    runtime->discardFrozenCaptureCanonicalOrder();
  }
  viewSet->reset();
  viewSet->captureGeneration = playbackGeneration;
  playingMidiDrainAfterOverdubStop_ = false;
  playingMidiDrainAfterOverdubStopIdleNoted_ = false;
  overdubDrainFallbackRetryAtMs_ = 0;
  loopPrefixMeasureAfterUndo_ = false;
  loopPrefixMeasureAfterUndoNoted_ = false;
  overdubStopDebtProbeArmed_ = false;
  const uint32_t telemetryStartUs = micros();
  SC_ODUB_STAGE("enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  const Loop& active = loopRef;
  if (trackState == TRACK_EMPTY && active.loopLengthTicks > 0) {
    forceSetState(TRACK_STOPPED);
  }
  const uint32_t stateAdvanceStartUs = micros();
  if (!setState(TRACK_OVERDUBBING)) {
    SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs,
                  DebugSessionCapture::kUnsampledHeapBytes,
                  DebugSessionCapture::kUnsampledHeapBytes, "failed");
    return;
  }
  ++captureInterval_;
  transportStopOutputBarrier_ = false;
  SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs,
                DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  recordAddedNoteOnCount = 0;
  Loop& loop = getActiveLoop();
  uint32_t playheadPhase = 0;
  if (loop.loopLengthTicks > 0) {
    playheadPhase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  }
  const uint32_t captureStartUs = micros();
  loop.openOverdubSession(playheadPhase);
  loop.beginCapture(CapturePhase::Overdub, playheadPhase);
  SC_ODUB_STAGE("begin_capture", micros() - captureStartUs,
                DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  if (loop.loopLengthTicks > 0) {
    const uint32_t phase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    projectionCycleStartTick =
        static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
  const uint32_t undoStartUs = micros();
  TrackUndo::beginOverdubSession(*this);
  SC_ODUB_STAGE("undo_session", micros() - undoStartUs, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  logger.info("Overdub session opened: events=%d, undo_entries=%d",
              static_cast<int>(loop.displayEventCountHint()),
              static_cast<int>(TrackUndo::getUndoCount(*this)));
  HotPathTelemetry::recordOverdubStart(micros() - telemetryStartUs,
                                       static_cast<uint32_t>(loop.displayEventCountHint()),
                                       static_cast<uint32_t>(TrackUndo::getUndoCount(*this)));
  SC_ODUB_STAGE("complete", micros() - telemetryStartUs, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  logger.logTrackEvent("Overdubbing started", currentTick);
}


TRACK_COLD_MEM void Track::stopOverdubbing() {
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
#if defined(SESSION_CAPTURE)
  const bool probeVisualDirtyBeforeStop = loop.visualCacheDirty;
  const uint16_t probeDirtyBarsBeforeStop = countVisualCacheDirtyBars(loop);
  const bool probeSavePendingBeforeStop = StorageManager::hasDeferredSaveWork();
#endif
  const uint32_t stopStartUs = micros();
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(true, currentTick, closeTick, stopStartUs)) {
    loop.closeOverdubSession();
    return;
  }
  // Normal capture uses the frozen, generation-matched tail. The legacy path
  // remains a fault fallback until the stop owner has its explicit recovery
  // state; ordinary admitted input never reaches it.
  if (beginDeferredOverdubStop(PendingStopDestination::Playing, currentTick)) {
    return;
  }
  // Capacity denial: a predecessor still owns the sole frozen/deferred admission
  // role, so this live interval cannot freeze into a second deferred job. The
  // denial is explicit and non-destructive — the interval keeps recording with its
  // rows, reservations, pending notes and preview intact, and no stop descriptor is
  // created for it. A later stop request is admitted once the predecessor retires.
  // This is the precise capacity question the pending-deferred predicate answers.
  // A denial is only a *fault* when nothing can say why. When the finalization
  // resources this stop would need are still owned or not yet replenished, the
  // denial is a capacity answer and the interval keeps recording (DEC-067); the
  // legacy fallback below would otherwise freeze a pass against spent credits.
  auto* stopRuntime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  const bool finalizationResourcesBusy =
      stopRuntime != nullptr &&
      (stopRuntime->captureStopOwner != nullptr ||
       !stopRuntime->canonicalCaptureOutputAdmitted() ||
       !loop.hasActiveOverdubTransferAdmission() ||
       !loop.canHandoffSessionExitSnapshot());
  if (hasPendingDeferredOverdubStop() || finalizationResourcesBusy) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       captureStopQueueDepth(), 0, "successor_stop_denied_capacity");
    logger.log(CAT_TRACK, LOG_INFO,
               "Overdub stop deferred: deferred/frozen admission role is occupied");
    return;
  }
  // The admitted path above must leave the MIDI button handler without
  // formatting capture traffic. Its state edge and all stop work are emitted
  // later from the main-loop grain owner. Retain the legacy diagnostics only
  // for the fault fallback that still performs a synchronous stop here.
  logOverdubStopStage(loop, stopStartUs, "enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "fallback");
  // The fallback still runs from the MIDI button boundary, so it must not
  // perform a blocking USB drain before it can publish PLAYING.  The old
  // 128-record flush could wait indefinitely on CDC backpressure and made a
  // clean normal stop look hung.  The timing-critical budget only sheds
  // nonessential records; the idle owner drains the retained trace later.
  SC_REC_FLUSH_PENDING_REVTS(8);
  finalizePendingNotes(currentTick);
  const uint32_t sealStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::OverdubStop, currentTick, closeTick);
  // Stage order seal → finalize preserved; duration covers seal+finalize together on seal.
  logOverdubStopStage(loop, stopStartUs, "seal", micros() - sealStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      commitResultLabel(sideEffectResult));
  logOverdubStopStage(loop, stopStartUs, "finalize", 0,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      commitResultLabel(sideEffectResult));
  const uint32_t stateStartUs = micros();
  // Silence this track only, then resume loop playback. Do not CC123 every channel —
  // that mutes other playing tracks. Transport stop still uses sendAllNotesOff().
  silenceTrackMidiOutput();
  playbackRuntime.clearAllLedgers();
  pendingNotes.clear();
  reanchorPlaybackCycle(currentTick);
  setState(TRACK_PLAYING);
  logOverdubStopStage(loop, stopStartUs, "set_state", micros() - stateStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "ok");
  const uint32_t flushStartUs = micros();
  // Keep the MIDI command bounded after the state edge as well.  Retained
  // capture evidence is drained by the idle owner once PLAYING is published.
  SC_REC_FLUSH_PENDING_REVTS(8);
  logOverdubStopStage(loop, stopStartUs, "flush", micros() - flushStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "ok");
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  logger.logTrackEvent("Overdubbing stopped", currentTick);
  logger.info("Overdub stopped: events=%d, undo_entries=%d", static_cast<int>(loop.displayEventCountHint()),
              TrackUndo::getUndoCount(*this));

  const uint32_t storagePhaseTickAtStop =
      (loop.loopLengthTicks > 0)
          ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
          : 0;
  displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
  logOverdubStopStage(loop, stopStartUs, "display", 0,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "hold");
#if defined(SESSION_CAPTURE)
  armOverdubStopDebtProbe(probeVisualDirtyBeforeStop, probeDirtyBarsBeforeStop,
                          probeSavePendingBeforeStop, loop);
#endif
  HotPathTelemetry::requestDeferredSummary("overdub_stop");
  loop.closeOverdubSession();
  armPlayingMidiDrainAfterOverdubStop();
}

void Track::armPlayingMidiDrainAfterOverdubStop() {
  playingMidiDrainAfterOverdubStop_ = true;
  playingMidiDrainAfterOverdubStopIdleNoted_ = false;
}

bool Track::playingMidiDrainAfterOverdubStopActive() const {
  if (!playingMidiDrainAfterOverdubStop_ || !isPlaying() || !loopsAllocated()) {
    return false;
  }
  return getActiveLoop().visualCacheDirty || !playingMidiDrainAfterOverdubStopIdleNoted_;
}

void Track::notePlayingMidiDrainAfterOverdubStopIdle() {
  if (!playingMidiDrainAfterOverdubStop_) {
    return;
  }
  playingMidiDrainAfterOverdubStopIdleNoted_ = true;
  if (!isPlaying() || !loopsAllocated() || !getActiveLoop().visualCacheDirty) {
    playingMidiDrainAfterOverdubStop_ = false;
    overdubDrainFallbackRetryAtMs_ = 0;
  }
}

TRACK_COLD_MEM void Track::armLoopPrefixMeasureAfterUndo() {
  loopPrefixMeasureAfterUndo_ = true;
  loopPrefixMeasureAfterUndoNoted_ = false;
}

TRACK_COLD_MEM bool Track::loopPrefixMeasureAfterUndoActive() const {
  if (!loopPrefixMeasureAfterUndo_ || !isPlaying() || !loopsAllocated()) {
    return false;
  }
  return getActiveLoop().visualCacheDirty || !loopPrefixMeasureAfterUndoNoted_;
}

TRACK_COLD_MEM void Track::noteLoopPrefixMeasureAfterUndo() {
  if (!loopPrefixMeasureAfterUndo_) {
    return;
  }
  loopPrefixMeasureAfterUndoNoted_ = true;
  if (!isPlaying() || !loopsAllocated() || !getActiveLoop().visualCacheDirty) {
    loopPrefixMeasureAfterUndo_ = false;
  }
}

TRACK_COLD_MEM void Track::stopOverdubbingToStopped() {
  if (isEmpty()) return;
  suppressMidiOutputForTransportStop();
  if (hasPendingDeferredOverdubStop()) {
    // Transport Stop wins over a pending return to PLAYING. The frozen source
    // remains valid, but its continuation may resolve only to STOPPED.
    requestDeferredOverdubStopToStopped();
    return;
  }
  overdubStopDebtProbeArmed_ = false;
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  const uint32_t stopStartUs = micros();
  logOverdubStopStage(loop, stopStartUs, "enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "entered");
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(false, currentTick, closeTick, stopStartUs)) {
    loop.closeOverdubSession();
    return;
  }
  if (beginDeferredOverdubStop(PendingStopDestination::Stopped, currentTick)) {
    return;
  }
  finalizePendingNotes(currentTick);
  const uint32_t sealStartUs = micros();
  const CommitResult commitResult =
      commitCaptureForStop(CommitReason::OverdubStopToStopped, currentTick, closeTick);
  logOverdubStopStage(loop, stopStartUs, "seal", micros() - sealStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      commitResultLabel(commitResult));
  silenceTrackMidiOutput();
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  setState(TRACK_STOPPED);
  resetPlaybackState(currentTick);
  const uint32_t storagePhaseTickAtStop =
      (loop.loopLengthTicks > 0)
          ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
          : 0;
  // RC5e: adopt composed capture display before STOPPED snapshot (174742 DFRAME gap).
  displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
  logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
  HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
  loop.closeOverdubSession();
}
