//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <limits>

#include "MidiEvent.h"

class Loop;

/// Track-owned committed MIDI scratch freshness helpers (OpenSpec 4.6 Stage 5).
/// `Track` owns the scratch buffer and stamps; `Loop` owns snapshot copy.
namespace CommittedMidiScratch {

enum class RefreshType : uint8_t {
  Reused = 0,
  FromSnapshot = 1,
  // Value 2 is retired (the pass-replay fallback OpenSpec 6.3.3 removed). `HoldEmpty`
  // keeps value 3 so existing capture comparisons and logs stay meaningful.
  HoldEmpty = 3,
};

inline bool isFresh(uint32_t scratchRevision, uint32_t playbackRevision,
                    const void* scratchIdentity, const void* publishedIdentity) {
  return scratchRevision == playbackRevision && scratchIdentity == publishedIdentity;
}

inline void invalidate(uint32_t& scratchRevision, const void*& scratchIdentity) {
  scratchRevision = std::numeric_limits<uint32_t>::max();
  scratchIdentity = nullptr;
}

inline void invalidate(MidiEventVec& scratch, uint32_t& scratchRevision,
                       const void*& scratchIdentity) {
  scratch.clear();
  invalidate(scratchRevision, scratchIdentity);
}

/// Refresh scratch from the currently published snapshot when usable; otherwise
/// hold empty (OpenSpec 6.3.3 — no pass-replay fallback). Fill uses one
/// publication observation for copy + stamp. Match uses the current published
/// identity for this call.
RefreshType refresh(MidiEventVec& scratch, uint32_t& scratchRevision,
                    const void*& scratchIdentity, Loop& loop);

}  // namespace CommittedMidiScratch
