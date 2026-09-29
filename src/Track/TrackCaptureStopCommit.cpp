//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "LoopContentResolution.h"
#include "LooperState.h"
#include "StorageManager.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/RecordStopLength.h"
#include "Utils/TrackMem.h"
#include "TickPhase.h"

extern TrackManager trackManager;

namespace {

struct OverdubStopFinalizeTraceCtx {
  bool enabled = false;
  uint32_t timelineStartUs = 0;
  size_t eventCount = 0;
  size_t chunkRefCount = 0;
};

TRACK_COLD_MEM OverdubStopFinalizeTraceCtx beginOverdubStopFinalizeTrace(const Loop& loop,
                                                                         bool overdubStop) {
  OverdubStopFinalizeTraceCtx ctx{};
#if defined(SESSION_CAPTURE)
  if (!overdubStop) {
    return ctx;
  }
  ctx.enabled = true;
  ctx.timelineStartUs = micros();
  const StopPathStorageStats stats = collectStopPathStorageStats(loop, false);
  ctx.eventCount = stats.eventCount;
  ctx.chunkRefCount = stats.chunkRefCount;
#else
  (void)loop;
  (void)overdubStop;
#endif
  return ctx;
}

TRACK_COLD_MEM void emitOverdubStopFinalizeTraceStage(const OverdubStopFinalizeTraceCtx& ctx,
                                                      const char* stage, uint32_t durationUs,
                                                      const char* outcome) {
#if defined(SESSION_CAPTURE)
  if (!ctx.enabled) {
    return;
  }
  const uint32_t elapsedUs = micros() - ctx.timelineStartUs;
  SC_ODUB_STOP_STAGE(stage, elapsedUs, durationUs, DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, ctx.eventCount, ctx.chunkRefCount,
                     outcome);
#else
  (void)ctx;
  (void)stage;
  (void)durationUs;
  (void)outcome;
#endif
}

}  // namespace

void Track::finalizeLoopAtStop(uint32_t openTailCloseTick, bool scheduleDeferredFullValidate) {
  (void)openTailCloseTick;
  deferredFullMidiValidate = scheduleDeferredFullValidate;
  deferredValidateQueuedAtMs = scheduleDeferredFullValidate ? millis() : 0;
}

TRACK_COLD_MEM CommitResult Track::finalizeCommitSideEffects(CommitResult result,
                                                             CommitReason reason,
                                                             uint32_t closeTick, uint8_t slot) {
  // S4f.5: everything below resolves the interval's own slot. Live-edge callers pass the active
  // index, so behaviour is unchanged today; S4f.3 is what makes these differ.
  Loop& loop = getLoop(slot);
  const bool recordStop = reason == CommitReason::RecordStop ||
                          reason == CommitReason::RecordStopToStopped;
  const bool overdubStop = reason == CommitReason::OverdubStop ||
                           reason == CommitReason::OverdubStopToStopped;
  const OverdubStopFinalizeTraceCtx finalizeTrace =
      beginOverdubStopFinalizeTrace(loop, overdubStop);
  const bool deferFullValidate = recordStop;
  auto scheduleDeferredValidateOnly = [&]() {
    const bool hasCommittedPasses = loop.hasCommittedPasses() && loop.loopLengthTicks > 0;
    deferredFullMidiValidate = deferFullValidate && hasCommittedPasses;
    deferredValidateQueuedAtMs = deferredFullMidiValidate ? millis() : 0;
  };

  switch (result) {
    case CommitResult::Skipped: {
      loop.discardCapture();
      if (recordStop && loop.activeCapturePassCount() == 0) {
        loop.resetPassTimeline();
        loop.loopLengthTicks = 0;
        loop.loopStartTick = 0;
        loop.startLoopTick = 0;
        loop.nextEventIndex = 0;
        loop.lastTickInLoop = 0;
        loop.invalidatePlaybackCaches();
      }
      if (overdubStop) {
#if defined(SESSION_CAPTURE)
        const uint32_t finalizeLoopStartUs = micros();
#endif
        finalizeLoopAtStop(closeTick, false);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_finalize_loop_stop",
                                          micros() - finalizeLoopStartUs, "ok");
#endif
        if (!editManager.isNoteEditActive()) {
#if defined(SESSION_CAPTURE)
          const uint32_t pushUndoStartUs = micros();
#endif
          TrackUndo::pushOverdubSessionOnStop(*this, slot, kInvalidPassId, {},
                                              false);
#if defined(SESSION_CAPTURE)
          emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_push_overdub_undo",
                                            micros() - pushUndoStartUs, "ok");
#endif
        } else {
          emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_push_overdub_undo", 0,
                                            "skipped_note_edit");
        }
        if (loop.hasPendingWrapJournal() || loop.hasInFlightSnapshotConstruction() ||
            loop.hasPendingSnapshotPublish()) {
          loop.queueSessionExitSnapshot(reason, {});
          TrackUndo::bindTipSessionExitUndoAttach(*this, loop);
        }
        const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
        const uint8_t persistSlotIndex = slot;
#if defined(SESSION_CAPTURE)
        const uint32_t markDirtyStartUs = micros();
#endif
        StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_mark_material_dirty",
                                          micros() - markDirtyStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
        const uint32_t admitPersistStartUs = micros();
#endif
        StorageManager::admitLoopSlotPersist(persistTrackIndex, persistSlotIndex);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_admit_loop_persist",
                                          micros() - admitPersistStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
        const uint32_t requestSaveStartUs = micros();
#endif
        StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                                 MemoryMonitor::getInternalHeapFreeBytes(), true);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_request_deferred_save",
                                          micros() - requestSaveStartUs, "ok");
