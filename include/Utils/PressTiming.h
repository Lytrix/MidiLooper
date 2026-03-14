//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file PressTiming.h
 * @brief Shared timing constants for press-type detection (short, long, double, triple).
 * Used by MidiButtonProcessor and BarStepButtonHandler for consistent behavior.
 */
#ifndef PRESS_TIMING_H
#define PRESS_TIMING_H

#include <cstdint>

namespace PressTiming {
  // Default timing (ms) - matches MidiButtonConfig defaults
  constexpr uint32_t DOUBLE_TAP_WINDOW = 300;
  constexpr uint32_t TRIPLE_TAP_WINDOW = 400;
  constexpr uint32_t LONG_PRESS_TIME = 600;
  // Gap (ms) between first and second press required for HOLD_TWO. Must be > LONG_PRESS_TIME
  // to require "hold one, then add second". 800ms avoids quick taps ~0.5s apart + MIDI latency.
  constexpr uint32_t HOLD_TWO_MIN_GAP_MS = 800;
}

#endif // PRESS_TIMING_H
