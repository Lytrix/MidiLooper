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

/// playback-tick-consume-only phase 2: the **first failed predicate**, in decision order, that kept a merged-fallback
/// tick off the windowed lane. Report-only. Values are append-only; `scripts/parse_tick_routes.py` names them.
/// The tick writes one byte where a resolver predicate fails; the classification is read only when the merged
/// fallback is selected, and the row is emitted from the main loop (`TrackManager::reportPlaybackRouteRejects`).
enum class RouteReject : uint8_t {
  NotEvaluated = 0,  // no resolver predicate ran this turn
  Served = 1,        // the resolver chose a bridge this turn (never reported)
  RollingDisabled = 2,  // the image has no windowed lane (`PLAYBACK_WINDOW_ROLLING_ENABLED=0`)
  // `Track::resolveQualificationPlaybackSourceForTurn`: why the committed reader was not eligible
  InactiveSlot = 3,         // a non-active slot without a cold one-bar layered frame
  NoteEditActive = 4,
  OverdubNoLiveWindow = 5,  // overdubbing, and the live overlay window was not selected
  NotPlaying = 6,           // the transport is not PLAYING (record, queued start, stopped)
  // `Track::resolveRollingPlaybackWindow`, in its own order
  NoRuntime = 7,
  FrameIdle = 8,     // no rolling frame: the producer has not opened a demand
  FrameBlocked = 9,  // the frame is blocked; `cause` names the resolver predicate that blocked it (0: the producer)
  GateNotActiveSlot = 10,
  GateNotPlaying = 11,
  GateJam = 12,
  GateQueuedStart = 13,
  GateNoteEdit = 14,
  GateDeferredOverdubStop = 15,
  ClockNoEpoch = 16,
  ClockEpochChanged = 17,
  ClockTickSkew = 18,  // the tick is behind the frame, or is not the clock's stamped tick
  GenerationChanged = 19,
  LoopLengthChanged = 20,
  ProjectionMoved = 21,
  NoViewSet = 22,
  ColdWaiting = 23,
  ServingViewNotBound = 24,
  ServingViewNotPublished = 25,  // complete / published / storage generation
  ServingViewSourceStale = 26,   // generation / origin / revision / source currency / snapshot
  ServingViewGeometry = 27,      // start phase / span / loop length
  ServingExpired = 28,
  ServingUncovered = 29,
  ColdReadyOffBoundary = 30,
  ColdReadyRefused = 31,
  WaitingForHandoff = 32,  // a requested window is not yet due; the merged fallback covers the lead
  HandoffMissed = 33,      // the tick is past the demand's deadline (it was not adopted on it)
  CandidateNotReady = 34,  // the frame is not Ready at its deadline (the build had not finished)
  HandoffPhaseMismatch = 35,
  CandidateRejected = 36,  // `rollingCandidateReadyAtBoundary` refused the prepared window
  AdoptRefused = 37,
  Count = 38
};

/// Per-slot reason state: written by the tick (`recordMergedRouteReject`), read and emitted by the main loop.
struct SlotRouteReject {
  volatile uint8_t reason;
  volatile uint8_t cause;
  volatile uint8_t prevReason;
  uint8_t emittedReason;
  uint8_t emittedCause;
  volatile uint32_t sinceTick;  // the tick at which `reason`/`cause` took effect
  volatile uint32_t ticks;      // merged ticks under the current reason
  volatile uint32_t prevTicks;  // merged ticks under `prevReason`
  volatile uint32_t changes;
  uint32_t emittedChanges;
  uint32_t emittedMs;
  const char* emittedExit;  // the producer exit named on the last row
  uint32_t emittedTicks;    // `ticks` at the last row: a slot still on the merged fallback has advanced it
};
/// The emitter's minimum spacing per slot: a reason that flaps every tick is sampled, and `chg` carries the count.
constexpr uint32_t kRouteRejectRowSpacingMs = 50;

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
  // Phase 2: the turn's scratch (reset by the resolver, read when the merged fallback is selected) and per-slot state.
  volatile uint8_t turnReject;
  volatile uint8_t turnCause;
  SlotRouteReject reject[8][8];
  const char* producerExit[8];  // the rolling producer's last exit token per track (flash literal)
};
extern State gState;

/// One byte: the predicate that failed this turn. Called only on a resolver's non-serving exit.
inline void noteTurnReject(RouteReject why) { gState.turnReject = static_cast<uint8_t>(why); }
/// Called where the merged fallback is selected (never on a served tick): files the turn's reason under the slot.
void recordMergedRouteReject(uint8_t trackIndex, uint8_t slotIndex, uint32_t tick);
#if defined(PIO_UNIT_TEST_NATIVE)
inline SlotRouteReject routeRejectForTest(uint8_t trackIndex, uint8_t slotIndex) {
  const SlotRouteReject& s = gState.reject[trackIndex][slotIndex];
  SlotRouteReject out{};
  out.reason = s.reason; out.cause = s.cause; out.prevReason = s.prevReason;
  out.sinceTick = s.sinceTick; out.ticks = s.ticks; out.prevTicks = s.prevTicks; out.changes = s.changes;
  return out;
}
#endif

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
