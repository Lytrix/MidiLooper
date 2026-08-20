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
                                                             uint32_t closeTick) {
  Loop& loop = getActiveLoop();
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
          TrackUndo::pushOverdubSessionOnStop(*this, getActiveLoopIndex(), kInvalidPassId, {},
                                              false);
#if defined(SESSION_CAPTURE)
          emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_push_overdub_undo",
                                            micros() - pushUndoStartUs, "ok");
#endif
        } else {
          emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_push_overdub_undo", 0,
                                            "skipped_note_edit");
        }
        const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
        const uint8_t persistSlotIndex = getActiveLoopIndex();
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
        StorageManager::admitLoopPersist(loopIdForSlot(persistSlotIndex));
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
        TrackUndo::pushRecordPassAdded(*this, getActiveLoopIndex(), undoPassId);
        loop.markDisplayCachesStale();
      } else if (!editManager.isNoteEditActive()) {
        // Dual-storage encoding: OverdubPass already published; seal Shorten/Hide companions.
#if defined(SESSION_CAPTURE)
        const uint32_t sealPendingStartUs = micros();
#endif
        companionIds = loop.sealPendingNoteChangesToEditPasses();
#if defined(SESSION_CAPTURE)
        emitOverdubStopFinalizeTraceStage(finalizeTrace, "seal_fx_seal_pending_changes",
                                          micros() - sealPendingStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
        const uint32_t pushUndoStartUs = micros();
#endif
        TrackUndo::pushOverdubSessionOnStop(*this, getActiveLoopIndex(), undoPassId, companionIds,
                                            true);
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
        const uint8_t persistSlotIndex = getActiveLoopIndex();
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
        StorageManager::admitLoopPersist(loopIdForSlot(persistSlotIndex));
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
        result = finalizeCommitSideEffects(retry, reason, closeTick);
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
      queueDeferredRecordRevts();
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
                                                         uint32_t closeTick) {
  Loop& loop = getActiveLoop();
  const CommitResult commitResult = loop.commitCapturePass(reason, commitTick);
  return finalizeCommitSideEffects(commitResult, reason, closeTick);
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
  const uint32_t lastEventTick = findLastEventTick();
  loop.loopLengthTicks = computeRecordStopLengthTicks(rawLength, lastEventTick);

  if (loop.loopLengthTicks > 0) {
    if (!pendingNotes.empty()) {
      finalizePendingNotes(currentTick);
    }
    // Record-stop truncation: events captured past final loop length must not
    // survive into committed playback state.
    loop.capture.store.dropEventsAtOrBeyondTick(loop.loopLengthTicks);
  }
  return rawLength;
}
void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  [[maybe_unused]] const bool captureAlignFlag = alignLoopOriginOnNextStop;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  const uint32_t stopPathStartUs = micros();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "entered");

  const uint32_t rawLength = prepareRecordStop(currentTick, "stopRecording");
  pendingNotes.clear();

  // Validate AFTER loopLengthTicks is known so wrap-matching and open-tail closing
  // (the second pass and synthetic note-offs) are active for this record-stop.
  // Record-stop must close open tails at loop end, not at the stop playhead tick.
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::RecordStop, currentTick, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, commitResultLabel(sideEffectResult),
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     sideEffectResult == CommitResult::Committed ? "deferred" : "skipped",
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     sideEffectResult == CommitResult::Committed ? "deferred" : "skipped",
                     &stopPathStats);

  if (alignLoopOriginOnNextStop) {
    alignLoopOriginOnNextStop = false;
    uint32_t absRecStart = loop.startLoopTick;
    uint32_t remBar = absRecStart % TICKS_PER_BAR;
    uint32_t graceBar = TICKS_PER_BAR / 2;
    uint32_t snapBar;
    if (remBar <= graceBar) {
      snapBar = absRecStart - remBar;
    } else {
      snapBar = absRecStart - remBar + TICKS_PER_BAR;
    }
    int64_t delta = (int64_t)snapBar - (int64_t)absRecStart;
    if (delta != 0 && loop.hasCommittedPasses()) {
      loop.shiftActiveCapturePassTicks(delta);
      loop.invalidatePlaybackCaches();
    }
  }

  loop.nextEventIndex = 0;
  uint32_t recordStartTick = loop.startLoopTick;
  uint32_t finalLength = loop.loopLengthTicks;

  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks =
      RecordStopLength::computeTruncationRewindTicks(rawLength, finalLength);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    clockManager.assignCurrentTickSilently(playbackTick);
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
  SC_REC_STOP("stop", activeLoopIndex, playbackTick, recordStartTick, rawLength, finalLength, captureAlignFlag);
  logger.logTrackEvent("Recording stopped", playbackTick, "recStart=%lu length=%lu",
                       static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(finalLength));
  logger.debug("Final ticks: playbackTick=%lu recStart=%lu rawLength=%lu length=%lu", playbackTick,
               static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(rawLength),
               static_cast<unsigned long>(finalLength));

  // Empty record-stop: reset capture slot geometry so hasDataInSlot stays false.
  if (loop.loopLengthTicks == 0 || loop.activeCapturePassCount() == 0) {
    resetActiveLoopAfterEmptyCapture(loop);
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       DebugSessionCapture::kUnsampledHeapBytes, "skipped_empty", &stopPathStats);
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                       &stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  // Return to playback after record-stop. Overdub starts on the next explicit
  // record press from PLAYING (record -> play -> overdub -> play flow).
  logRecordStopStage(loop, stopPathStartUs, "pre_state_advance", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "enter", &stopPathStats);
  // Silence live/held notes on the wire before loop playback catch-up; CC123 is not stored.
  sendAllNotesOff();
  resetPlaybackState(playbackTick);
  const uint32_t stateAdvanceStartUs = micros();
  startPlaying(playbackTick, true);
  displayManager.refreshViewportAfterRecordStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     trackState == TRACK_PLAYING ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Committed) {
    const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
    const uint8_t persistSlotIndex = recordedSlotIndex;
    StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
    StorageManager::admitLoopPersist(loopIdForSlot(persistSlotIndex));
    StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                             MemoryMonitor::getInternalHeapFreeBytes(), true);
  }
}

