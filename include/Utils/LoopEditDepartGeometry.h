#pragma once

#include <cstdint>

/// While transport is active, LOOP_EDIT slot depart must not write loopStartTick /
/// loopLengthTicks onto the live loop (transport reanchor owns the playback frame).
inline bool mayWriteLoopGeometryOnEditDepart(bool transportActive) {
  return !transportActive;
}

/// After transport reanchor zeros live loopStartTick, keep LOOP_EDIT baseline aligned.
inline void syncLoopEditBaselineFromLiveGeometry(uint32_t liveLoopStartTick,
                                                 uint32_t liveLoopLengthTicks,
                                                 uint32_t& baselineStart,
                                                 uint32_t& baselineLength) {
  baselineStart = liveLoopStartTick;
  baselineLength = liveLoopLengthTicks;
}
