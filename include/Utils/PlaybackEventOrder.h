//  Copyright (c)  2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Canonical playback-event order. The bridge compiles an order (the base/insertion merge) and then
// *consumes* an order (the same merge replayed by the cursor). Design invariant 5 requires those two
// to agree, so the comparison has exactly one owner here: a compiled order that disagrees with the
// order it is consumed in silently drops or duplicates accepted events.

#pragma once

#include <cstdint>

#include "MidiEvent.h"

namespace PlaybackEventOrder {

/// Emission class for the canonical Off-before-On rule: an Off sorts before an On at the same tick,
/// and any other event kind sorts after both.
inline uint8_t eventKind(const MidiEvent& event) {
  return event.isNoteOff() ? 0 : (event.isNoteOn() ? 1 : 2);
}

/// The five-key order: projection phase, then event kind, then tick, then canonical rank, then the
/// row's provenance value. `provenance` is whatever the caller's side uses as its final tie-break
/// (an insertion's recorded provenance, or the base order index for a base row).
struct MergeKey {
  uint32_t phase;
  uint32_t tick;
  uint32_t rank;
  uint32_t provenance;
  uint8_t kind;
};

inline MergeKey mergeKey(uint32_t phase, uint32_t tick, uint32_t rank, uint32_t provenance,
                         const MidiEvent& event) {
  return MergeKey{phase, tick, rank, provenance, eventKind(event)};
}

inline bool mergeKeyBefore(const MergeKey& a, const MergeKey& b) {
  if (a.phase != b.phase) return a.phase < b.phase;
  if (a.kind != b.kind) return a.kind < b.kind;
  if (a.tick != b.tick) return a.tick < b.tick;
  if (a.rank != b.rank) return a.rank < b.rank;
  return a.provenance < b.provenance;
}

}  // namespace PlaybackEventOrder
