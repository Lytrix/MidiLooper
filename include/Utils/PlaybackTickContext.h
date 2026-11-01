//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file PlaybackTickContext.h
 * @brief Whether the current playback tick runs in the internal-clock interrupt (DEC-241).
 *
 * On the internal clock, `ClockManager::updateInternalClock` runs `TrackManager::updateAllTracks` inside the
 * `IntervalTimer` (PIT) ISR. A tick in that context is **consume-only**: it may read a qualified, already-built
 * playback source, but it never builds, allocates, normalizes, walks passes or publishes. Work it cannot consume
 * is owed to the main-loop owner instead. The flag is set by the ISR around the track update only; native fixtures
 * set it with `InterruptScope` to drive the same route.
 */
#pragma once

#include <cstdint>

namespace PlaybackTickContext {

/// RAM2 (`DMAMEM`, NOLOAD) so the RT guard adds nothing to the DTCM variables budget; `initAtBoot()` clears it before
/// the clock can start. `owed` has one bit per slot per track; `misses` counts interrupt ticks with no consumable
/// merged fallback; `servicing` is set while the main-loop owner rebuilds one.
struct State {
  volatile bool inInterrupt;
  volatile bool servicing;
  volatile uint8_t owed[8];
  volatile uint32_t misses;
};
extern State gState;

void initAtBoot();

inline bool inInterrupt() { return gState.inInterrupt; }

struct InterruptScope {
  InterruptScope() { gState.inInterrupt = true; }
  ~InterruptScope() { gState.inInterrupt = false; }
  InterruptScope(const InterruptScope&) = delete;
  InterruptScope& operator=(const InterruptScope&) = delete;
};

}  // namespace PlaybackTickContext
