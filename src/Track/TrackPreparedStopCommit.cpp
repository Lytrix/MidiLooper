// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "TrackPreparedStopCommit.h"
#include "Loop.h"
#include "TrackUndoPublication.h"
#include "TrackPlaybackRuntime.h"
#include "Utils/TrackMem.h"

namespace {
TRACK_COLD_MEM bool commitPreparedStop(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, TrackUndoPublication* undo) {
  const auto* views = runtime.viewSetIfPrewarmed();
  if (!views || !stop.canonicalPreparationQuantum || !stop.historyRecycleQuantum ||
      !stop.remainingHistoryRecycleGrains || !loop.canCommitPreparedCaptureAndCompanions() ||
      !loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs) ||
      !loop.preparedCompanionHistoryMatches(runtime.frozenCaptureCompanions, views->captureStaging) ||
      !runtime.canonicalPassInputs.matchesCanonicalOwner(loop.passes,
          {loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration})) return false;
  const uint32_t revisionDelta = loop.preparedCompanionHistoryIds()->empty() ? 1 : 2;
  if (loop.playbackRevision > UINT32_MAX - revisionDelta) return false;
  const PlaybackViewSourceIdentity base{loop.playbackRevision, generation, loop.publishedSnapshotIdentity()};
  // A successful capture makes the published snapshot stale until session-exit
  // publication. Source identity must match what the next playback tick reads.
  const PlaybackViewSourceIdentity target{loop.playbackRevision + revisionDelta, generation, nullptr};
  if (!runtime.canSelectReservedFrozenCaptureBridge(stop, base, target, loop.loopLengthTicks, cursor)) return false;
  // This turn cannot interleave MIDI emission or owner mutation. Canonical
  // transfer's fallible checks precede mutation; bridge publication below uses
  // only prepared storage and the lifecycle/source/ledger proof above.
  if (!loop.commitPreparedCaptureAndCompanionsAtomic(transferred)) return false;
  if (undo) undo->publishAtCommit();
  runtime.publishPreflightedFullLoopBridge(cursor);
  stop.historyRecycleOwner = &loop;
  stop.historyRecycleGeneration = loop.preparedCompanionHistoryGeneration();
  stop.historyRecycleStarted = false;
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

}  // namespace