#endif
      } else {
        scheduleDeferredValidateOnly();
      }
      break;
    }
    case CommitResult::Committed: {
      const PassId undoPassId = loop.lastCommittedPassId();
      const uint32_t overdubSealPriorRevision =
          loop.playbackRevision > 0 ? loop.playbackRevision - 1 : 0;
      if (overdubStop) {
#if defined(SESSION_CAPTURE)
        const uint32_t finalizeLoopStartUs = micros();
#endif
        finalizeLoopAtStop(closeTick, false);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_finalize_loop_stop",
                                          micros() - finalizeLoopStartUs, "ok");
#endif
      } else {
        // Record stop already finalizes wrap-window at seal; defer full validate only.
        scheduleDeferredValidateOnly();
      }
      const bool isRecordPass =
          loop.passes.hasRecordPass() && loop.passes.recordPass.id == undoPassId;
      EditPassIdList companionIds;
      if (isRecordPass) {
        TrackUndo::pushRecordPassAdded(*this, slot, undoPassId);
        loop.markDisplayCachesStale();
      } else if (!editManager.isNoteEditActive()) {
        // Dual-storage encoding: OverdubPass already published; seal Shorten/Hide companions.
#if defined(SESSION_CAPTURE)
        const uint32_t sealPendingStartUs = micros();
#endif
        companionIds = loop.sealPendingNoteChangesToEditPasses(!overdubStop);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_seal_pending_changes",
                                          micros() - sealPendingStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
        const uint32_t pushUndoStartUs = micros();
#endif
        TrackUndo::pushOverdubSessionOnStop(*this, slot, undoPassId, companionIds,
                                            true);
        loop.queueSessionExitSnapshot(reason, companionIds);
        TrackUndo::bindTipSessionExitUndoAttach(*this, loop);
        if (overdubStop) {
          loop.recordLastOverdubSeal(undoPassId, companionIds, overdubSealPriorRevision);
        }
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_push_overdub_undo",
                                          micros() - pushUndoStartUs, "ok");
#endif
        if (overdubStop) {
#if defined(SESSION_CAPTURE)
          const uint32_t markRangesStartUs = micros();
#endif
          loop.markAffectedDisplayCacheRanges(undoPassId, companionIds);
#if defined(SESSION_CAPTURE)
          emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_mark_display_ranges",
                                            micros() - markRangesStartUs, "ok");
