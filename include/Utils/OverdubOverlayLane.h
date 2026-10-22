//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "RuntimePlaybackView.h"

/// **Slice 3.15e/2b-ii-b step 1 — the overlay lane's waiting state.** A pure observer's vocabulary: what the lane is waiting
/// for, keyed on the identity quartet that must still match when it stops waiting. It has **no write-side effect** — the lane
/// may observe canonical-prefix readiness but must never advance canonical-prefix construction (the committed lane owns that,
/// and the liveness measurement showed it arrives: 13 owner turns to `ready` under overlay-only demand).
namespace OverdubOverlayLane {

enum class Step : uint8_t {
  /// No overlay demand: the lane takes no part in this turn.
  NotRequired,
  /// A demand exists but its canonical prerequisite is not ready. A **pure wait**: continue observing the same identity; never
  /// drive `stepCanonicalPrefix` from here.
  WaitingCanonicalBase,
  /// The canonical prerequisite is ready but the **one** provenance-bearing base instance per slot (the stopped-oracle
  /// companion storage) is not borrowable this turn. Named, so a refused fill is never silence; the lane stays owed.
  BaseFillRefused,
  /// The canonical prerequisite is ready **and** the companion storage is free: the lane may begin the base fill.
  BaseReady,
  /// An existing candidate for this slot no longer matches the demand's identity (committed revision, capture generation or
  /// coverage). Retire it before building; the underlying committed content is untouched.
  RetireStaleCandidate,
};

/// The identity a revisit must find unchanged to keep waiting for the same prerequisite — and that makes either half stale.
struct Observation {
  bool demandOwed = false;
  bool baseCommitted = false;
  bool canonicalReady = false;
  /// The four identity facts the owner named: what the wait is *for*.
  uint32_t committedRevision = 0;
  uint64_t canonicalGeneration = 0;
  uint64_t captureGeneration = 0;
  PlaybackCoverageInterval coverage{};
  /// An existing candidate carries a different committed revision, capture generation or coverage than this demand.
  bool candidateIdentityMismatch = false;
  /// The one companion instance per slot is free to borrow this turn (`frozenCompanionPasses.idle()` and the apply object
  /// available). The lane borrows it only while OVERDUBBING, where the stopped-oracle path cannot be in flight.
  bool companionAvailable = true;
};

inline Step observe(const Observation& observation) {
  if (!observation.demandOwed) return Step::NotRequired;
  // A demand whose identity moved retires first: the candidate's content is no longer this owner's answer.
  if (observation.candidateIdentityMismatch) return Step::RetireStaleCandidate;
  // A slot with no committed base cannot ever satisfy the wait; it is not "waiting", it is not required.
  if (!observation.baseCommitted) return Step::NotRequired;
  if (!observation.canonicalReady) return Step::WaitingCanonicalBase;
  // One provenance-bearing base instance per slot: refuse by name rather than build into the oracle's borrowed storage.
  return observation.companionAvailable ? Step::BaseReady : Step::BaseFillRefused;
}

/// **The collision half of the borrow rule.** A retained overlay candidate *owns* the borrowed companion storage until it is
/// retired, so the stopped-oracle path may not begin another companion build underneath it. This is the decision it must
/// consult; it is a pure function of two facts its owner already has.
enum class OracleRebuildDecision : uint8_t {
  /// No overlay candidate is retained: the oracle may borrow the companion storage as it always has.
  Allowed,
  /// A candidate is retained but its identity no longer matches its demand: **retire it first** (the lane's
  /// `RetireStaleCandidate`), then rebuild. Never rebuild underneath a stale candidate and never leave it unreported.
  RetireCandidateFirst,
  /// A candidate is retained and its identity still matches: the oracle is **refused by name** for this turn.
  RefusedRetainedCandidate,
};

struct CandidateRetention {
  bool overlayCandidateRetained = false;
  /// The retained candidate still matches the committed revision, capture generation and coverage it was built for.
  bool candidateIdentityCurrent = false;
};

inline OracleRebuildDecision oracleRebuildDecision(const CandidateRetention& retention) {
  if (!retention.overlayCandidateRetained) return OracleRebuildDecision::Allowed;
  return retention.candidateIdentityCurrent ? OracleRebuildDecision::RefusedRetainedCandidate
                                            : OracleRebuildDecision::RetireCandidateFirst;
}

/// True while the lane is waiting for a prerequisite it may not advance itself. Named so a caller cannot report a wait as
/// progress: only `BaseReady` and later steps advance.
inline bool waiting(const Step step) { return step == Step::WaitingCanonicalBase; }

}  // namespace OverdubOverlayLane