TRACK_COLD_MEM bool TrackPreparedStopCommit::retargetStoppedRecovery(
    LoopPlaybackRuntime& runtime, const PendingStopAdmission& stop) {
  const auto* views = runtime.viewSetIfPrewarmed();
  return stop.pending && stop.destination == PendingStopDestination::Stopped &&
      stop.phase != PendingStopPhase::DeferredCleanup && views &&
      views->captureStaging.frozenGeneration == stop.frozenGeneration &&
      runtime.canonicalPassInputs.retargetFrozenCommandEpoch(stop.transportCommandEpoch, views->captureStaging);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commitStoppedWithUndo(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    GlobalUndoStack& stack, TrackUndoPublication& undo) {
  const auto* views = runtime.viewSetIfPrewarmed();
  if (!views || stop.phase != PendingStopPhase::AtomicTransfer ||
      !stop.canCommit(stop.transportCommandEpoch, PendingStopDestination::Stopped) ||
      !stop.canonicalPreparationQuantum || !stop.historyRecycleQuantum || !stop.remainingHistoryRecycleGrains ||
      views->captureStaging.frozenGeneration != stop.frozenGeneration ||
      !runtime.canonicalPassInputs.matchesFrozenCommand(stop.transportCommandEpoch) ||
      !runtime.canonicalPassInputs.matchesCanonicalOwner(loop.passes,
          {loop.playbackRevision, generation, loop.loopLengthTicks, loop.canonicalStorageGeneration}) ||
      !loop.canCommitPreparedCaptureAndCompanions() ||
      !loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs) ||
      !loop.preparedCompanionHistoryMatches(runtime.frozenCaptureCompanions, views->captureStaging) ||
      !undo.canPublish(stack, loop.loopId, loop.pendingCapturePass().id)) return false;
  const uint32_t revisionDelta = loop.preparedCompanionHistoryIds()->empty() ? 1 : 2;
  if (loop.playbackRevision > UINT32_MAX - revisionDelta ||
      !loop.commitPreparedCaptureAndCompanionsAtomic(transferred)) return false;
  undo.publishAtCommit();
  // STOPPED has no audible successor selection. The old PLAYING command and
  // bridge epoch stay invalid; transport silence is owned by Track's stop action.
  stop.historyRecycleOwner = &loop;
  stop.historyRecycleGeneration = loop.preparedCompanionHistoryGeneration();
  stop.historyRecycleStarted = false;
  stop.setPhase(PendingStopPhase::DeferredCleanup);
  return true;
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commitWithUndo(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack& stack, TrackUndoPublication& undo) {
  if (!undo.canPublish(stack, loop.loopId, loop.pendingCapturePass().id)) return false;
  return commitPreparedStop(loop, runtime, stop, generation, cursor, transferred, &undo);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::commit(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred) {
  return commitPreparedStop(loop, runtime, stop, generation, cursor, transferred, nullptr);
}

TRACK_COLD_MEM bool TrackPreparedStopCommit::beginHistoryRecycle(
    Loop& loop, PendingStopAdmission& stop) {
  if (stop.historyRecycleOwner != &loop || stop.historyRecycleStarted || !stop.historyRecycleQuantum ||
      stop.historyRecycleGeneration != loop.preparedCompanionHistoryGeneration() ||
      !stop.consumeHistoryRecycleGrain()) return false;
  if (!loop.beginRecyclePreparedCompanionHistory()) return false;
  stop.historyRecycleStarted = true;
  return true;
}

TRACK_COLD_MEM RuntimePlaybackOrderBuildResult TrackPreparedStopCommit::stepHistoryRecycle(
    Loop& loop, PendingStopAdmission& stop, size_t quantum) {
  if (stop.historyRecycleOwner != &loop || !stop.historyRecycleStarted || !quantum ||
      stop.historyRecycleGeneration != loop.preparedCompanionHistoryGeneration() ||
      quantum != stop.historyRecycleQuantum || stop.phase != PendingStopPhase::DeferredCleanup ||
      !stop.finalizationComplete()) return RuntimePlaybackOrderBuildResult::Denied;
  if (loop.preparedCompanionHistoryRecycleComplete()) return RuntimePlaybackOrderBuildResult::Complete;
  if (!stop.consumeHistoryRecycleGrain() || !loop.stepRecyclePreparedCompanionHistory(quantum))
    return RuntimePlaybackOrderBuildResult::Denied;
  return loop.preparedCompanionHistoryRecycleComplete()
      ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}

TRACK_COLD_MEM RuntimePlaybackOrderBuildResult TrackPreparedStopCommit::stepPreparation(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop, size_t quantum) {
  const auto* views = runtime.viewSetIfPrewarmed();
  if (!views || !stop.pending || !quantum || quantum != stop.canonicalPreparationQuantum ||
      stop.remainingClosureGrains || views->captureStaging.frozenGeneration != stop.frozenGeneration ||
      !runtime.canonicalPassInputs.matchesFrozenCommand(stop.transportCommandEpoch) ||
      !loop.preparedCompanionHistoryCanStep(runtime.frozenCaptureCompanions, views->captureStaging,
                                            runtime.canonicalPassInputs))
    return RuntimePlaybackOrderBuildResult::Denied;
  if (loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs))
    return RuntimePlaybackOrderBuildResult::Complete;
  if (!stop.consumeCanonicalPreparationGrain()) return RuntimePlaybackOrderBuildResult::Denied;
  if (!loop.stepPreparedCompanionHistory(quantum)) return RuntimePlaybackOrderBuildResult::Denied;
  return loop.preparedCompanionCaptureVerified(runtime.canonicalPassInputs)
      ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}
