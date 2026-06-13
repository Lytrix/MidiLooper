//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file ClockTransportUtils.h
 * @brief Host-testable helpers for MIDI transport output when internal vs external clock.
 */
#pragma once

#include <stdint.h>

namespace ClockTransportUtils {

/// @p clockSource: 0 = CLOCK_INTERNAL, 1 = CLOCK_EXTERNAL (see ClockManager.h).
/// True when slaved to an external clock that is still pulsing within @p timeoutUs.
inline bool isExternalMidiClockActive(int clockSource, uint32_t lastPulseMicros,
                                      uint32_t nowMicros, uint32_t timeoutUs) {
  constexpr int kExternal = 1;
  if (clockSource != kExternal) return false;
  return (nowMicros - lastPulseMicros) <= timeoutUs;
}

/// True when this device should emit MIDI Start/Stop/Clock on DIN/USB (no active external master).
inline bool shouldEmitMidiTransportAsMaster(int clockSource, uint32_t lastPulseMicros,
                                            uint32_t nowMicros, uint32_t timeoutUs) {
  return !isExternalMidiClockActive(clockSource, lastPulseMicros, nowMicros, timeoutUs);
}

}  // namespace ClockTransportUtils
