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
#include "TrackPreparedStopCommit.h"
#include "Utils/DebugSessionCapture.h"
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
  }
  return true;
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
  if (runtime == nullptr || views == nullptr || runtime->captureStopOwner != nullptr ||
      !loop.hasActiveOverdubTransferAdmission()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       views != nullptr, loop.hasActiveOverdubTransferAdmission(), "storage_denied");
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
  if (rankedCredits == UINT64_MAX || canonicalCredits == UINT64_MAX ||
      recycleCredits == UINT64_MAX) {
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
                                   rankedCredits, canonicalCredits, recycleCredits) ||
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
  job.loopId = loop.loopId;
  job.slot = activeLoopIndex;
  job.phaseTick = capturePhaseTick(currentTick);
  job.loopLengthTicks = loop.loopLengthTicks;
  job.interval = captureInterval_;
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
    if (!loop.preparedCompanionHistoryReady()) {
      if (!loop.preparedCompanionHistoryCanStep(runtime->frozenCaptureCompanions,
                                                staging, runtime->canonicalPassInputs)) {
        if (!loop.beginPreparedCompanionHistory(runtime->frozenCaptureCompanions,
                                                 staging, &runtime->canonicalPassInputs)) {
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
    const auto anchor = runtime->currentCycleCursor(
        loop.lastTickInLoop == UINT32_MAX ? phaseTick : loop.lastTickInLoop, targetSource.sourceGeneration);
    if (TrackPreparedStopCommit::requiresBridgeRefresh(
            loop, *runtime, stop, anchor)) {
      stop.setPhase(PendingStopPhase::CompanionBridge);
      return false;
    }
    PreparedCapturePassTransfer transferred{};
    (void)TrackPreparedStopCommit::advanceAtomicTransfer(
        loop, *runtime, stop, playbackGeneration, anchor, transferred);
    return false;
  }

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
  // A frozen stop candidate owns capture staging until it reaches its
  // command-epoch-validated destination. This guard must sit before any
  // staging reset so direct callers cannot destroy that candidate while the
  // manager-level admission gate is bypassed.
  if (hasPendingDeferredOverdubStop()) {
    logger.log(CAT_TRACK, LOG_INFO,
               "Overdub start deferred while previous overdub finalization is pending");
    return;
  }
  Loop& loopRef = getActiveLoop();
  if (trackState == TRACK_OVERDUBBING && loopRef.capture.phase == CapturePhase::Overdub) {
    return;
  }
  // Admission storage must already exist. Do not allocate it from this active
  // transport command: setup/load/record-start prewarm owns that work.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  CaptureAdmissionStaging* viewSet =
      runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
  if (viewSet == nullptr || !captureStopJobs_) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: capture staging or stop descriptors are not prewarmed");
    return;
  }
  if (!runtime->canonicalCaptureOutputAdmitted()) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: canonical copy chunks are not reserved");
    return;
  }
  if (!loopRef.hasActiveOverdubTransferAdmission()) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: canonical transfer capacity is not prewarmed");
    return;
  }
  if (!loopRef.canHandoffSessionExitSnapshot()) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start deferred: previous session snapshot handoff is pending");
    return;
  }
  runtime->discardFrozenCaptureOrder();
  runtime->discardFrozenCaptureCanonicalOrder();
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
