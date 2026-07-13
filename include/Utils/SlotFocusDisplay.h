#pragma once

#include <cstdint>

#include "Utils/IntervalProjection.h"

/// True when preview slot differs from playing slot while transport is active on the track.
inline bool isPreviewPlayheadPending(uint8_t previewSlot, uint8_t playingSlot, bool transportActive) {
  return transportActive && previewSlot != playingSlot;
}

/// In-loop storage phase for the playhead. Active slot during transport uses the projection
/// cycle (same anchor as playMidiEvents / commitQueuedPlaybackStart); other slots use
/// startLoopTick-relative phase.
inline uint32_t resolvePlayheadStoragePhase(uint32_t currentTick, int32_t projectionCycleStartTick,
                                            uint32_t loopLength, uint8_t displaySlot,
                                            uint8_t activeSlot, bool transportActive,
                                            uint32_t displayTick, uint32_t startLoopTick) {
  if (transportActive && displaySlot == activeSlot && loopLength > 0) {
    return IntervalProjection::tickPhaseInProjectionCycle(currentTick, projectionCycleStartTick,
                                                          loopLength);
  }
  return IntervalProjection::tickPhaseInLoop(displayTick, startLoopTick, loopLength);
}

/// Toggle interval for flashing preview playhead (~250 ms).
inline bool previewPlayheadFlashVisible(uint32_t millisNow) {
  return (millisNow / 250U) % 2U == 0U;
}
