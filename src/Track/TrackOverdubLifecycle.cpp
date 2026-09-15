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

#if defined(SESSION_CAPTURE)
TRACK_COLD_MEM void Track::printStoppedServiceStatusForHitl() {
  if (clockManager.isTransportRunning()) return;
  const auto& stop = pendingStopAdmission_;
  const auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  Serial.printf("#STOP_STATUS,us=%lu,state=%s,pending=%u,phase=%u,dest=%u,order=%llu,ranked=%llu,canonical=%llu,prefix=%u,tail=%u,bridge=%u,wrap=%u\r\n",
      static_cast<unsigned long>(micros()), getStateName(getState()), stop.pending,
      static_cast<unsigned>(stop.phase), static_cast<unsigned>(stop.destination),
      static_cast<unsigned long long>(stop.remainingOrderGrains),
      static_cast<unsigned long long>(stop.remainingRankedPreparationGrains),
      static_cast<unsigned long long>(stop.remainingCanonicalPreparationGrains),
      runtime ? static_cast<unsigned>(runtime->canonicalPrefixPhase) : 255u,
      runtime ? static_cast<unsigned>(runtime->canonicalTailPhase) : 255u,
      runtime ? static_cast<unsigned>(runtime->captureBridgePhase) : 255u,
      loopsAllocated() && getActiveLoop().hasDeferredVisualCacheWrapAppend());
  if (loopsAllocated() && runtime) {
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
  if (!isOverdubbing() || pendingStopAdmission_.pending) {
    return false;
  }
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
  CaptureAdmissionStaging* views = runtime != nullptr ? runtime->captureStagingIfPrewarmed() : nullptr;
  if (runtime == nullptr || views == nullptr || !loop.hasActiveOverdubTransferAdmission()) {
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
  const RuntimePlaybackView* baseView = runtime->publishedFullLoopView(
      baseSource, loop.loopLengthTicks);
  const size_t baseEventCount = baseView == nullptr
      ? finalFrozenCount : baseView->storage.eventCount;
  const size_t candidateCapacity = std::min(
      std::max(baseEventCount, finalFrozenCount), RuntimePlaybackViewConfig::kEventsPerView);
  const uint64_t rankedCredits = stoppedDestination || finalFrozenCount == 0 ? 0 :
      StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(
          candidateCapacity,
          std::min(loop.passes.overdubPasses.size() + 1,
                   RuntimePlaybackViewConfig::kRecordsPerBridge),
          baseEventCount, historyCount, pendingChangeCount,
          kDeferredStopPreparationQuantum);
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
  if (!pendingStopAdmission_.begin(generation, currentTick, 0, openReservations,
                                   finalFrozenCount, kDeferredStopContract,
                                   rankedCredits, canonicalCredits, recycleCredits) ||
      !staging.freeze(generation)) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       finalFrozenCount, openReservations, "freeze_or_contract_denied");
    pendingStopAdmission_.reset();
    return false;
  }
  // There is no closure grain when every captured NoteOn already has its
  // matching NoteOff. Advance directly to canonical ordering; otherwise the
  // deferred owner would remain in ReservedClosure forever waiting for a
  // reservation that cannot exist (the clean normal-stop hang).
  if (openReservations == 0) {
    pendingStopAdmission_.setPhase(PendingStopPhase::CanonicalPrefix);
  }
  pendingStopAdmission_.destination = destination;
  if (!isValidStateTransition(TRACK_OVERDUB_STOPPING)) {
    pendingStopAdmission_.reset();
    return false;
  }
  // Button A closes input at this boundary. The sealed frozen capture owns all
  // remaining canonical/companion work, while the existing prepared playback
  // window remains the active source. Publish PLAYING immediately so the
  // control surface never waits behind a resumable preparation pass.
  // `pendingStopAdmission_` remains the capture-admission and transport-stop
  // barrier until the joint commit and cleanup retire it.
  trackState = TRACK_OVERDUB_STOPPING;
#if RUNTIME_TIMING_ENABLED
  RuntimeTimingTelemetry::recordStopStateTransition(
      resolveTrackIndexForPersistence(*this), activeLoopIndex,
      TRACK_OVERDUBBING, TRACK_OVERDUB_STOPPING, pendingStopAdmission_.stopTick, micros());
#endif
  if (destination == PendingStopDestination::Playing) {
    trackState = TRACK_PLAYING;
#if RUNTIME_TIMING_ENABLED
    RuntimeTimingTelemetry::recordStopStateTransition(
        resolveTrackIndexForPersistence(*this), activeLoopIndex,
        TRACK_OVERDUB_STOPPING, TRACK_PLAYING, pendingStopAdmission_.stopTick, micros());
#endif
    armPlayingMidiDrainAfterOverdubStop();
  }
  return true;
}

