// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "TrackPreparedStopCommit.h"
#include "Loop.h"
#include "TrackUndoPublication.h"
#include "TrackPlaybackRuntime.h"
#include "Utils/TrackMem.h"
#include "Utils/DebugSessionCapture.h"

namespace {
TRACK_COLD_MEM bool denyAtomicTransfer(uint16_t reason) {
#if defined(SESSION_CAPTURE)
  static uint16_t lastReason = 0;
  if (reason != lastReason) {
    SC_ODUB_STOP_STAGE("atomic_preflight", 0, 0, UINT32_MAX, UINT32_MAX, reason, 0, "denied");
    lastReason = reason;
  }
#else
  (void)reason;
#endif
  return false;
}

/// OpenSpec 5.12 / D8: the interval's final pass identity. A deferred job owns its staged receipt
/// from `PrepareTransfer` onward, so the Loop producer slot may already carry a successor's pass
/// and must never be consulted for this interval's identity.
TRACK_COLD_MEM PassId finalPassId(const PendingCapturePass* pendingPass, const Loop& loop) {
  if (pendingPass != nullptr) return pendingPass->id;
  return loop.hasPendingCapturePass() ? loop.pendingCapturePass().id : kInvalidPassId;
}

TRACK_COLD_MEM bool commitPreparedStop(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, TrackUndoPublication* undo,
    PendingCapturePass* pendingPass) {
  const auto* views = runtime.viewSetIfPrewarmed();
  if (!views) return denyAtomicTransfer(1);
  uint16_t denied = !stop.canonicalPreparationQuantum ? 2 : 0;
  denied |= !stop.historyRecycleQuantum ? 4 : 0;
  denied |= !stop.remainingHistoryRecycleGrains ? 8 : 0;
  denied |= !loop.canCommitPreparedCaptureAndCompanions(stop.intervalToken(), pendingPass) ? 16 : 0;
  denied |= !loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs, stop.intervalToken()) ? 32 : 0;
  denied |= !loop.preparedCompanionHistoryMatches(runtime.frozenCaptureCompanions, views->frozenCaptureStaging(), stop.intervalToken()) ? 64 : 0;
  denied |= !runtime.canonicalPassInputs.matchesCanonicalOwner(loop.passes,
      {loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration}) ? 128 : 0;
  if (denied) return denyAtomicTransfer(denied);
  const uint32_t revisionDelta = loop.preparedCompanionHistoryIds(stop.intervalToken())->empty() ? 1 : 2;
  if (loop.playbackRevision > UINT32_MAX - revisionDelta) return false;
  const PlaybackViewSourceIdentity base{loop.playbackRevision, generation, loop.publishedSnapshotIdentity()};
  // A successful capture makes the published snapshot stale until session-exit
  // publication. Source identity must match what the next playback tick reads.
  const PlaybackViewSourceIdentity target{loop.playbackRevision + revisionDelta, generation, nullptr};
  if (!runtime.canSelectReservedFrozenCaptureBridge(stop, base, target, loop.loopLengthTicks, cursor)) {
    const auto& admission = runtime.frozenCaptureSourceAdmission;
    uint16_t reason = 256;
    reason |= !runtime.canonicalCaptureBridge ? 1 : 0;
    reason |= !stop.rankedPreparationQuantum ? 2 : 0;
    reason |= stop.phase != PendingStopPhase::AtomicTransfer ? 4 : 0;
    reason |= !stop.canCommit(admission.transportCommandEpoch, PendingStopDestination::Playing) ? 8 : 0;
    reason |= !(admission.baseSource == base) ? 16 : 0;
    reason |= !(admission.targetSource == target) ? 32 : 0;
    reason |= !runtime.canSelectFrozenCaptureBridge(stop.frozenGeneration,
        stop.transportCommandEpoch, loop.loopLengthTicks, cursor) ? 64 : 0;
    return denyAtomicTransfer(reason);
  }
  // This turn cannot interleave MIDI emission or owner mutation. Canonical
  // transfer's fallible checks precede mutation; bridge publication below uses
  // only prepared storage and the lifecycle/source/ledger proof above.
  if (!loop.commitPreparedCaptureAndCompanionsAtomic(transferred, stop.intervalToken(), pendingPass))
    return false;
  if (undo) undo->publishAtCommit();
  runtime.publishPreflightedFullLoopBridge(cursor);
  stop.historyRecycleOwner = &loop;
  stop.historyRecycleGeneration = loop.preparedCompanionHistoryGeneration();
  stop.historyRecycleStarted = false;
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

}  // namespace

