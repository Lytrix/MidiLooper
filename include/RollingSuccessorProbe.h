//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

// C5.3b (DEC-246 amendments 24-25): the successor-lead measurement probe. On the host it is compiled in and inert
// until a fixture sets `gKnobs.enabled`. On the device it exists only in a **measurement image**
// (`-D PLAYBACK_ROLLING_SUCCESSOR_PROBE=1`, env `teensy41-capture-serial-revstore-succprobe`), which enables it at boot
// (`installAtBoot`); every other image compiles none of it.
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
  /// DEC-246 amendment 26 (device measurement): deterministic arbitration. After every holder grain served to
  /// `denyTrack` (0-based; 0xFF: off), its next `denyPerGrain` grants are withheld (the lane turn serves no one).
  uint8_t denyTrack = 0xFF;
  uint8_t denyPerGrain = 0;
  uint8_t denyLeft = 0;
};

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

/// Per-track running aggregates: what the device reports (`DIAG,succ`), since it keeps only a short record ring.
struct Aggregate {
  uint32_t requested = 0;
  uint32_t handedOff = 0;
  uint32_t missed = 0;
  uint32_t cancelled = 0;
  uint32_t priceRefused = 0;
  uint32_t maxRequestToReady = 0;        // ticks
  uint32_t maxRequestToAdmit = 0;        // ticks
  uint32_t minReadyMargin = UINT32_MAX;  // ticks from ready to N.end
  uint32_t maxPricedGrains = 0;
  uint32_t minRequestLead = UINT32_MAX;  // ticks from the demand's opening to N.end (the lead actually given)
  uint32_t deniedGrants = 0;             // grants withheld by the injector (amendment 26)
  uint32_t reported = 0;  // `requested + closed outcomes` at the last row
};

#if defined(__IMXRT1062__)
constexpr uint32_t kRecords = 64;  // a ring: a lookup only ever needs the track's open demand
#else
constexpr uint32_t kRecords = 4096;
#endif
constexpr uint8_t kTracks = 8;
constexpr uint32_t kSpanTicks = 2 * 768;  // one successor span (two bars at 192 PPQN), for the trace only
struct Counters {
  uint32_t requested = 0;
  uint32_t admitted = 0;
  uint32_t ready = 0;
  uint32_t handedOff = 0;
  uint32_t missed = 0;          // the successor was not ready and current at N.end
  uint32_t priceRefused = 0;    // the DEC-147 lead price refused it (bypass off)
  uint32_t cancelled = 0;       // a stale or invalidated candidate was discarded by name
  uint32_t duplicateStarts = 0; // a second demand for an N.end that already had one
  uint32_t recordCount = 0;     // records ever opened (the ring index on the device)
  Aggregate perTrack[kTracks] = {};
  Record records[kRecords] = {};
  /// Owner ruling (amendment 26 follow-up): where a successor's lead goes before its demand opens. One per track, for
  /// the window being served: when the lead was first reached, which gates held the demand and for how many turns,
  /// when it opened, and the first grant and first withheld grant after that. Reported once when the seam resolves.
  struct SeamTrace {
    uint64_t deadline = 0;        // N.end of the seam traced
    uint64_t eligibleTick = 0;    // the first turn the requested lead was reached
    uint64_t requestTick = 0;     // the demand opened
    uint64_t firstGrantTick = 0;  // the first lane grant to the track after the demand opened
    uint64_t firstDenyTick = 0;   // the first grant the injector withheld after it opened
    uint32_t blockedTurns = 0;    // eligible turns the demand was held by a gate
    uint8_t blockMask = 0;        // gates seen: 1 pending, 2 frozen, 4 coverage role, 8 outstanding grains, 16 ready
    uint8_t lastRole = 0;         // `PlaybackCoverageRoleRefusal` at the last role block
    uint8_t outcome = 0;          // `Outcome` once resolved
    uint8_t resolved = 0;         // 1: resolved and not yet reported
    uint64_t servingSeenTick = 0; // the first producer turn that saw N Serving under this key
    uint8_t forcedGrants = 0;     // grants the service bound kept the injector from withholding (owner ruling)
    // Owner ruling (cadence contract): while the successor is owed, the largest transport-tick gap between eligible
    // lane opportunities (main-loop starvation; contract <= 8) and between grants served to it (arbitration
    // starvation; contract <= 32).
    uint64_t lastOpportunityTick = 0;
    uint64_t lastServedTick = 0;
    uint32_t maxOpportunityGap = 0;
    uint32_t maxGrantGap = 0;
    // Owner ruling (grant bound): grants served while owed, of them before admission, and the counted window (rows E
    // and read units) the admission priced -- to compare grants with ceil((2E+1)/64) + ceil(E/48) + fixed.
    uint16_t grantsServed = 0;
    uint16_t grantsAtAdmit = 0;
    uint32_t rows = 0;
    uint32_t readUnits = 0;
    // Owner ruling (candidate service deadline): the next grant's due tick, the bounded grant estimate behind it, the
    // worst lateness of a grant against its due, and the grants served because they were due.
    uint64_t grantDueTick = 0;
    uint32_t maxGrantLateness = 0;
    uint16_t estimatedGrants = 0;
    uint16_t dueForcedGrants = 0;
  } seam[kTracks] = {};
  /// Resolved seams waiting for the main-loop report (a handed-off seam's trace is replaced by the next one at once).
  static constexpr uint8_t kSeamLog = 8;
  SeamTrace seamLog[kSeamLog] = {};
  uint8_t seamLogTrack[kSeamLog] = {};
  uint8_t seamLogHead = 0, seamLogCount = 0;
};

