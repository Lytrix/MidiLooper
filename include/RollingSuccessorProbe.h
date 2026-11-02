//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

// C5.3b (DEC-246 amendment 24): the successor-lead measurement probe. **Host only** -- the device image compiles none
// of it (`PLAYBACK_ROLLING_SUCCESSOR_PROBE` is 0 on the target), and on the host it is inert until a fixture sets
// `gKnobs.enabled`.
//
// While the rolling frame serves window N (`[N.start, N.end)`, absolute ticks), the probe requester opens **one**
// successor demand for `[N.end, N.end + span)` with its absolute deadline at `N.end`, no earlier than `requestLeadTicks`
// before that deadline. The completed build is the ready successor; the resolver hands it off at `N.end` exactly, or
// names the miss. Every admission rule is the production one except the DEC-147 lead price (`leadTooShort`), which
// `bypassLeadPrice` skips for this demand only, so the measurement sees how much lead the bounded producer really needs.

#include <cstdint>

#ifndef PLAYBACK_ROLLING_SUCCESSOR_PROBE
#if defined(__IMXRT1062__)
#define PLAYBACK_ROLLING_SUCCESSOR_PROBE 0
#else
#define PLAYBACK_ROLLING_SUCCESSOR_PROBE 1
#endif
#endif

#if PLAYBACK_ROLLING_SUCCESSOR_PROBE

namespace RollingSuccessorProbe {

struct Knobs {
  bool enabled = false;
  /// Skip only the DEC-147 lead price for the successor demand; every other admission term stays.
  bool bypassLeadPrice = false;
  /// Open the successor demand no earlier than this many ticks before its deadline (`N.end`).
  uint32_t requestLeadTicks = UINT32_MAX;
};
inline Knobs gKnobs{};

/// One successor demand's life, in absolute ticks (0: did not happen).
struct Record {
  uint64_t deadline = 0;      // N.end: the successor's handoff
  uint64_t requestTick = 0;   // the demand was opened
  uint64_t admitTick = 0;     // the build was admitted (after qualification)
  uint64_t readyTick = 0;     // published as the ready successor
  uint32_t startPhase = 0;
  uint16_t pricedGrains = 0;  // the grains the admission charges this window once qualified (DEC-147's input)
  uint8_t track = 0;
  uint8_t outcome = 0;        // `Outcome`
};
enum class Outcome : uint8_t { Open = 0, HandedOff = 1, Missed = 2, PriceRefused = 3, Cancelled = 4 };

constexpr uint32_t kRecords = 4096;
struct Counters {
  uint32_t requested = 0;
  uint32_t admitted = 0;
  uint32_t ready = 0;
  uint32_t handedOff = 0;
  uint32_t missed = 0;          // the successor was not ready and current at N.end
  uint32_t priceRefused = 0;    // the DEC-147 lead price refused it (bypass off)
  uint32_t cancelled = 0;       // a stale or invalidated candidate was discarded by name
  uint32_t duplicateStarts = 0; // a second demand for an N.end that already had one
  uint32_t recordCount = 0;
  Record records[kRecords] = {};
};
inline Counters gCounters{};

/// The record of `track`'s demand whose deadline is `deadline`, or nullptr.
inline Record* recordFor(uint8_t track, uint64_t deadline) {
  const uint32_t n = gCounters.recordCount < kRecords ? gCounters.recordCount : kRecords;
  for (uint32_t i = n; i-- > 0;)
    if (gCounters.records[i].track == track && gCounters.records[i].deadline == deadline) return &gCounters.records[i];
  return nullptr;
}

}  // namespace RollingSuccessorProbe

#endif
