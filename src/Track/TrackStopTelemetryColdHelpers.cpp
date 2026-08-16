//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <cstring>

#include "DisplayManager.h"
#include "LoopEventStore.h"
#include "TrackManager.h"
#include "TrackStateMachine.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

extern TrackManager trackManager;

namespace {

bool isSamePitchSoundingAtTick(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                               uint32_t tick) {
  for (const NoteUtils::DisplayNote& displayNote : notes) {
    if (displayNote.note != pitch) {
      continue;
    }
    if (tick >= displayNote.startTick && tick < displayNote.endTick) {
      return true;
    }
  }
  return false;
}

}  // namespace

TRACK_INTERNAL_MEM bool shouldRestoreCommittedOverlapOnOverdubStop(const Loop& loop, uint8_t note,
                                                                   uint32_t pendingOnPhaseTick,
                                                                   uint32_t closePhaseTick) {
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    return false;
  }
  SessionMidiEventVec committedEvents;
  loop.passes.materializeToEventVector(committedEvents, loop.loopLengthTicks);
  if (committedEvents.empty()) {
    return false;
  }
  const NoteUtils::DisplayNoteVec reconstructed =
      NoteUtils::reconstructDisplayNotes(committedEvents, loop.loopLengthTicks, false);
  if (isSamePitchSoundingAtTick(reconstructed, note, pendingOnPhaseTick)) {
    return true;
  }
  return isSamePitchSoundingAtTick(reconstructed, note, closePhaseTick);
}

TRACK_INTERNAL_MEM StopPathStorageStats collectStopPathStorageStats(const Loop& loop,
                                                                    bool includeCaptureBuffer) {
  StopPathStorageStats stats{};
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active) {
    stats.chunkRefCount += loop.passes.recordPass.committedChunkIds.size();
    stats.eventCount += LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state != CapturePassState::Active) {
      continue;
    }
    stats.chunkRefCount += pass.committedChunkIds.size();
    stats.eventCount += LoopEventStore::countEventsInChunkIds(pass.committedChunkIds);
  }
  if (loop.hasPendingCapturePass()) {
    const PendingCapturePass& pending = loop.pendingCapturePass();
    stats.chunkRefCount += pending.committedChunkIds.size();
    stats.eventCount += LoopEventStore::countEventsInChunkIds(pending.committedChunkIds);
  }
  if (includeCaptureBuffer && loop.captureActive()) {
    stats.eventCount += loop.capture.store.size();
  }
  return stats;
}

TRACK_INTERNAL_MEM const char* commitResultLabel(CommitResult result) {
  switch (result) {
    case CommitResult::Skipped:
      return "skipped";
    case CommitResult::Committed:
      return "published";  // CAP wire token — HITL legacy_record_baseline completed_outcomes
    case CommitResult::SealFailed:
      return "seal_failed";
  }
  return "unknown";
}

TRACK_INTERNAL_MEM void logRecordStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                                           uint32_t stageDurationUs, uint32_t heapBefore,
                                           uint32_t heapAfter, const char* outcome,
                                           const StopPathStorageStats* cachedStats) {
  const StopPathStorageStats stats =
      cachedStats ? *cachedStats : collectStopPathStorageStats(loop);
  const uint32_t elapsedUs = micros() - stopStartUs;
  SC_REC_STOP_STAGE(stage, elapsedUs, stageDurationUs, heapBefore, heapAfter, stats.eventCount,
                    stats.chunkRefCount, outcome);
  // Record-stop publish DIAG emits from Loop::commitCapturePass (seal/publish stages live there).
}

TRACK_INTERNAL_MEM void logOverdubStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                                           uint32_t stageDurationUs, uint32_t heapBefore,
                                           uint32_t heapAfter, const char* outcome,
                                           const StopPathStorageStats* cachedStats) {
  const StopPathStorageStats stats =
      cachedStats ? *cachedStats : collectStopPathStorageStats(loop, false);
  const uint32_t elapsedUs = micros() - stopStartUs;
  SC_ODUB_STOP_STAGE(stage, elapsedUs, stageDurationUs, heapBefore, heapAfter, stats.eventCount,
                     stats.chunkRefCount, outcome);
  if (stage != nullptr && std::strcmp(stage, "seal") == 0) {
    loop.emitOverlapHoldTotals();
  }
}

TRACK_INTERNAL_MEM void emitOverdubStopDisplaySnapshot(Track& track, uint8_t displaySlot,
                                                     uint32_t currentTick) {
  const Loop& loop = track.getLoop(displaySlot);
  if (LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
          MemoryMonitor::getInternalHeapFreeBytes())) {
    displayManager.emitDisplayCaptureSnapshot(track, displaySlot, currentTick);
    return;
  }
  SC_DISP(displaySlot, TrackStateMachine::toString(track.getState()), loop.loopLengthTicks, 0, 0, 0,
          0, loop.hasCommittedPasses() ? 1 : 0);
}

TRACK_INTERNAL_MEM void logMemoryAfterOverdubStop(uint32_t overdubNoteOns, const Loop& loop) {
  const StopPathStorageStats stats = collectStopPathStorageStats(loop, false);
  MemoryMonitor::logStatusAtAddedNotes(overdubNoteOns, stats.eventCount, nullptr,
                                       stats.chunkRefCount, stats.chunkRefCount > 0);
}

TRACK_INTERNAL_MEM uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    if (&trackManager.getTrack(i) == &track) {
      return i;
    }
  }
  return trackManager.getSelectedTrackIndex();
}

TRACK_INTERNAL_MEM void resetActiveLoopAfterEmptyCapture(Loop& loop) {
  loop.discardCapture();
  loop.resetPassTimeline();
  loop.loopLengthTicks = 0;
  loop.loopStartTick = 0;
  loop.startLoopTick = 0;
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = 0;
  loop.invalidatePlaybackCaches();
}

#if defined(SESSION_CAPTURE)
TRACK_INTERNAL_MEM void logOverdubCaptureCoordinate(const Track& track, uint32_t absTick,
                                                    uint32_t storageTick, uint8_t channel,
                                                    uint8_t note) {
  const Loop& loop = track.getActiveLoop();
  if (loop.loopLengthTicks == 0) {
    return;
  }
  const uint32_t projPhase = IntervalProjection::tickPhaseInProjectionCycle(
      absTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  const uint32_t displayPhase =
      IntervalProjection::noteRelativeTick(projPhase, loop.loopStartTick, loop.loopLengthTicks);
  SC_CAPTURE_COORD(absTick, storageTick, projPhase, displayPhase, loop.startLoopTick,
                   track.getProjectionCycleStartTick(), loop.loopStartTick, channel, note);
}
#endif