TRACK_COLD_MEM bool TrackPreparedStopCommit::requiresBridgeRefresh(
    const Loop& loop, const LoopPlaybackRuntime& runtime,
    const PendingStopAdmission& stop, const EmissionCursor& cursor,
    const PendingCapturePass* pendingPass) {
  // OpenSpec 5.12 / D8: "is a receipt staged for this interval" is answered by the job-owned
  // pass when the job holds one, never by the Loop slot a successor may own.
  const bool staged = pendingPass != nullptr ? pendingPass->id != kInvalidPassId
                                            : loop.hasPendingCapturePass();
  return stop.destination == PendingStopDestination::Playing &&
      staged &&
      !runtime.canSelectFrozenCaptureBridge(stop.frozenGeneration,
          stop.transportCommandEpoch, loop.loopLengthTicks, cursor);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::advanceAtomicTransfer(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, PendingCapturePass* pendingPass) {
  if (stop.phase != PendingStopPhase::AtomicTransfer ||
      !stop.canCommit(stop.transportCommandEpoch, stop.destination)) return false;
  // OpenSpec 5.12 / D8: the interval's receipt lives in its job from `PrepareTransfer` onward.
  const bool staged = pendingPass != nullptr ? pendingPass->id != kInvalidPassId
                                            : loop.hasPendingCapturePass();
  if (staged) {
    if (stop.destination == PendingStopDestination::Playing)
      return commit(loop, runtime, stop, generation, cursor, transferred, pendingPass);
    if (loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs, stop.intervalToken()))
      return commitStopped(loop, runtime, stop, generation, transferred, pendingPass);
    // Transport Stop can recover the frozen canonical pass before companion
    // preparation. This route never authorizes an audible PLAYING successor.
    if (pendingPass != nullptr ? !loop.commitPreparedCapturePassAtomic(transferred, *pendingPass)
                               : !loop.commitPreparedCapturePassAtomic(transferred)) return false;
  }
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::advanceAtomicTransferWithUndo(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack& stack,
    TrackUndoPublication& undo, PendingCapturePass* pendingPass) {
  if (stop.phase != PendingStopPhase::AtomicTransfer ||
      !stop.canCommit(stop.transportCommandEpoch, stop.destination)) return false;
  const bool staged = pendingPass != nullptr ? pendingPass->id != kInvalidPassId
                                            : loop.hasPendingCapturePass();
  if (staged) {
    if (stop.destination == PendingStopDestination::Playing)
      return commitWithUndo(loop, runtime, stop, generation, cursor, transferred, stack, undo,
                            pendingPass);
    if (loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs, stop.intervalToken()))
      return commitStoppedWithUndo(loop, runtime, stop, generation, transferred, stack, undo,
                                   pendingPass);
    // Transport Stop can recover the frozen canonical pass before companion
    // preparation. This route never authorizes an audible PLAYING successor, and
    // it publishes no protected undo entry.
    if (pendingPass != nullptr ? !loop.commitPreparedCapturePassAtomic(transferred, *pendingPass)
                               : !loop.commitPreparedCapturePassAtomic(transferred)) return false;
  }
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::completeDeferredCleanup(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    CapturePreview& preview, bool ownsLiveSession) {
  if (stop.phase != PendingStopPhase::DeferredCleanup || !stop.finalizationComplete() ||
      stop.historyRecycleOwner) return false;
  // DEC-068: this interval's session-exit receipt was captured at its own stop admission, before
  // any successor could touch the shared session state. The transfer above committed our final
  // pass, so fill its id and release the receipt to the publisher. Building a receipt from the
  // live session here is what handed the successor's identities to the predecessor.
  if (stop.sessionExitReceiptRing != Loop::kInvalidSessionExitReceipt) {
    if (!loop.markSessionExitTailReady(stop.sessionExitReceiptRing, loop.lastCommittedPassId())) {
      return false;
    }
  } else if (!loop.handoffSessionExitSnapshot(stop.destination == PendingStopDestination::Playing
                 ? CommitReason::OverdubStop : CommitReason::OverdubStopToStopped)) {
    return false;
  }
  loop.completePreparedCapturePassDeferredCleanup(preview);
  // Retire preparation workers only. The published bridge and any held-note
  // closure remain owned by the continuously advancing playback cursor.
  runtime.discardFrozenCaptureBridge();
  runtime.discardCanonicalWindow();
  // Keep the already-computed per-pass companion results across punch-out.
  // This exchanges owned scratch buffers; it never replays history here.
  // DEC-070: this retention exists to save the *next* interval work, but when a successor interval is
  // already live it is exactly what blocks that successor -- the retained `canonicalPassInputs` keeps
  // its frozen capture attached and `beginFrozenCanonicalTail` refuses that (probe bit 3 in
  // `captures/successor-wrap-43`; `-45` shows the resulting stall), while clearing the attachment at
  // the retarget would invalidate the canonical prefix it still needs. Retain only while this job is
  // still the live interval; a live successor prepares its own canonical inputs.
  if (ownsLiveSession && runtime.canonicalPassInputs.captureAttached()) {
    const auto generation = runtime.canonicalPrefixSource.sourceGeneration;
    const PlaybackViewSourceIdentity source{loop.playbackRevision, generation, loop.publishedSnapshotIdentity()};
    // Bind the original live generation owner, not this local scalar.
    runtime.retainCommittedCanonicalTail(loop, source);
  }
  runtime.discardFrozenCanonicalTail();
  // DEC-068: only the interval that still owns the live session may end it. A successor's
  // session stays open and independent while this job retires.
  if (ownsLiveSession) {
    loop.closeOverdubSession();
  }
  if (runtime.captureStopOwner == &stop) runtime.captureStopOwner = nullptr;
  // DEC-067: the successor's stop may be admitted again once this job's
  // finalization resources are back; the maintenance turn reports that edge.
  runtime.canonicalReleaseMarkerArmed = true;
  stop.reset();
  return true;
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::retargetStoppedRecovery(
    LoopPlaybackRuntime& runtime, const PendingStopAdmission& stop) {
  const auto* views = runtime.viewSetIfPrewarmed();
  return stop.pending && stop.destination == PendingStopDestination::Stopped &&
      stop.phase != PendingStopPhase::DeferredCleanup && views &&
      views->frozenCaptureStaging().frozenGeneration == stop.frozenGeneration &&
      runtime.canonicalPassInputs.retargetFrozenCommandEpoch(stop.transportCommandEpoch, views->frozenCaptureStaging());
}

static TRACK_COLD_MEM bool commitPreparedStopped(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    GlobalUndoStack* stack, TrackUndoPublication* undo, PendingCapturePass* pendingPass) {
  const auto* views = runtime.viewSetIfPrewarmed();
  if (!views || stop.phase != PendingStopPhase::AtomicTransfer ||
      !stop.canCommit(stop.transportCommandEpoch, PendingStopDestination::Stopped) ||
      !stop.canonicalPreparationQuantum || !stop.historyRecycleQuantum || !stop.remainingHistoryRecycleGrains ||
      views->frozenCaptureStaging().frozenGeneration != stop.frozenGeneration ||
      !runtime.canonicalPassInputs.matchesFrozenCommand(stop.transportCommandEpoch) ||
      !runtime.canonicalPassInputs.matchesCanonicalOwner(loop.passes,
          {loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration}) ||
      !loop.canCommitPreparedCaptureAndCompanions(stop.intervalToken(), pendingPass) ||
      !loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs, stop.intervalToken()) ||
      !loop.preparedCompanionHistoryMatches(runtime.frozenCaptureCompanions, views->frozenCaptureStaging(), stop.intervalToken()) ||
      (undo && (!stack || !undo->canPublish(*stack, loop.loopId, finalPassId(pendingPass, loop)))))
    return false;
  const uint32_t revisionDelta = loop.preparedCompanionHistoryIds(stop.intervalToken())->empty() ? 1 : 2;
  if (loop.playbackRevision > UINT32_MAX - revisionDelta ||
      !loop.commitPreparedCaptureAndCompanionsAtomic(transferred, stop.intervalToken(), pendingPass))
    return false;
  if (undo) undo->publishAtCommit();
  // STOPPED has no audible successor selection. The old PLAYING command and
  // bridge epoch stay invalid; transport silence is owned by Track's stop action.
  stop.historyRecycleOwner = &loop;
  stop.historyRecycleGeneration = loop.preparedCompanionHistoryGeneration();
  stop.historyRecycleStarted = false;
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commitStopped(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    PendingCapturePass* pendingPass) {
  return commitPreparedStopped(loop, runtime, stop, generation, transferred, nullptr, nullptr,
                               pendingPass);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commitStoppedWithUndo(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    GlobalUndoStack& stack, TrackUndoPublication& undo, PendingCapturePass* pendingPass) {
  return commitPreparedStopped(loop, runtime, stop, generation, transferred, &stack, &undo,
                               pendingPass);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commitWithUndo(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack& stack, TrackUndoPublication& undo,
    PendingCapturePass* pendingPass) {
  if (!undo.canPublish(stack, loop.loopId, finalPassId(pendingPass, loop))) return false;
  return commitPreparedStop(loop, runtime, stop, generation, cursor, transferred, &undo,
                            pendingPass);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commit(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, PendingCapturePass* pendingPass) {
  return commitPreparedStop(loop, runtime, stop, generation, cursor, transferred, nullptr,
                            pendingPass);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::beginHistoryRecycle(
    Loop& loop, PendingStopAdmission& stop) {
  if (stop.historyRecycleOwner != &loop || stop.historyRecycleStarted || !stop.historyRecycleQuantum ||
      stop.historyRecycleGeneration != loop.preparedCompanionHistoryGeneration() ||
      !stop.consumeHistoryRecycleGrain()) return false;
  if (!loop.beginRecyclePreparedCompanionHistory(stop.intervalToken())) return false;
  stop.historyRecycleStarted = true;
  return true;
}

TRACK_COLD_MEM RuntimePlaybackOrderBuildResult TrackPreparedStopCommit::stepHistoryRecycle(
    Loop& loop, PendingStopAdmission& stop, size_t quantum) {
  if (stop.historyRecycleOwner != &loop || !stop.historyRecycleStarted || !quantum ||
      stop.historyRecycleGeneration != loop.preparedCompanionHistoryGeneration() ||
      quantum != stop.historyRecycleQuantum || stop.phase != PendingStopPhase::DeferredCleanup ||
      !stop.finalizationComplete()) return RuntimePlaybackOrderBuildResult::Denied;
  if (loop.preparedCompanionHistoryRecycleComplete(stop.intervalToken())) return RuntimePlaybackOrderBuildResult::Complete;
  if (!stop.consumeHistoryRecycleGrain() || !loop.stepRecyclePreparedCompanionHistory(quantum, stop.intervalToken()))
    return RuntimePlaybackOrderBuildResult::Denied;
  return loop.preparedCompanionHistoryRecycleComplete(stop.intervalToken())
      ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commitSupersededCausalBase(
    Loop& loop, PendingStopAdmission& stop, PreparedCapturePassTransfer& transferred,
    TrackUndoPublication* undo, PendingCapturePass* pendingPass) {
  if (stop.phase != PendingStopPhase::AtomicTransfer) return false;
  // No bridge, no companion publication, no stale PLAYING successor selection.
  // The accepted capture pass is still committed, and a prepared undo entry for
  // it is published in the same turn.
  const bool staged = pendingPass != nullptr ? pendingPass->id != kInvalidPassId
                                            : loop.hasPendingCapturePass();
  if (staged) {
    if (pendingPass != nullptr ? !loop.commitPreparedCapturePassAtomic(transferred, *pendingPass)
                               : !loop.commitPreparedCapturePassAtomic(transferred)) return false;
    if (undo != nullptr) undo->publishAtCommit();
  }
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

TRACK_COLD_MEM RuntimePlaybackOrderBuildResult TrackPreparedStopCommit::stepPreparation(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop, size_t quantum) {
  const auto* views = runtime.viewSetIfPrewarmed();
  if (!views || !stop.pending || !quantum || quantum != stop.canonicalPreparationQuantum ||
      stop.remainingClosureGrains || views->frozenCaptureStaging().frozenGeneration != stop.frozenGeneration ||
      !runtime.canonicalPassInputs.matchesFrozenCommand(stop.transportCommandEpoch) ||
      !loop.preparedCompanionHistoryCanStep(runtime.frozenCaptureCompanions, views->frozenCaptureStaging(),
                                            runtime.canonicalPassInputs, stop.intervalToken()))
    return RuntimePlaybackOrderBuildResult::Denied;
  if (loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs, stop.intervalToken()))
    return RuntimePlaybackOrderBuildResult::Complete;
  if (!stop.consumeCanonicalPreparationGrain()) return RuntimePlaybackOrderBuildResult::Denied;
  if (!loop.stepPreparedCompanionHistory(quantum, stop.intervalToken())) return RuntimePlaybackOrderBuildResult::Denied;
  return loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs, stop.intervalToken())
      ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}
