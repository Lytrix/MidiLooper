#pragma once

#include <cstdint>

/// While transport is active, LOOP_EDIT slot depart must not write loopStartTick /
/// loopLengthTicks onto the live loop (transport reanchor owns the playback frame).
inline bool mayWriteLoopGeometryOnEditDepart(bool transportActive) {
  return !transportActive;
}

/// Capture owns loop length for the session. Physical length/start fader noise
/// (HITL `170743` / `165541`: ch14 pitchbend → 2304→3072 mid-overdub) must not
/// resize the loop while RECORDING or OVERDUBBING.
inline bool shouldIgnoreLoopGeometryFaderDuringCapture(bool recording, bool overdubbing) {
  return recording || overdubbing;
}

/// After transport reanchor zeros live loopStartTick, keep LOOP_EDIT baseline aligned.
inline void syncLoopEditBaselineFromLiveGeometry(uint32_t liveLoopStartTick,
                                                 uint32_t liveLoopLengthTicks,
                                                 uint32_t& baselineStart,
                                                 uint32_t& baselineLength) {
  baselineStart = liveLoopStartTick;
  baselineLength = liveLoopLengthTicks;
}
