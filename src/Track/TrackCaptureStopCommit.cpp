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
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/RecordStopLength.h"
#include "Utils/TrackMem.h"
#include "TickPhase.h"

extern TrackManager trackManager;

namespace {

TRACK_COLD_MEM void logRecordStopSaveRequestAndPersist(Track& track, Loop& loop,
                                                       uint32_t stopPathStartUs,
                                                       uint8_t recordedSlotIndex,
                                                       uint32_t heapAfter,
                                                       CommitResult sideEffectResult,
                                                       const StopPathStorageStats& stopPathStats) {
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, heapAfter, heapAfter,
                     sideEffectResult == CommitResult::Committed ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Committed) {
    requestLoopSlotPersistAndSaveState(track, recordedSlotIndex, heapAfter);
  }
}

TRACK_COLD_MEM void resetAndLogEmptyRecordStop(Loop& loop, uint32_t stopPathStartUs,
                                               uint32_t heapValue,
                                               const char* saveRequestOutcome,
                                               const StopPathStorageStats& stopPathStats) {
  resetActiveLoopAfterEmptyCapture(loop);
  logRecordStopStage(loop, stopPathStartUs, "state_advance", 0, heapValue, heapValue,
                     "skipped_empty", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, heapValue, heapValue,
                     saveRequestOutcome, &stopPathStats);
}

TRACK_COLD_MEM void logRecordStopPathEntry(Loop& loop, uint32_t& stopPathStartUs,
                                           uint32_t& stopHeap) {
  stopPathStartUs = micros();
  stopHeap = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0, stopHeap, stopHeap, "entered");
}

TRACK_COLD_MEM void logRecordStopStateAdvance(Loop& loop, uint32_t stopPathStartUs,
                                              uint32_t stateAdvanceDurationUs,
                                              uint32_t stateAdvanceHeapBefore,
                                              uint32_t stateAdvanceHeapAfter,
                                              bool stateAdvanceSucceeded,
                                              const StopPathStorageStats& stopPathStats) {
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     stateAdvanceSucceeded ? "ok" : "failed", &stopPathStats);
}

struct RecordStopFinalizeContext {
  CommitResult sideEffectResult = CommitResult::Skipped;
  StopPathStorageStats stopPathStats{};
};

TRACK_COLD_MEM RecordStopFinalizeContext finalizeRecordStopCommitAndLog(
    Track& track, Loop& loop, CommitReason reason, uint32_t currentTick,
    uint32_t stopPathStartUs) {
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult = track.commitCaptureForStop(reason, currentTick, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const uint32_t finalizeHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult), &stopPathStats);
  const char* requestOutcome = sideEffectResult == CommitResult::Committed ? "deferred" : "skipped";
  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter, requestOutcome, &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
                     requestOutcome, &stopPathStats);
  return {sideEffectResult, stopPathStats};
}

TRACK_COLD_MEM uint32_t applyRecordStopTruncationRewind(uint32_t currentTick, uint32_t rawLength,
                                                        uint32_t finalLength,
                                                        bool logRewindDetails) {
  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks =
      RecordStopLength::computeTruncationRewindTicks(rawLength, finalLength);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    clockManager.assignCurrentTickSilently(playbackTick);
    if (logRewindDetails) {
      logger.log(
          CAT_TRACK, LOG_INFO,
          "Record stop truncation rewind: raw=%lu final=%lu rewind=%lu playbackTick=%lu positionInBar=%lu",
          rawLength, finalLength, rewindTicks, playbackTick, rawLength % Config::TICKS_PER_BAR);
    }
  }
  return playbackTick;
}

