//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "TickPhase.h"
#include "TrackUndo.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/MemoryMonitor.h"

bool Track::handleNoteEditFold(bool endInPlaying, uint32_t currentTick, uint32_t closeTick,
                               uint32_t stopStartUs) {
  (void)closeTick;
  if (!editManager.isNoteEditActive()) {
    return false;
  }
  Loop& loop = getActiveLoop();
  finalizePendingNotes(currentTick);
  if (!loop.capture.store.empty() && loop.loopLengthTicks > 0) {
    loop.ensureCaptureEventsSorted();
    loop.assignMissingNoteIdsInStore(loop.capture.store);
    loop.finalizeCaptureWrapWindowAtStop(currentTick);
  }
  editManager.foldLiveCaptureIntoNoteEditSession(*this);
  pendingNotes.clear();
  if (endInPlaying) {
#if defined(SESSION_CAPTURE)
    const uint32_t stateHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
    const uint32_t stateStartUs = micros();
#endif
    silenceTrackMidiOutput();
    playbackRuntime.clearAllLedgers();
    pendingNotes.clear();
    resetPlaybackState(currentTick);
    setState(TRACK_PLAYING);
#if defined(SESSION_CAPTURE)
    TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "set_state", micros() - stateStartUs,
                                stateHeapBefore, MemoryMonitor::getInternalHeapFreeBytes(),
                                "in_edit");
#endif
#if defined(SESSION_CAPTURE)
    const uint32_t flushHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
    const uint32_t flushStartUs = micros();
#endif
    SC_REC_FLUSH_PENDING_REVTS(8);
#if defined(SESSION_CAPTURE)
    TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "flush", micros() - flushStartUs,
                                flushHeapBefore, MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
    TRACK_SC_OVERDUB_STOP_MEMORY(recordAddedNoteOnCount, loop);
    logger.logTrackEvent("Overdubbing stopped", currentTick);
    logger.info("Overdub stopped (in-edit fold): events=%d, undo_entries=%d",
                static_cast<int>(loop.displayEventCountHint()), TrackUndo::getUndoCount(*this));
    const uint32_t storagePhaseTickAtStop =
        (loop.loopLengthTicks > 0)
            ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
            : 0;
    displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
    emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
#if defined(SESSION_CAPTURE)
    TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "display", 0,
                                MemoryMonitor::getInternalHeapFreeBytes(),
                                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
    HOT_PATH_TELEMETRY_REQUEST_DEFERRED_SUMMARY("overdub_stop");
    armPlayingMidiDrainAfterOverdubStop();
  } else {
    TRACK_SC_OVERDUB_STOP_MEMORY(recordAddedNoteOnCount, loop);
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
    HOT_PATH_TELEMETRY_REQUEST_DEFERRED_SUMMARY("overdub_stop_to_stopped");
  }
  return true;
}

void Track::armOverdubPreRoll() {
  if (!isPlaying()) {
    clearOverdubPreRoll();
    return;
  }
  overdubPreRollArmed_ = true;
}

void Track::clearOverdubPreRoll() {
  overdubPreRollArmed_ = false;
  overdubPreRollNotes.clear();
}