#endif
        } else {
          loop.markDisplayCachesStale();
        }
      } else if (overdubStop) {
#if defined(SESSION_CAPTURE)
        const uint32_t markRangesStartUs = micros();
#endif
        loop.markAffectedDisplayCacheRanges(undoPassId, EditPassIdList{});
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_mark_display_ranges",
                                          micros() - markRangesStartUs, "active_note_edit");
#endif
      } else {
        loop.markDisplayCachesStale();
      }
      if (!isRecordPass) {
        bool published = false;
#if defined(SESSION_CAPTURE)
        const uint32_t publishPreparedStartUs = micros();
#endif
        for (const OverdubPass& pass : loop.passes.overdubPasses) {
          if (pass.id == undoPassId) {
            LoopContentResolution::publishPreparedOverdubPass(pass, loop.playbackRevision,
                                                              loop.loopLengthTicks,
                                                              loop.passes.editPasses, companionIds);
            published = true;
            break;
          }
        }
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_publish_prepared_overdub",
                                          micros() - publishPreparedStartUs,
                                          published ? "ok" : "missing_pass");
#endif
      }
      if (overdubStop) {
        const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
        const uint8_t persistSlotIndex = slot;
#if defined(SESSION_CAPTURE)
        const uint32_t markDirtyStartUs = micros();
#endif
        StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_mark_material_dirty",
                                          micros() - markDirtyStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
        const uint32_t admitPersistStartUs = micros();
#endif
        StorageManager::admitLoopSlotPersist(persistTrackIndex, persistSlotIndex);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_admit_loop_persist",
                                          micros() - admitPersistStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
        const uint32_t requestSaveStartUs = micros();
#endif
        StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                                 MemoryMonitor::getInternalHeapFreeBytes(), true);
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_request_deferred_save",
                                          micros() - requestSaveStartUs, "ok");
#endif
      }
      break;
    }
    case CommitResult::SealFailed: {
      loop.discardPendingCapturePass();
      trackManager.reclaimUnreferencedDisabledPasses();
      const CommitResult retry = loop.commitCapturePass(reason, closeTick);
      if (retry != CommitResult::SealFailed) {
        result = finalizeCommitSideEffects(retry, reason, closeTick, slot);
        return result;
      }
      break;
    }
  }

  if (result == CommitResult::SealFailed) {
    deferredFullMidiValidate = false;
    deferredValidateQueuedAtMs = 0;
  }

  if (result == CommitResult::Committed) {
#if defined(SESSION_CAPTURE)
    const uint32_t invalidateCachesStartUs = micros();
#endif
    invalidateCaches();
#if defined(SESSION_CAPTURE)
    if (overdubStop) {
      emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_invalidate_caches",
                                        micros() - invalidateCachesStartUs, "ok");
    }
#endif
    if (recordStop) {
      queueDeferredRecordRevts(slot);
    }
    if (overdubStop) {
#if defined(SESSION_CAPTURE)
      const uint32_t queueStoredVerifyStartUs = micros();
#endif
      queueDeferredStoredMidiVerification();
#if defined(SESSION_CAPTURE)
      emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_queue_stored_verify",
                                        micros() - queueStoredVerifyStartUs, "ok");
#endif
    }
  }
  return result;
}

TRACK_COLD_MEM CommitResult Track::commitCaptureForStop(CommitReason reason, uint32_t commitTick,
                                                         uint32_t closeTick, uint8_t slot) {
  Loop& loop = getLoop(slot);
  const CommitResult commitResult = loop.commitCapturePass(reason, commitTick);
  return finalizeCommitSideEffects(commitResult, reason, closeTick, slot);
}