TRACK_COLD_MEM uint32_t reanchorLoopPlaybackAfterRecordStop(Loop& loop, uint32_t playbackTick,
                                                            uint32_t recordStartTick,
                                                            uint32_t finalLength) {
  loop.nextEventIndex = 0;
  loop.startLoopTick = recordStartTick;
  loop.lastTickInLoop =
      (finalLength > 0) ? tickPhaseInLoop(playbackTick, recordStartTick, finalLength) : 0;
  loop.invalidatePlaybackCaches();
  return loop.lastTickInLoop;
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
  const bool deferFullValidate = recordStop;
  auto scheduleDeferredValidateOnly = [&]() {
    const bool hasCommittedPasses = loop.hasCommittedPasses() && loop.loopLengthTicks > 0;
    deferredFullMidiValidate = deferFullValidate && hasCommittedPasses;
    deferredValidateQueuedAtMs = deferredFullMidiValidate ? millis() : 0;
  };
  auto persistActiveLoopAfterOverdubStop = [&]() {
    requestActiveLoopSlotPersistAndSaveState(*this, MemoryMonitor::getInternalHeapFreeBytes());
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
        if (!editManager.isNoteEditActive()) {
          TrackUndo::pushOverdubSessionOnStop(*this, getActiveLoopIndex(), kInvalidPassId, {},
                                              false);
        }
        persistActiveLoopAfterOverdubStop();
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
      const bool isRecordPass =
          loop.passes.hasRecordPass() && loop.passes.recordPass.id == undoPassId;
      EditPassIdList companionIds;
      if (isRecordPass) {
        TrackUndo::pushRecordPassAdded(*this, getActiveLoopIndex(), undoPassId);
        loop.markDisplayCachesStale();
      } else if (!editManager.isNoteEditActive()) {
        // Dual-storage encoding: OverdubPass already published; seal Shorten/Hide companions.
        companionIds = loop.sealPendingNoteChangesToEditPasses();
        TrackUndo::pushOverdubSessionOnStop(*this, getActiveLoopIndex(), undoPassId, companionIds,
                                            true);
        if (overdubStop) {
          loop.markAffectedDisplayCacheRanges(undoPassId, companionIds);
        } else {
          loop.markDisplayCachesStale();
        }
      } else if (overdubStop) {
        loop.markAffectedDisplayCacheRanges(undoPassId, EditPassIdList{});
      } else {
        loop.markDisplayCachesStale();
      }
      if (!isRecordPass) {
        for (const OverdubPass& pass : loop.passes.overdubPasses) {
          if (pass.id == undoPassId) {
            LoopContentResolution::publishPreparedOverdubPass(pass, loop.playbackRevision,
                                                              loop.loopLengthTicks,
                                                              loop.passes.editPasses, companionIds);
            break;
          }
        }
      }
      if (overdubStop) {
        persistActiveLoopAfterOverdubStop();
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
      queueDeferredStoredMidiVerification();
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

uint32_t Track::prepareRecordStopAndClearPendingNotes(uint32_t currentTick,
                                                      const char* guardLabel) {
  const uint32_t rawLength = prepareRecordStop(currentTick, guardLabel);
  pendingNotes.clear();
  return rawLength;
}

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  [[maybe_unused]] const bool captureAlignFlag = alignLoopOriginOnNextStop;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  uint32_t stopPathStartUs = 0;
  uint32_t stopHeap = 0;
  logRecordStopPathEntry(loop, stopPathStartUs, stopHeap);

  const uint32_t rawLength =
      prepareRecordStopAndClearPendingNotes(currentTick, "stopRecording");

  // Validate AFTER loopLengthTicks is known so wrap-matching and open-tail closing
  // (the second pass and synthetic note-offs) are active for this record-stop.
  // Record-stop must close open tails at loop end, not at the stop playhead tick.
  const RecordStopFinalizeContext finalizeContext = finalizeRecordStopCommitAndLog(
      *this, loop, CommitReason::RecordStop, currentTick, stopPathStartUs);
  const CommitResult sideEffectResult = finalizeContext.sideEffectResult;
  const StopPathStorageStats& stopPathStats = finalizeContext.stopPathStats;

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

  uint32_t recordStartTick = loop.startLoopTick;
  uint32_t finalLength = loop.loopLengthTicks;
  uint32_t playbackTick =
      applyRecordStopTruncationRewind(currentTick, rawLength, finalLength, true);
  const uint32_t storagePhaseTickAtStop =
      reanchorLoopPlaybackAfterRecordStop(loop, playbackTick, recordStartTick, finalLength);
  if (finalLength > 0) {
    projectionCycleStartTick =
        static_cast<int32_t>(playbackTick) - static_cast<int32_t>(loop.lastTickInLoop);
  }
  SC_REC_STOP("stop", activeLoopIndex, playbackTick, recordStartTick, rawLength, finalLength, captureAlignFlag);
  logger.logTrackEvent("Recording stopped", playbackTick, "recStart=%lu length=%lu",
                       static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(finalLength));
  logger.debug("Final ticks: playbackTick=%lu recStart=%lu rawLength=%lu length=%lu", playbackTick,
               static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(rawLength),
               static_cast<unsigned long>(finalLength));

  // Empty record-stop: reset capture slot geometry so hasDataInSlot stays false.
  if (loop.loopLengthTicks == 0 || loop.activeCapturePassCount() == 0) {
    resetAndLogEmptyRecordStop(loop, stopPathStartUs, stopHeap,
                               sideEffectResult == CommitResult::Committed ? "requested"
                                                                           : "skipped",
                               stopPathStats);
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
  logRecordStopStateAdvance(loop, stopPathStartUs, stateAdvanceDurationUs, stateAdvanceHeapBefore,
                            stateAdvanceHeapAfter, trackState == TRACK_PLAYING, stopPathStats);
  logRecordStopSaveRequestAndPersist(*this, loop, stopPathStartUs, recordedSlotIndex,
                                     stateAdvanceHeapAfter, sideEffectResult, stopPathStats);
}

TRACK_COLD_MEM void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  alignLoopOriginOnNextStop = false;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  uint32_t stopPathStartUs = 0;
  uint32_t stopHeap = 0;
  logRecordStopPathEntry(loop, stopPathStartUs, stopHeap);

  const uint32_t rawLength =
      prepareRecordStopAndClearPendingNotes(currentTick, "stopRecordingToStopped");

  // Validate AFTER loopLengthTicks is known (see stopRecording for rationale).
  const RecordStopFinalizeContext finalizeContext = finalizeRecordStopCommitAndLog(
      *this, loop, CommitReason::RecordStopToStopped, currentTick, stopPathStartUs);
  const CommitResult sideEffectResult = finalizeContext.sideEffectResult;
  const StopPathStorageStats& stopPathStats = finalizeContext.stopPathStats;

  [[maybe_unused]] const uint32_t recordStartTickStopped = loop.startLoopTick;
  const uint32_t finalLength = loop.loopLengthTicks;
  uint32_t playbackTick =
      applyRecordStopTruncationRewind(currentTick, rawLength, finalLength, false);
  reanchorLoopPlaybackAfterRecordStop(loop, playbackTick, recordStartTickStopped, finalLength);

  SC_REC_STOP("stopToStopped", activeLoopIndex, playbackTick, recordStartTickStopped,
              rawLength, finalLength, false);
  logger.logTrackEvent("Recording stopped (to STOPPED)", playbackTick, "length=%lu",
                       static_cast<unsigned long>(finalLength));

  const uint32_t stateAdvanceHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  if (!loop.hasData()) {
    resetAndLogEmptyRecordStop(loop, stopPathStartUs, stateAdvanceHeapBefore, "skipped",
                               stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  const uint32_t stateAdvanceStartUs = micros();
  setState(TRACK_STOPPED);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStateAdvance(loop, stopPathStartUs, stateAdvanceDurationUs, stateAdvanceHeapBefore,
                            stateAdvanceHeapAfter, trackState == TRACK_STOPPED, stopPathStats);
  logRecordStopSaveRequestAndPersist(*this, loop, stopPathStartUs, recordedSlotIndex,
                                     stateAdvanceHeapAfter, sideEffectResult, stopPathStats);
}
