// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>

/// DEC-191: the retained-Start wake decision, as a **pure** predicate so both boundaries can be pinned natively.
/// A retained Start is only re-driven when the admission identity that refused it actually moved. The identity is
/// (publish-claim state, live reserved owner, session-exit receipt-ring occupancy) -- deliberately **not**
/// `playbackRevision`, which moves for unrelated musical reasons and would re-awaken the expensive start body
/// while the real blocker stands.
///
/// Ownership guarantees correctness (an ownerless `Reserved` claim resolves itself); this gate guarantees RT
/// safety (a legitimately owned refusal cannot cost the preparation body every loop).
enum class OverdubStartWake : uint8_t {
  Park = 0,   ///< identity unchanged: a cheap preflight at most, never the preparation body
  Retry = 1,  ///< a member of the identity moved: the next eligible turn may attempt admission again
};

inline OverdubStartWake overdubStartRetryDecision(bool refusedOnSessionExit, uint8_t claimThen, uint8_t claimNow,
                                                  bool ownerThen, bool ownerNow, uint8_t ringThen,
                                                  uint8_t ringNow) {
  if (!refusedOnSessionExit) return OverdubStartWake::Retry;
  if (claimNow != claimThen) return OverdubStartWake::Retry;
  if (ownerNow != ownerThen) return OverdubStartWake::Retry;
  if (ringNow != ringThen) return OverdubStartWake::Retry;
  return OverdubStartWake::Park;
}