uint32_t Track::prepareRecordStop(uint32_t currentTick, const char* guardLabel) {
  Loop& loop = getActiveLoop();
  uint32_t rawLength = 0;
  if (currentTick >= loop.startLoopTick) {
    rawLength = currentTick - loop.startLoopTick;
  } else {
    logger.warning("%s guard: currentTick(%lu) < startLoopTick(%lu), clamping length",
                   guardLabel != nullptr ? guardLabel : "prepareRecordStop",
                   static_cast<unsigned long>(currentTick),
                   static_cast<unsigned long>(loop.startLoopTick));
  }
  // First-record input maintains its maximum as rows are accepted. Do not materialize
  // the growing take just to compute one scalar on the stop edge.
  const uint32_t lastEventTick = loop.capture.phase == CapturePhase::Record
                                     ? loop.captureMaximumTick() : findLastEventTick();
  loop.loopLengthTicks = computeRecordStopLengthTicks(rawLength, lastEventTick);

  if (loop.loopLengthTicks > 0) {
    if (!pendingNotes.empty()) {
      finalizePendingNotes(currentTick);
    }
    // Overflow NoteOns stay with in-loop stop Offs (174646 last 81). Remaining
    // events past the quantized length are still dropped.
    loop.capture.store.mapNoteOnsAtOrBeyondTickIntoLoop(loop.loopLengthTicks);
  }
  return rawLength;
}

TRACK_COLD_MEM void Track::completeAdmittedRecordStop(PendingStopDestination destination,
                                                      uint32_t currentTick, uint8_t slot,
                                                      uint32_t rawLength) {
  if (!loopsAllocated() || slot >= Config::MAX_LOOPS_PER_TRACK) return;
  Loop& loop = getLoop(slot);
  // Admission stamped the interval's final length from the maintained maximum summary, so the
  // edge needs no store walk: it is the bounded scalar part of the old inline stop.
  const uint32_t finalLength = loop.loopLengthTicks;
  const bool toPlaying = destination == PendingStopDestination::Playing;
  // A stop whose interval no longer owns the live slot still gets its loop-local geometry, but
  // must not move the track-global transport or the shared clock under a successor.
  const bool ownsTrackState = slot == activeLoopIndex;
  const bool alignRequested = toPlaying && alignLoopOriginOnNextStop;
  alignLoopOriginOnNextStop = false;
  if (alignRequested) {
    const uint32_t absRecStart = loop.startLoopTick;
    const uint32_t remBar = absRecStart % TICKS_PER_BAR;
    const uint32_t graceBar = TICKS_PER_BAR / 2;
    const uint32_t snapBar = remBar <= graceBar ? absRecStart - remBar
                                                : absRecStart - remBar + TICKS_PER_BAR;
    const int64_t delta = static_cast<int64_t>(snapBar) - static_cast<int64_t>(absRecStart);
    if (delta != 0 && loop.hasCommittedPasses()) {
      loop.shiftActiveCapturePassTicks(delta);
      loop.invalidatePlaybackCaches();
    }
  }
  loop.nextEventIndex = 0;
  const uint32_t recordStartTick = loop.startLoopTick;
  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks =
      RecordStopLength::computeTruncationRewindTicks(rawLength, finalLength);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    if (ownsTrackState) clockManager.assignCurrentTickSilently(playbackTick);
    logger.log(CAT_TRACK, LOG_INFO,
               "Record stop truncation rewind: raw=%lu final=%lu rewind=%lu playbackTick=%lu positionInBar=%lu",
               rawLength, finalLength, rewindTicks, playbackTick, rawLength % Config::TICKS_PER_BAR);
  }
  loop.startLoopTick = recordStartTick;
  loop.lastTickInLoop = (finalLength > 0)
                            ? tickPhaseInLoop(playbackTick, recordStartTick, finalLength)
                            : 0;
  const uint32_t storagePhaseTickAtStop = loop.lastTickInLoop;
  if (finalLength > 0) {
    projectionCycleStartTick =
        static_cast<int32_t>(playbackTick) - static_cast<int32_t>(loop.lastTickInLoop);
  }
  invalidatePlaybackCaches();
  SC_REC_STOP(toPlaying ? "stop" : "stopToStopped", slot, playbackTick, recordStartTick, rawLength,
              finalLength, alignRequested);
  logger.logTrackEvent(toPlaying ? "Recording stopped" : "Recording stopped (to STOPPED)",
                       playbackTick, "recStart=%lu length=%lu",
                       static_cast<unsigned long>(recordStartTick),
                       static_cast<unsigned long>(finalLength));
  if (!ownsTrackState) return;
  // RECORDING -> STOPPED_RECORDING is this stop's own edge; the destination then resolves
  // exactly as the inline route's did. Emptiness is deliberately NOT decided here: a take that
  // passes the length policy can still be filtered out downstream, which
  // `stepDeferredRecordEmptyResult` resolves once, against the published result.
  (void)setState(TRACK_STOPPED_RECORDING);
  if (!toPlaying) {
    setState(TRACK_STOPPED);
    return;
  }
  if (finalLength == 0) {
    resetActiveLoopAfterEmptyCapture(loop);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }
  // Silence live/held notes on the wire before loop playback catch-up; CC123 is not stored.
  sendAllNotesOff();
  resetPlaybackState(playbackTick);
  if (LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot)) {
    // The final length is known only now. Bind the already prepared bar-1 view before the
    // normal PLAYING transition, so launch stays on this boundary while the deferred worker
    // finalizes the take. (The frozen return window the closure grain binds keeps the interval
    // audible after it.)
    if (!runtime->bindFirstRecordingBar(finalLength)) {
      logger.log(CAT_TRACK, LOG_WARNING,
                 "First-record bar-1 playback binding unavailable length=%lu",
                 static_cast<unsigned long>(finalLength));
    }
  }
  startPlaying(playbackTick, true);
  displayManager.refreshViewportAfterRecordStop(*this, slot, storagePhaseTickAtStop);
}

