//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Position in loop [0, loopLengthTicks). Correct when currentTick < startLoopTick (transport
/// reset, MIDI Start) or when uint32 tick counters wrap; avoids unsigned underflow in (a-b)%L.
inline uint32_t tickPhaseInLoop(uint32_t currentTick, uint32_t startLoopTick, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) return 0;
  int64_t d = static_cast<int64_t>(currentTick) - static_cast<int64_t>(startLoopTick);
  int64_t L = static_cast<int64_t>(loopLengthTicks);
  int64_t m = d % L;
  if (m < 0) m += L;
  return static_cast<uint32_t>(m);
}
