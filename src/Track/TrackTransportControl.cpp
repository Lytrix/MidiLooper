//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "DisplayManager.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/IntervalProjection.h"

// -------------------------
// Start playing
// -------------------------

void Track::startPlaying(uint32_t currentTick, bool preserveLoopPhaseOrigin) {
  Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks > 0) {
    if (trackState == TRACK_EMPTY) {
      forceSetState(TRACK_STOPPED);
    }
    if (!setState(TRACK_PLAYING)) return;
    reanchorPlaybackProjection(currentTick, preserveLoopPhaseOrigin);
    logger.logTrackEvent("Playback started", currentTick);
  }
}

// -------------------------
// Stop playing
// -------------------------

void Track::stopPlaying() {
  if (isEmpty()) return; // Nothing to stop, empty track
  sendAllNotesOff();  // first kill all sounding notes

  // then transition to the stopped state
  setState(TRACK_STOPPED);
  logger.logTrackEvent("Playback stopped", clockManager.getCurrentTick());
  displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, clockManager.getCurrentTick());
  HotPathTelemetry::requestDeferredSummary("playback_stop");
}

// -------------------------
// Toggle play/stop
// -------------------------

void Track::togglePlayStop() {
  isPlaying() ? stopPlaying() : startPlaying(clockManager.getCurrentTick());
}

void Track::queuePlaybackStartAtGrid(int32_t startTick, uint32_t queuedAtTick) {
  useQueuedStart = true;
  queuedStartTick = startTick;
  queuedStartQueuedAtTick = queuedAtTick;
  getActiveLoop().nextEventIndex = 0;
  getActiveLoop().lastTickInLoop = UINT32_MAX;
}

void Track::clearQueuedPlaybackStart() {
  useQueuedStart = false;
  queuedStartTick = 0;
  queuedStartQueuedAtTick = UINT32_MAX;
}

bool Track::shouldCommitQueuedPlaybackStart(uint32_t currentTick) const {
  if (!useQueuedStart) {
    return false;
  }
  if (currentTick == queuedStartQueuedAtTick) {
    return false;
  }
  if (queuedStartGridTicks == 0) {
    return false;
  }
  return (currentTick % queuedStartGridTicks) == 0;
}

void Track::commitQueuedPlaybackStart(uint32_t commitTick) {
  if (!useQueuedStart) {
    return;
  }
  Loop& loop = getActiveLoop();
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0) {
    clearQueuedPlaybackStart();
    return;
  }
  const uint32_t startPhase = IntervalProjection::noteRelativeTick(
      static_cast<uint32_t>(queuedStartTick), loop.loopStartTick, loopLength);
  projectionCycleStartTick =
      static_cast<int32_t>(commitTick) - static_cast<int32_t>(startPhase);
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = UINT32_MAX;
  clearQueuedPlaybackStart();
}
