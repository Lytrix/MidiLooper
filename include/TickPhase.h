//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Utils/IntervalProjection.h"

/// Position in loop [0, loopLengthTicks). Correct when currentTick < startLoopTick (transport
/// reset, MIDI Start) or when uint32 tick counters wrap; avoids unsigned underflow in (a-b)%L.
inline uint32_t tickPhaseInLoop(uint32_t currentTick, uint32_t startLoopTick, uint32_t loopLengthTicks) {
    return IntervalProjection::tickPhaseInLoop(currentTick, startLoopTick, loopLengthTicks);
}