void Track::startOverdubbing(uint32_t currentTick) {
  Loop& loopRef = getActiveLoop();
  if (trackState == TRACK_OVERDUBBING && loopRef.capture.phase == CapturePhase::Overdub) {
    return;
  }
  playingMidiDrainAfterOverdubStop_ = false;
  playingMidiDrainAfterOverdubStopIdleNoted_ = false;
  loopPrefixMeasureAfterUndo_ = false;
  loopPrefixMeasureAfterUndoNoted_ = false;
#if defined(SESSION_CAPTURE)
  const uint32_t telemetryStartUs = micros();
  const uint32_t heapAtEnter = MemoryMonitor::getInternalHeapFreeBytes();
  SC_ODUB_STAGE("enter", 0, heapAtEnter, heapAtEnter, "ok");
#endif
  const Loop& active = loopRef;
  if (trackState == TRACK_EMPTY && active.loopLengthTicks > 0) {
    forceSetState(TRACK_STOPPED);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t stateAdvanceStartUs = micros();
#endif
  if (!setState(TRACK_OVERDUBBING)) {
#if defined(SESSION_CAPTURE)
    SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs, heapAtEnter,
                  MemoryMonitor::getInternalHeapFreeBytes(), "failed");
#endif
    return;
  }
#if defined(SESSION_CAPTURE)
  SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
  recordAddedNoteOnCount = 0;
  auto preRoll = std::move(overdubPreRollNotes);
  overdubPreRollArmed_ = false;
  Loop& loop = getActiveLoop();
  uint32_t playheadPhase = 0;
  if (loop.loopLengthTicks > 0) {
    playheadPhase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t captureStartUs = micros();
#endif
  loop.openOverdubSession(playheadPhase);
  loop.beginCapture(CapturePhase::Overdub, playheadPhase);
#if defined(SESSION_CAPTURE)
  if (!preRoll.empty()) {
    logger.info("Overdub pre-roll armed notes: %u", static_cast<unsigned>(preRoll.size()));
  }
#endif
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  for (const auto& entry : preRoll) {
    const PendingNote& preRollPending = entry.second;
    PendingNote pending{preRollPending.note, preRollPending.channel, currentTick,
                        preRollPending.velocity};
    // Start path must stay deterministic: avoid per-note hold snapshot/catch-up work here.
    // Use current ledger occupancy only; regular overdub note-ons continue through the
    // full snapshot path once overdubbing is active.
    if (runtime != nullptr) {
      loop.collectOverdubNoteOnParticipantIds(pending.note, midiChannel, runtime->ledger,
                                              pending.overlapNoteIds);
    }
    pendingNotes[entry.first] = pending;
    recordMidiEvents(midi::NoteOn, preRollPending.channel, preRollPending.note,
                     preRollPending.velocity, currentTick);
  }
#if defined(SESSION_CAPTURE)
  SC_ODUB_STAGE("begin_capture", micros() - captureStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
  if (loop.loopLengthTicks > 0) {
    const uint32_t phase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    projectionCycleStartTick =
        static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t undoStartUs = micros();
#endif
  TrackUndo::beginOverdubSession(*this);
#if defined(SESSION_CAPTURE)
  SC_ODUB_STAGE("undo_session", micros() - undoStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
#if defined(SESSION_CAPTURE)
  HOT_PATH_TELEMETRY_RECORD_OVERDUB_START(
      micros() - telemetryStartUs, static_cast<uint32_t>(loop.displayEventCountHint()),
      static_cast<uint32_t>(TrackUndo::getUndoCount(*this)));
  SC_ODUB_STAGE("complete", micros() - telemetryStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
  logger.logTrackEvent("Overdubbing started", currentTick);
}


void Track::stopOverdubbing() {
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
#if defined(SESSION_CAPTURE)
  const uint32_t stopStartUs = micros();
  const uint32_t heapAtEnter = MemoryMonitor::getInternalHeapFreeBytes();
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "enter", 0, heapAtEnter, heapAtEnter, "entered");
#else
  const uint32_t stopStartUs = 0;
#endif
  SC_REC_FLUSH_PENDING_REVTS(8);
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(true, currentTick, closeTick, stopStartUs)) {
    loop.closeOverdubSession();
    return;
  }
  finalizePendingNotes(currentTick);
#if defined(SESSION_CAPTURE)
  const uint32_t sealHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t sealStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::OverdubStop, currentTick, closeTick);
  // Stage order seal → finalize preserved; duration covers seal+finalize together on seal.
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "seal", micros() - sealStartUs, sealHeapBefore,
                              MemoryMonitor::getInternalHeapFreeBytes(),
                              commitResultLabel(sideEffectResult));
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "finalize", 0,
                              MemoryMonitor::getInternalHeapFreeBytes(),
                              MemoryMonitor::getInternalHeapFreeBytes(),
                              commitResultLabel(sideEffectResult));
#else
  (void)commitCaptureForStop(CommitReason::OverdubStop, currentTick, closeTick);
#endif
#if defined(SESSION_CAPTURE)
  const uint32_t stateHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t stateStartUs = micros();
#endif
  // Silence this track only, then resume loop playback. Do not CC123 every channel —
  // that mutes other playing tracks. Transport stop still uses sendAllNotesOff().
  silenceTrackMidiOutput();
  playbackRuntime.clearAllLedgers();
  pendingNotes.clear();
  resetPlaybackState(currentTick);
  setState(TRACK_PLAYING);
#if defined(SESSION_CAPTURE)
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "set_state", micros() - stateStartUs,
                              stateHeapBefore, MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
#if defined(SESSION_CAPTURE)
  const uint32_t flushHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t flushStartUs = micros();
#endif
  SC_REC_FLUSH_PENDING_REVTS(8);
#if defined(SESSION_CAPTURE)
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "flush", micros() - flushStartUs, flushHeapBefore,
                              MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
  TRACK_SC_OVERDUB_STOP_MEMORY(recordAddedNoteOnCount, loop);
  logger.logTrackEvent("Overdubbing stopped", currentTick);
  logger.info("Overdub stopped: events=%d, undo_entries=%d", static_cast<int>(loop.displayEventCountHint()),
              TrackUndo::getUndoCount(*this));

  const uint32_t storagePhaseTickAtStop =
      (loop.loopLengthTicks > 0)
          ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
          : 0;
  displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
#if defined(SESSION_CAPTURE)
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "display", 0,
                              MemoryMonitor::getInternalHeapFreeBytes(),
                              MemoryMonitor::getInternalHeapFreeBytes(), "ok");
#endif
  HOT_PATH_TELEMETRY_REQUEST_DEFERRED_SUMMARY("overdub_stop");
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

void Track::stopOverdubbingToStopped() {
  if (isEmpty()) return;
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
#if defined(SESSION_CAPTURE)
  const uint32_t stopStartUs = micros();
  const uint32_t heapAtEnter = MemoryMonitor::getInternalHeapFreeBytes();
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "enter", 0, heapAtEnter, heapAtEnter, "entered");
#else
  const uint32_t stopStartUs = 0;
#endif
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(false, currentTick, closeTick, stopStartUs)) {
    loop.closeOverdubSession();
    return;
  }
  finalizePendingNotes(currentTick);
#if defined(SESSION_CAPTURE)
  const uint32_t sealHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t sealStartUs = micros();
  const CommitResult commitResult =
      commitCaptureForStop(CommitReason::OverdubStopToStopped, currentTick, closeTick);
  TRACK_SC_OVERDUB_STOP_STAGE(loop, stopStartUs, "seal", micros() - sealStartUs, sealHeapBefore,
                              MemoryMonitor::getInternalHeapFreeBytes(),
                              commitResultLabel(commitResult));
#else
  (void)commitCaptureForStop(CommitReason::OverdubStopToStopped, currentTick, closeTick);
#endif
  silenceTrackMidiOutput();
  TRACK_SC_OVERDUB_STOP_MEMORY(recordAddedNoteOnCount, loop);
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
  HOT_PATH_TELEMETRY_REQUEST_DEFERRED_SUMMARY("overdub_stop_to_stopped");
  loop.closeOverdubSession();
}
