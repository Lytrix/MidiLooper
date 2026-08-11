//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
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

void Track::finalizeLoopAtStop(uint32_t openTailCloseTick, bool scheduleDeferredFullValidate) {
  (void)openTailCloseTick;
  deferredFullMidiValidate = scheduleDeferredFullValidate;
  deferredValidateQueuedAtMs = scheduleDeferredFullValidate ? millis() : 0;
}

CommitResult Track::finalizeCommitSideEffects(CommitResult result, CommitReason reason,
                                              uint32_t closeTick) {
  Loop& loop = getActiveLoop();
  const bool recordStop = reason == CommitReason::RecordStop ||
                          reason == CommitReason::RecordStopToStopped;
  const bool overdubStop = reason == CommitReason::OverdubStop ||
                           reason == CommitReason::OverdubStopToStopped;
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
        finalizeLoopAtStop(closeTick, false);
        const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
        const uint8_t persistSlotIndex = getActiveLoopIndex();
        StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
        StorageManager::admitLoopSlotPersist(persistTrackIndex, persistSlotIndex);
        StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                                 MemoryMonitor::getInternalHeapFreeBytes(), true);
      } else {
        scheduleDeferredValidateOnly();
      }
      break;
    }
    case CommitResult::Committed: {
      const PassId undoPassId = loop.lastCommittedPassId();
      if (overdubStop) {
        finalizeLoopAtStop(closeTick, false);
      } else {
        // Record stop already finalizes wrap-window at seal; defer full validate only.
        scheduleDeferredValidateOnly();
      }
      loop.markDisplayCachesStale();
      const bool isRecordPass =
          loop.passes.hasRecordPass() && loop.passes.recordPass.id == undoPassId;
      if (isRecordPass) {
        TrackUndo::pushRecordPassAdded(*this, getActiveLoopIndex(), undoPassId);
        StorageManager::admitLoopUndoHistory(resolveTrackIndexForPersistence(*this),
                                             getActiveLoopIndex());
      } else if (!editManager.isNoteEditActive()) {
        // Dual-storage encoding: OverdubPass already published; seal Shorten/Hide companions.
        EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
        TrackUndo::pushOverdubPassAdded(*this, getActiveLoopIndex(), undoPassId,
                                        std::move(companionIds));
        StorageManager::admitLoopUndoHistory(resolveTrackIndexForPersistence(*this),
                                             getActiveLoopIndex());
      }
      if (overdubStop) {
        const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
        const uint8_t persistSlotIndex = getActiveLoopIndex();
        StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
        StorageManager::admitLoopSlotPersist(persistTrackIndex, persistSlotIndex);
        StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                                 MemoryMonitor::getInternalHeapFreeBytes(), true);
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
    invalidateCaches();
    if (recordStop) {
      queueDeferredRecordRevts();
    }
    if (overdubStop) {
      emitStoredMidiVerification();
    }
  }
  return result;
}

CommitResult Track::commitCaptureForStop(CommitReason reason, uint32_t commitTick,
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
  const uint32_t stopHeap = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0, stopHeap, stopHeap, "entered");

  const uint32_t rawLength = prepareRecordStop(currentTick, "stopRecording");
  pendingNotes.clear();

  // Validate AFTER loopLengthTicks is known so wrap-matching and open-tail closing
  // (the second pass and synthetic note-offs) are active for this record-stop.
  // Record-stop must close open tails at loop end, not at the stop playhead tick.
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::RecordStop, currentTick, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const uint32_t finalizeHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult), &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter,
                     sideEffectResult == CommitResult::Committed ? "deferred" : "skipped",
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
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
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0, stopHeap, stopHeap,
                       "skipped_empty", &stopPathStats);
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stopHeap, stopHeap,
                       sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                       &stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  // Return to playback after record-stop. Overdub starts on the next explicit
  // record press from PLAYING (record -> play -> overdub -> play flow).
  const uint32_t stateAdvanceHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "pre_state_advance", 0, stateAdvanceHeapBefore,
                     stateAdvanceHeapBefore, "enter", &stopPathStats);
  // Silence live/held notes on the wire before loop playback catch-up; CC123 is not stored.
  sendAllNotesOff();
  resetPlaybackState(playbackTick);
  const uint32_t stateAdvanceStartUs = micros();
  startPlaying(playbackTick, true);
  displayManager.refreshViewportAfterRecordStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     trackState == TRACK_PLAYING ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapAfter,
                     stateAdvanceHeapAfter,
                     sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Committed) {
    const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
    const uint8_t persistSlotIndex = recordedSlotIndex;
    StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
    StorageManager::admitLoopSlotPersist(persistTrackIndex, persistSlotIndex);
    StorageManager::requestDeferredSaveState(looperState.getLooperState(), stateAdvanceHeapAfter,
                                             true);
  }
}

TRACK_COLD_MEM void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  alignLoopOriginOnNextStop = false;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  const uint32_t stopPathStartUs = micros();
  const uint32_t stopHeap = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0, stopHeap, stopHeap, "entered");

  const uint32_t rawLength = prepareRecordStop(currentTick, "stopRecordingToStopped");
  pendingNotes.clear();

  // Validate AFTER loopLengthTicks is known (see stopRecording for rationale).
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::RecordStopToStopped, currentTick, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const uint32_t finalizeHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult), &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter,
                     sideEffectResult == CommitResult::Committed ? "deferred" : "skipped",
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
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

  const uint32_t stateAdvanceHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  if (!loop.hasData()) {
    resetActiveLoopAfterEmptyCapture(loop);
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0, stateAdvanceHeapBefore,
                       stateAdvanceHeapBefore, "skipped_empty", &stopPathStats);
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapBefore,
                       stateAdvanceHeapBefore, "skipped", &stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  const uint32_t stateAdvanceStartUs = micros();
  setState(TRACK_STOPPED);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     trackState == TRACK_STOPPED ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapAfter,
                     stateAdvanceHeapAfter,
                     sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Committed) {
    const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(*this);
    const uint8_t persistSlotIndex = recordedSlotIndex;
    StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, persistSlotIndex);
    StorageManager::admitLoopSlotPersist(persistTrackIndex, persistSlotIndex);
    StorageManager::requestDeferredSaveState(looperState.getLooperState(), stateAdvanceHeapAfter,
                                             true);
  }
}