void Track::stopRecording(uint32_t currentTick) {
  // Only a live Record interval can be stopped. A repeat press on a stop already in flight is a
  // no-op here; the accepted obligation carries it exactly once.
  if (!isRecording()) return;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  const uint64_t interval = captureInterval_;
  Loop& loop = getLoop(recordedSlotIndex);
  const uint32_t stopPathStartUs = micros();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "entered");

  // S4f.17: admit the interval as a deferred job. The transport edge (rewind, destination state
  // and the record-stop playback handoff) runs inside the admission, so the live press and the
  // accepted-stop drive take exactly the same route. The pass, the `RecordPassAdded` undo entry
  // and persistence come from the worker's publication, not from here.
  if (!beginDeferredRecordStopForSlot(PendingStopDestination::Playing, currentTick,
                                      recordedSlotIndex, interval)) {
    // Capacity denial, not a refusal (DEC-067 / invariant 31): the interval keeps recording with
    // its rows, pending notes and reservations intact, and the deferred drive admits the retained
    // obligation exactly once when the finalization owner frees. The user never presses twice.
    latchAcceptedStop(PendingStopDestination::Playing, recordedSlotIndex, interval);
    logRecordStopStage(loop, stopPathStartUs, "finalize", micros() - stopPathStartUs,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       DebugSessionCapture::kUnsampledHeapBytes, "accepted_wait");
    return;
  }
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  const uint32_t edgeUs = micros() - stopPathStartUs;
  logRecordStopStage(loop, stopPathStartUs, "finalize", edgeUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "state_advance", edgeUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     trackState == TRACK_PLAYING ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
}

TRACK_COLD_MEM void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!isRecording()) return;
  // The STOPPED destination never aligns the take's origin; clear the request the same way the
  // inline route did before it touched anything.
  alignLoopOriginOnNextStop = false;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  const uint64_t interval = captureInterval_;
  Loop& loop = getLoop(recordedSlotIndex);
  const uint32_t stopPathStartUs = micros();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "entered");

  // S4f.17: same admission as the PLAYING stop, with the stopped destination. See
  // `stopRecording` for why the edge lives in the admission and a denial is a retained
  // obligation rather than a dropped stop.
  if (!beginDeferredRecordStopForSlot(PendingStopDestination::Stopped, currentTick,
                                      recordedSlotIndex, interval)) {
    latchAcceptedStop(PendingStopDestination::Stopped, recordedSlotIndex, interval);
    logRecordStopStage(loop, stopPathStartUs, "finalize", micros() - stopPathStartUs,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       DebugSessionCapture::kUnsampledHeapBytes, "accepted_wait");
    return;
  }
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  const uint32_t edgeUs = micros() - stopPathStartUs;
  logRecordStopStage(loop, stopPathStartUs, "finalize", edgeUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "state_advance", edgeUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     trackState == TRACK_STOPPED ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "deferred", &stopPathStats);
}