TRACK_COLD_MEM void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  alignLoopOriginOnNextStop = false;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  const uint32_t stopPathStartUs = micros();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, "entered");

  const uint32_t rawLength = prepareRecordStop(currentTick, "stopRecordingToStopped");
  pendingNotes.clear();

  // Validate AFTER loopLengthTicks is known (see stopRecording for rationale).
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::RecordStopToStopped, currentTick, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, commitResultLabel(sideEffectResult),
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     sideEffectResult == CommitResult::Committed ? "deferred" : "skipped",
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     sideEffectResult == CommitResult::Committed ? "deferred" : "skipped",
                     &stopPathStats);

  [[maybe_unused]] const uint32_t recordStartTickStopped = loop.startLoopTick;
  loop.nextEventIndex = 0;
  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks =
      RecordStopLength::computeTruncationRewindTicks(rawLength, loop.loopLengthTicks);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    clockManager.assignCurrentTickSilently(playbackTick);
  }
  loop.startLoopTick = recordStartTickStopped;
  loop.lastTickInLoop = (loop.loopLengthTicks > 0)
                            ? tickPhaseInLoop(playbackTick, recordStartTickStopped, loop.loopLengthTicks)
                            : 0;
  invalidatePlaybackCaches();

  SC_REC_STOP("stopToStopped", activeLoopIndex, playbackTick, recordStartTickStopped,
              rawLength, loop.loopLengthTicks, false);
  logger.logTrackEvent("Recording stopped (to STOPPED)", playbackTick, "length=%lu",
                       static_cast<unsigned long>(loop.loopLengthTicks));

  if (!loop.hasData()) {
    resetActiveLoopAfterEmptyCapture(loop);
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       DebugSessionCapture::kUnsampledHeapBytes, "skipped_empty", &stopPathStats);
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0,
                       DebugSessionCapture::kUnsampledHeapBytes,
                       DebugSessionCapture::kUnsampledHeapBytes, "skipped", &stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  const uint32_t stateAdvanceStartUs = micros();
  setState(TRACK_STOPPED);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     trackState == TRACK_STOPPED ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes,
                     sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Committed) {
    const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
    const uint8_t persistSlotIndex = recordedSlotIndex;
    StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
    StorageManager::admitLoopPersist(loopIdForSlot(persistSlotIndex));
    StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                             MemoryMonitor::getInternalHeapFreeBytes(), true);
  }
}
