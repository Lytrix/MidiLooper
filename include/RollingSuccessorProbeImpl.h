//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

// The successor-lead probe's helpers (`RollingSuccessorProbe.h`). Included once per image: inline from that header on
// the host; on the device only by TrackPlaybackWindowBuild.cpp, with `ROLLING_SUCCESSOR_PROBE_MEM` placing them in flash.

#include "RollingSuccessorProbe.h"

namespace RollingSuccessorProbe {

/// The record of `track`'s demand whose deadline is `deadline`, or nullptr.
ROLLING_SUCCESSOR_PROBE_MEM Record* recordFor(uint8_t track, uint64_t deadline) {
  const uint32_t n = gCounters.recordCount < kRecords ? gCounters.recordCount : kRecords;
  for (uint32_t back = 0; back < n; ++back) {
    Record& r = gCounters.records[(gCounters.recordCount - 1 - back) % kRecords];
    if (r.track == track && r.deadline == deadline) return &r;
  }
  return nullptr;
}

/// Open a record. The device's ring overwrites its oldest; the host's table stops recording when full.
ROLLING_SUCCESSOR_PROBE_MEM Record* openRecord(uint8_t track, uint64_t deadline, uint64_t requestTick,
                                               uint32_t startPhase) {
#if !defined(__IMXRT1062__)
  if (gCounters.recordCount >= kRecords) return nullptr;
#endif
  Record& r = gCounters.records[gCounters.recordCount % kRecords];
  ++gCounters.recordCount;
  r = Record{};
  r.deadline = deadline;
  r.requestTick = requestTick;
  r.startPhase = startPhase;
  r.track = track;
  if (track < kTracks) {
    auto& a = gCounters.perTrack[track];
    ++a.requested;
    const uint64_t lead = deadline > requestTick ? deadline - requestTick : 0;
    if (lead < a.minRequestLead) a.minRequestLead = static_cast<uint32_t>(lead);
  }
  return &r;
}

ROLLING_SUCCESSOR_PROBE_MEM void noteAdmitted(Record& r, uint64_t tick) {
  r.admitTick = tick;
  if (r.track >= kTracks || tick < r.requestTick) return;
  auto& a = gCounters.perTrack[r.track];
  const uint64_t d = tick - r.requestTick;
  if (d > a.maxRequestToAdmit) a.maxRequestToAdmit = d > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(d);
}

ROLLING_SUCCESSOR_PROBE_MEM void noteReady(Record& r, uint64_t tick) {
  r.readyTick = tick;
  if (r.track >= kTracks || tick < r.requestTick) return;
  auto& a = gCounters.perTrack[r.track];
  const uint64_t d = tick - r.requestTick;
  if (d > a.maxRequestToReady) a.maxRequestToReady = d > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(d);
  if (tick <= r.deadline && r.deadline - tick < a.minReadyMargin)
    a.minReadyMargin = static_cast<uint32_t>(r.deadline - tick);
  if (r.pricedGrains > a.maxPricedGrains) a.maxPricedGrains = r.pricedGrains;
}

/// Close an open record with `outcome`; counted once, globally and per track.
ROLLING_SUCCESSOR_PROBE_MEM void noteOutcome(Record& r, Outcome outcome) {
  if (r.outcome != static_cast<uint8_t>(Outcome::Open)) return;
  r.outcome = static_cast<uint8_t>(outcome);
  if (r.track < kTracks && gCounters.seam[r.track].deadline == r.deadline) {
    auto& seam = gCounters.seam[r.track];
    seam.outcome = static_cast<uint8_t>(outcome);
    if (gCounters.seamLogCount < Counters::kSeamLog) {  // full: the oldest unreported seams are kept, later ones lost
      const uint8_t at = uint8_t((gCounters.seamLogHead + gCounters.seamLogCount) % Counters::kSeamLog);
      gCounters.seamLog[at] = seam;
      gCounters.seamLogTrack[at] = r.track;
      ++gCounters.seamLogCount;
    }
  }
  Aggregate* a = r.track < kTracks ? &gCounters.perTrack[r.track] : nullptr;
  switch (outcome) {
    case Outcome::HandedOff: ++gCounters.handedOff; if (a) ++a->handedOff; break;
    case Outcome::Missed: ++gCounters.missed; if (a) ++a->missed; break;
    case Outcome::PriceRefused: ++gCounters.priceRefused; if (a) ++a->priceRefused; break;
    default: ++gCounters.cancelled; if (a) ++a->cancelled; break;
  }
}

ROLLING_SUCCESSOR_PROBE_MEM bool denyThisGrant(uint8_t track, uint64_t tick) {
  if (!gKnobs.enabled || track != gKnobs.denyTrack || gKnobs.denyLeft == 0) return false;
  --gKnobs.denyLeft;
  if (track < kTracks) {
    ++gCounters.perTrack[track].deniedGrants;
    auto& seam = gCounters.seam[track];
    if (seam.requestTick != 0 && seam.firstDenyTick == 0) seam.firstDenyTick = tick;
  }
  return true;
}

ROLLING_SUCCESSOR_PROBE_MEM void noteGrant(uint8_t track, uint64_t tick) {
  if (track < kTracks) {
    auto& seam = gCounters.seam[track];
    if (seam.requestTick != 0 && seam.firstGrantTick == 0) seam.firstGrantTick = tick;
  }
  if (gKnobs.enabled && track == gKnobs.denyTrack) gKnobs.denyLeft = gKnobs.denyPerGrain;
}

ROLLING_SUCCESSOR_PROBE_MEM void noteForcedGrant(uint8_t track) {
  if (track < kTracks && gCounters.seam[track].forcedGrants != 255) ++gCounters.seam[track].forcedGrants;
}

ROLLING_SUCCESSOR_PROBE_MEM void configure(uint32_t requestLeadTicks, uint8_t denyTrack, uint8_t denyPerGrain) {
  gCounters = Counters{};
  gKnobs.enabled = true;
  gKnobs.bypassLeadPrice = true;
  gKnobs.requestLeadTicks = requestLeadTicks;
  gKnobs.denyTrack = denyTrack;
  gKnobs.denyPerGrain = denyPerGrain;
  gKnobs.denyLeft = 0;
}

}  // namespace RollingSuccessorProbe
