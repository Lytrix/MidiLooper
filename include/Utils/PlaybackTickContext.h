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
/// The playback source a slot-tick consumed (playback-tick-consume-only phase 1), in emission priority.
enum class TickRoute : uint8_t { View = 0, Bridge = 1, Paged = 2, Merged = 3, None = 4, Count = 5 };

struct State {
  volatile bool inInterrupt;
  volatile bool servicing;
  volatile uint8_t owed[8];
  volatile uint32_t misses;          // interrupt-tick misses (DEC-241)
  volatile uint32_t missesExternal;  // main-loop-tick misses (0 until the external tick is consume-only)
  volatile uint32_t route[static_cast<int>(TickRoute::Count)];
  // Totals at the previous telemetry window, so the row reports deltas.
  uint32_t emittedRoute[static_cast<int>(TickRoute::Count)];
  uint32_t emittedMissInterrupt;
  uint32_t emittedMissExternal;
};
extern State gState;

void initAtBoot();

/// One indexed increment: the only hot-path cost of the route telemetry.
inline void noteTickRoute(TickRoute route) {
  const int index = static_cast<int>(route);
  gState.route[index] = gState.route[index] + 1u;
}

struct RouteCounts {
  uint32_t route[static_cast<int>(TickRoute::Count)];
  uint32_t missInterrupt;
  uint32_t missExternal;
};
/// A copy of the running totals (telemetry takes deltas between copies).
RouteCounts routeCounts();
/// Deltas since the previous call (the 5 s telemetry window); advances the emitted snapshot.
RouteCounts takeRouteWindow();
#if defined(PIO_UNIT_TEST_NATIVE)
inline RouteCounts routeCountsForTest() { return routeCounts(); }
#endif

inline bool inInterrupt() { return gState.inInterrupt; }

struct InterruptScope {
  InterruptScope() { gState.inInterrupt = true; }
  ~InterruptScope() { gState.inInterrupt = false; }
  InterruptScope(const InterruptScope&) = delete;
  InterruptScope& operator=(const InterruptScope&) = delete;
};

}  // namespace PlaybackTickContext
