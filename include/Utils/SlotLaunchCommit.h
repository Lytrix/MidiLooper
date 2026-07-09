#pragma once

#include <cstdint>

#include "TickPhase.h"
#include "Utils/IntervalProjection.h"

/// Loop-end commit uses the same projection-cycle wrap detection as playback
/// (`Track::playMidiEvents` atLoopStart / display wrap), evaluated before
/// `lastTickInLoop` is updated for the current tick.
inline bool shouldCommitLoopEndSwitch(uint32_t playbackTick, int32_t projectionCycleStartTick,
                                      uint32_t loopLengthTicks, uint32_t loopStartTick,
                                      uint32_t startLoopTick, uint32_t lastTickInLoop) {
  if (loopLengthTicks == 0) {
    return false;
  }
  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      playbackTick, projectionCycleStartTick, loopLengthTicks);
  if (lastTickInLoop != UINT32_MAX &&
      IntervalProjection::didDisplayPlayheadWrapBackward(tickInLoop, lastTickInLoop, loopStartTick,
                                                         loopLengthTicks)) {
    return true;
  }
  // When playback indices were never advanced (fresh re-anchor), fall back to the
  // legacy storage-phase-zero grid used before projection wrap tracking.
  if (lastTickInLoop == UINT32_MAX) {
    return tickPhaseInLoop(playbackTick, startLoopTick, loopLengthTicks) == 0;
  }
  return false;
}