#if defined(__IMXRT1062__)
// RAM2 (`DMAMEM`, NOLOAD): defined once in TrackPlaybackWindowBuild.cpp and written by `installAtBoot`.
extern Knobs gKnobs;
extern Counters gCounters;
#else
inline Knobs gKnobs{};
inline Counters gCounters{};
#endif

Record* recordFor(uint8_t track, uint64_t deadline);
Record* openRecord(uint8_t track, uint64_t deadline, uint64_t requestTick, uint32_t startPhase);
void noteAdmitted(Record& r, uint64_t tick);
void noteReady(Record& r, uint64_t tick);
void noteOutcome(Record& r, Outcome outcome);
/// Amendment 26 (device): true when this grant to `track` is withheld by the injector; `noteGrant` after a served one.
bool denyThisGrant(uint8_t track, uint64_t tick);
void noteGrant(uint8_t track, uint64_t tick);
/// The service bound overrode a withheld grant for `track` (counted on the seam being traced).
void noteForcedGrant(uint8_t track);
}  // namespace RollingSuccessorProbe
namespace MidiServiceDrain { struct PlaybackHolder; }
namespace RollingSuccessorProbe {
/// One lane turn (owner ruling, cadence contract): every holder whose deadline successor is owed had an eligible
/// opportunity at `tick`; `servedTrack` is the track that was actually served (`0xFF`: none). Measurement image only;
/// defined beside the lane (flash).
void noteSuccessorOpportunities(const MidiServiceDrain::PlaybackHolder* offers, uint8_t count, uint8_t servedTrack,
                                uint64_t tick);
/// Reconfigure the measurement (lead, denied track, denials per grain) and reset every counter.
void configure(uint32_t requestLeadTicks, uint8_t denyTrack, uint8_t denyPerGrain);

#if defined(__IMXRT1062__)
/// The measurement image enables the probe at boot: one successor per served window, at N's start (lead = one span),
/// with only the DEC-147 lead price waived for it. NOLOAD storage, so everything is written here.
void installAtBoot();
#endif

}  // namespace RollingSuccessorProbe

#if !defined(__IMXRT1062__)
// The host defines the helpers inline here; the device defines them once, in flash, in TrackPlaybackWindowBuild.cpp.
#define ROLLING_SUCCESSOR_PROBE_MEM inline
#include "RollingSuccessorProbeImpl.h"
#endif

#endif