TRACK_COLD_MEM void Track::requestDeferredOverdubStopToStopped() {
  pendingStopAdmission_.requestTransportStop();
  if (auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex)) {
    (void)TrackPreparedStopCommit::retargetStoppedRecovery(*runtime, pendingStopAdmission_);
  }
}

TRACK_COLD_MEM bool Track::processDeferredOverdubStopGrain() {
  if (!pendingStopAdmission_.pending) {
    return false;
  }
  Loop& loop = getActiveLoop();
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  CaptureAdmissionStaging* views = runtime != nullptr ? runtime->captureStagingIfPrewarmed() : nullptr;
  if (runtime == nullptr || views == nullptr ||
      views->frozenGeneration != pendingStopAdmission_.frozenGeneration) {
    return false;
  }
  CaptureAdmissionStaging& staging = *views;
  const uint32_t phaseTick = capturePhaseTick(pendingStopAdmission_.stopTick);

  // A clean stop commonly follows a completed wrap with no successor rows.
  // There is no canonical or audible ordering work in that candidate; advance
  // directly to the admitted transfer tail instead of invoking zero-row
  // builders on the active transport path.
  if (staging.storage.eventCount == 0 &&
      (pendingStopAdmission_.phase == PendingStopPhase::CanonicalPrefix ||
       pendingStopAdmission_.phase == PendingStopPhase::CanonicalOrder)) {
    pendingStopAdmission_.setPhase(PendingStopPhase::PrepareTransfer);
    return false;
  }

  if (pendingStopAdmission_.phase == PendingStopPhase::ReservedClosure) {
    CaptureNoteReservation* reservation = staging.firstOpenReservation();
    if (reservation == nullptr) {
      if (staging.openReservationCount() == 0) {
        pendingStopAdmission_.setPhase(PendingStopPhase::CanonicalOrder);
      }
      return false;
    }
    if (!appendReservedCaptureNoteOffAtPhase(*reservation, phaseTick)) {
      return false;
    }
    (void)pendingStopAdmission_.consumeClosureGrain();
    if (pendingStopAdmission_.remainingClosureGrains == 0) {
      pendingStopAdmission_.setPhase(
          pendingStopAdmission_.destination == PendingStopDestination::Stopped
              ? PendingStopPhase::CanonicalOrder : PendingStopPhase::CanonicalPrefix);
    }
    return false;
  }
  const PlaybackViewSourceIdentity baseSource{
      loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  const uint32_t targetRevisionDelta = loop.hasPendingNoteChanges() ? 2u : 1u;
  const PlaybackViewSourceIdentity targetSource{
      loop.playbackRevision + targetRevisionDelta, playbackGeneration, nullptr};
  const RuntimeCanonicalPassInputs::CurrentSource currentSource{
      loop.playbackRevision, playbackGeneration, loop.loopLengthTicks,
      loop.canonicalStorageGeneration};
  const RuntimeCanonicalPassInputs::FrozenSource frozenSource{
      loop.playbackRevision, playbackGeneration,
      pendingStopAdmission_.transportCommandEpoch, loop.loopLengthTicks, staging};

  if (pendingStopAdmission_.phase == PendingStopPhase::CanonicalPrefix) {
    if (pendingStopAdmission_.destination == PendingStopDestination::Stopped) {
      pendingStopAdmission_.setPhase(PendingStopPhase::CanonicalOrder);
      return false;
    }
    const bool prefixWasReady = runtime->canonicalPrefixPhase == LoopPlaybackRuntime::CanonicalPrefixPhase::Ready;
    const auto result = runtime->stepCanonicalPrefix(
        loop.passes, baseSource, currentSource, kDeferredStopPreparationQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      if (!prefixWasReady) return false; // Window work starts on its own owner turn.
      const auto* base = runtime->publishedFullLoopView(baseSource, loop.loopLengthTicks);
      if (!base || !base->sourceFilled || !base->passOriginsReady || !(base->passOriginSource == baseSource)) {
        const uint32_t windowLength = std::min(uint32_t(Config::TICKS_PER_BAR), loop.loopLengthTicks);
        if (!windowLength) return false;
        const PlaybackCoverageInterval coverage{phaseTick / windowLength * windowLength,
                                                windowLength, loop.loopLengthTicks};
        const EmissionCursor anchor{0, phaseTick, 0, 0, baseSource.sourceGeneration};
        if (runtime->stepCanonicalWindow(coverage, anchor, kDeferredStopPreparationQuantum) !=
            RuntimePlaybackOrderBuildResult::Complete) return false;
      }
      pendingStopAdmission_.setPhase(PendingStopPhase::CanonicalOrder);
    }
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::CanonicalOrder) {
    if (!runtime->beginFrozenCaptureCanonicalOrder(pendingStopAdmission_.frozenGeneration)) {
      return false;
    }
    const RuntimePlaybackOrderBuildResult result =
        runtime->stepFrozenCaptureCanonicalOrder(kDeferredStopOrderQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      return false;
    }
    (void)pendingStopAdmission_.consumeOrderGrain();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      pendingStopAdmission_.setPhase(PendingStopPhase::CanonicalRewrite);
    }
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::CanonicalRewrite) {
    const LoopEventStoreRewriteResult result = runtime->stepFrozenCaptureCanonicalRewrite(
        loop.capture.store, pendingStopAdmission_.frozenGeneration, kDeferredStopOrderQuantum);
    if (result == LoopEventStoreRewriteResult::Denied) {
      return false;
    }
    (void)pendingStopAdmission_.consumeOrderGrain();
    if (result == LoopEventStoreRewriteResult::Complete) {
      const size_t count = runtime->frozenCaptureCanonicalOrder.orderedEventCount();
      const uint32_t lastTick = count == 0 ? 0 : staging.storage.events[staging.storage.order[count - 1]].tick;
      loop.noteCaptureEventsCanonicalized(lastTick);
      // An empty successor capture has no audible rows to project.  Skip the
      // zero-work audible-order builder and continue with the already-admitted
      // transfer tail; this keeps a clean stop from parking on an unnecessary
      // owner turn after canonical rewrite.
      pendingStopAdmission_.setPhase(count == 0 ? PendingStopPhase::PrepareTransfer
                                                : PendingStopPhase::AudibleOrder);
    }
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::AudibleOrder) {
    if (!runtime->beginFrozenCaptureOrder(loop.loopLengthTicks,
                                          pendingStopAdmission_.frozenGeneration)) {
      return false;
    }
    const RuntimePlaybackOrderBuildResult result =
        runtime->stepFrozenCaptureOrder(kDeferredStopOrderQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      return false;
    }
    (void)pendingStopAdmission_.consumeOrderGrain();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      pendingStopAdmission_.setPhase(PendingStopPhase::PrepareTransfer);
    }
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::PrepareTransfer) {
    // Ordering/rewrite have finished. Unused estimates are not service work.
    if (!pendingStopAdmission_.retireCompletedPreparation()) return false;
    const SealOutcome staged = loop.capture.store.empty()
                                   ? SealOutcome::SkippedEmpty
                                   : loop.stagePreparedCapturePassFromActiveAdmission(
                                         pendingStopAdmission_.stopTick,
                                         pendingStopAdmission_.destination == PendingStopDestination::Playing
                                             ? CommitReason::OverdubStop
                                             : CommitReason::OverdubStopToStopped);
    if (staged != SealOutcome::Ok && staged != SealOutcome::SkippedEmpty) {
      return false;
    }
    // A zero-event stop has no canonical tail or companion bridge. Keep the
    // existing bounded transfer path for that case; populated captures use
    // the prepared joint handoff below.
    if (staged == SealOutcome::SkippedEmpty ||
        pendingStopAdmission_.destination == PendingStopDestination::Stopped) {
      if (!pendingStopAdmission_.retireSkippedCompanionPreparation(
              staged == SealOutcome::SkippedEmpty)) return false;
      pendingStopAdmission_.setPhase(PendingStopPhase::AtomicTransfer);
    } else {
      // The control boundary already released the track to the current
      // prepared PLAYING window. Continue only the frozen companion work here.
      pendingStopAdmission_.setPhase(PendingStopPhase::CanonicalTail);
    }
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::CanonicalTail) {
    if (!runtime->frozenCanonicalTailReady(targetSource)) {
      if (runtime->beginFrozenCanonicalTail(frozenSource, loop.pendingNoteChanges(), targetSource)) {
        return false;
      }
      const auto result = runtime->stepReservedFrozenCanonicalTail(
          pendingStopAdmission_, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      if (result != RuntimePlaybackOrderBuildResult::Complete) return false;
    }
    pendingStopAdmission_.setPhase(PendingStopPhase::CompanionHistory);
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::CompanionHistory) {
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
          loop, *runtime, pendingStopAdmission_, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      return false;
    }
    if (!pendingStopAdmission_.retireCompletedPreparation()) return false;
#if defined(SESSION_CAPTURE)
    const auto* windowBase = runtime->preparedCanonicalWindow(baseSource);
    const auto* publishedBase = windowBase ? windowBase : runtime->publishedFullLoopView(baseSource, loop.loopLengthTicks);
    SC_ODUB_STOP_STAGE("bridge_base", 0, 0, UINT32_MAX, UINT32_MAX,
        publishedBase != nullptr, publishedBase && publishedBase->passOriginsReady,
        windowBase ? "prepared_window" : publishedBase ? "present" : "missing_canonical_base");
#endif
    pendingStopAdmission_.setPhase(PendingStopPhase::CompanionBridge);
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::CompanionBridge) {
    if (pendingStopAdmission_.destination == PendingStopDestination::Stopped) {
      // History is complete; a stopped destination has no audible bridge.
      if (!pendingStopAdmission_.retireCompletedPreparation()) return false;
      pendingStopAdmission_.setPhase(PendingStopPhase::AtomicTransfer);
      return false;
    }
    const EmissionCursor anchor{
        0, phaseTick, 0, 0, targetSource.sourceGeneration};
    if (!runtime->canSelectFrozenCaptureBridge(
            pendingStopAdmission_.frozenGeneration,
            pendingStopAdmission_.transportCommandEpoch, loop.loopLengthTicks, anchor)) {
      if (runtime->beginFrozenCompanionBridge(
              baseSource, targetSource, pendingStopAdmission_.frozenGeneration,
              pendingStopAdmission_.transportCommandEpoch, loop.loopLengthTicks, anchor)) {
        return false;
      }
      const auto result = runtime->stepReservedFrozenCaptureBridge(
          pendingStopAdmission_, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      return false;
    }
    if (!pendingStopAdmission_.retireCompletedPreparation()) return false;
    pendingStopAdmission_.setPhase(PendingStopPhase::AtomicTransfer);
    return false;
  }
  if (pendingStopAdmission_.phase == PendingStopPhase::AtomicTransfer) {
    const EmissionCursor anchor{0, phaseTick, 0, 0, targetSource.sourceGeneration};
    PreparedCapturePassTransfer transferred{};
    if (pendingStopAdmission_.destination == PendingStopDestination::Stopped &&
        loop.preparedCompanionCaptureVerified(runtime->canonicalPassInputs)) {
      (void)TrackPreparedStopCommit::commitStopped(loop, *runtime, pendingStopAdmission_,
                                                  playbackGeneration, transferred);
      return false;
    }
    const bool prepared = loop.hasPendingCapturePass() &&
        pendingStopAdmission_.destination == PendingStopDestination::Playing &&
        TrackPreparedStopCommit::commit(loop, *runtime, pendingStopAdmission_,
                                        playbackGeneration, anchor, transferred);
    if (prepared) return false;
    if (!pendingStopAdmission_.canCommit(pendingStopAdmission_.transportCommandEpoch,
                                         pendingStopAdmission_.destination)) return false;
    if (loop.hasPendingCapturePass()) {
      if (pendingStopAdmission_.destination == PendingStopDestination::Stopped) {
        if (!loop.commitPreparedCapturePassAtomic(transferred)) return false;
      } else {
        if (!loop.canCommitPreparedCaptureAndCompanions() ||
            !loop.commitPreparedCaptureAndCompanionsAtomic(transferred)) return false;
        pendingStopAdmission_.historyRecycleOwner = &loop;
        pendingStopAdmission_.historyRecycleGeneration = loop.preparedCompanionHistoryGeneration();
        pendingStopAdmission_.historyRecycleStarted = false;
      }
    }
    pendingStopAdmission_.setPhase(PendingStopPhase::DeferredCleanup);
    return false;
  }

  if (pendingStopAdmission_.historyRecycleOwner == &loop) {
    if (!pendingStopAdmission_.historyRecycleStarted) {
      if (!TrackPreparedStopCommit::beginHistoryRecycle(loop, pendingStopAdmission_)) return false;
      return false;
    }
    const auto result = TrackPreparedStopCommit::stepHistoryRecycle(
        loop, pendingStopAdmission_, kDeferredStopPreparationQuantum);
    if (result != RuntimePlaybackOrderBuildResult::Complete) return false;
    pendingStopAdmission_.historyRecycleOwner = nullptr;
  }

  if (!loop.handoffSessionExitSnapshot(
          pendingStopAdmission_.destination == PendingStopDestination::Playing
              ? CommitReason::OverdubStop : CommitReason::OverdubStopToStopped)) {
    return false;
  }
  loop.completePreparedCapturePassDeferredCleanup();
  pendingNotes.clear();
  const PendingStopDestination destination = pendingStopAdmission_.destination;
  // Atomic publication selected the completed bridge, but its preparation
  // workers still describe the just-finished pass. Retire only that temporary
  // state before accepting a later record/overdub stop; discard preserves the
  // selected bridge and releases the alternate candidate for the next pass.
  runtime->discardFrozenCaptureBridge();
  pendingStopAdmission_.reset();
  loop.closeOverdubSession();
  if (destination == PendingStopDestination::Stopped) {
    silenceTrackMidiOutput();
    setState(TRACK_STOPPED);
    resetPlaybackState(clockManager.getCurrentTick());
  } else {
    silenceTrackMidiOutput();
    playbackRuntime.clearAllLedgers();
    reanchorPlaybackCycle(clockManager.getCurrentTick());
    // Normal Button A release has already published PLAYING. A later
    // transport-stop retarget is the only path that still needs this state edge.
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
      runtime != nullptr ? runtime->captureStagingIfPrewarmed() : nullptr;
  if (viewSet == nullptr) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: capture admission staging is not prewarmed");
    return;
  }
  if (!loopRef.hasActiveOverdubTransferAdmission() || !loopRef.canHandoffSessionExitSnapshot()) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: canonical transfer capacity is not prewarmed");
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
