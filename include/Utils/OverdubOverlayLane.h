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
  /// A candidate is retained but its committed base no longer answers its demand: **retire it first** (the lane's
  /// `RetireStaleCandidate`), then rebuild. Never rebuild underneath a stale candidate and never leave it unreported.
  RetireCandidateFirst,
  /// A candidate is retained and its base still answers the demand: the oracle is **refused by name** for this turn.
  RefusedRetainedCandidate,
};

struct CandidateRetention {
  bool overlayCandidateRetained = false;
  /// **The retention rule.** The candidate still answers its demand: the demand is owed and the committed revision and
  /// coverage are the ones it was built for. This — not the capture generation — is what decides retirement, because
  /// `captureContentGeneration()` advances on **every accepted input row**: that is the append frontier the continuation
  /// extends, not a new pass identity. A genuinely re-based capture is caught by the builder's own live-source currency
  /// (`nextGrain() == Denied`), which is the second half of the same rule.
  bool candidateBaseCurrent = false;
  /// **The stricter currency a claim compares** (`PlaybackDemandClaims::overlay`): the base **and** the live capture
  /// generation. A candidate one append behind is not identity-current until the lane's next turn extends it; slice-2
  /// arbitration consumes this, and it must never be used as the retirement predicate for that reason.
  bool candidateIdentityCurrent = false;
};

inline OracleRebuildDecision oracleRebuildDecision(const CandidateRetention& retention) {
  if (!retention.overlayCandidateRetained) return OracleRebuildDecision::Allowed;
  // The oracle is about to reinterpret the borrowed storage. A candidate that still answers its demand — even one append
  // behind — owns that storage; only a candidate whose **base** moved (or whose demand ended) may be retired first.
  return retention.candidateBaseCurrent ? OracleRebuildDecision::RefusedRetainedCandidate
                                        : OracleRebuildDecision::RetireCandidateFirst;
}

/// **A retained overlay candidate is identifiable from its own metadata** — no stored marker and no fourth state. Only
/// the continuation writes this shape: a per-row provenance table (`passOriginsReady`) while the committed canonical
/// rank map no longer describes the whole window (`canonicalRanksReady == false`), on a Capture-origin identity that
/// carries the live capture generation. A committed base fill sets both provenance flags; a raw Record/live build sets
/// neither. So the owner turn, the retirement predicate and the oracle all agree on what "the candidate" is.
inline bool isOverlayCandidate(const RuntimePlaybackView& view) {
  return view.passOriginsReady && !view.canonicalRanksReady &&
         view.source.origin == PlaybackWindowOrigin::Capture &&
         view.source.captureContentGeneration != 0;
}

/// **The retention identity rule, in one place.** A retained candidate answers the demand only while its committed base
/// — the identity its rows were gathered from, plus the coverage — still matches the live demand. The live capture
/// generation is deliberately separate: it advances on every accepted input row and is the append frontier the
/// continuation consumes, so it is reported as the stricter claim currency rather than as a retirement reason. A demand
/// that is no longer owed makes the candidate stale — there is nothing left for it to answer — and the borrower's
/// storage must be released before any other owner reinterprets it.
inline CandidateRetention candidateRetention(const RuntimePlaybackView* candidate, bool demandOwed,
                                            const PlaybackViewSourceIdentity& baseIdentity,
                                            uint64_t captureGeneration,
                                            const PlaybackCoverageInterval& coverage) {
  CandidateRetention out;
  if (candidate == nullptr || !isOverlayCandidate(*candidate)) return out;
  out.overlayCandidateRetained = true;
  // The base half's authority is the identity its rows were gathered from: the continuation replaces `source`'s
  // snapshot pointer with the overlay's Capture identity, so the **provenance** source (`passOriginSource`, written by
  // the base fill and never rewritten by the append) is what must match. Comparing the live `source` here would let a
  // republication at the same revision look current for a base it no longer holds.
  out.candidateBaseCurrent =
      demandOwed && candidate->passOriginsReady && candidate->passOriginSource == baseIdentity &&
      candidate->coverage.startPhase == coverage.startPhase &&
      candidate->coverage.lengthTicks == coverage.lengthTicks &&
      candidate->coverage.loopLengthTicks == coverage.loopLengthTicks;
  out.candidateIdentityCurrent =
      out.candidateBaseCurrent && candidate->source.captureContentGeneration == captureGeneration;
  return out;
}

/// True when a retained candidate no longer answers its demand and must be retired **before** anything (another build
/// or the stopped oracle) may reuse the borrowed companion storage underneath it.
inline bool mustRetire(const CandidateRetention& retention) {
  return retention.overlayCandidateRetained && !retention.candidateBaseCurrent;
}

/// **The owner build's inputs.** The base half is the provenance-bearing committed interval the ordinary committed lane
/// would prepare; the overlay half is the live capture generation the demand names. `playheadPhase` only positions the
/// build's cursor anchor and the committed route's coverage policy, both of which the caller resolved.
struct BuildRequest {
  PlaybackViewSourceIdentity baseSource{};
  PlaybackCoverageInterval coverage{};
  uint64_t captureGeneration = 0;
  uint32_t playheadPhase = 0;
};

/// One owner turn's outcome, named so a caller never reports a wait, a refusal or a deferral as progress.
enum class Turn : uint8_t {
  /// Nothing is owed for this slot this turn, and no candidate was retired.
  Idle,
  /// The canonical prerequisite is not ready: a pure wait, never a build.
  WaitingCanonicalBase,
  /// The canonical prerequisite is ready but the one borrowable companion instance is not free. Named, never a silent
  /// fill; no partial candidate is left behind.
  BaseFillRefused,
  /// Another owner holds the slot's builder, so this lane defers by name (role arbitration is 2b-iii).
  Deferred,
  /// The borrowed companion build was begun this turn (setup is its own turn).
  BaseStarted,
  /// One companion/fill/order grain was consumed; the base half is in flight.
  BaseBuilding,
  /// The base half completed and the append-stable live overlay was attached this turn.
  OverlayStarted,
  /// One overlay fill/order grain was consumed; the continuation is in flight.
  OverlayBuilding,
  /// The combined candidate is retained and complete, and its committed base still answers the demand.
  CandidateReady,
  /// A candidate whose identity no longer matched was retired this turn.
  RetiredStale,
  /// The build could not proceed honestly (a named refusal or a denied grain); nothing partial is servable.
  Denied,
};

/// True only for the two outcomes that represent completed work (a retained candidate or the retirement that precedes
/// one), so a caller cannot count a wait, a refusal or a deferral as progress.
inline bool progressed(const Turn turn) {
  return turn == Turn::BaseStarted || turn == Turn::BaseBuilding || turn == Turn::OverlayStarted ||
         turn == Turn::OverlayBuilding || turn == Turn::CandidateReady || turn == Turn::RetiredStale;
}

/// True while the lane is waiting for a prerequisite it may not advance itself. Named so a caller cannot report a wait as
/// progress: only `BaseReady` and later steps advance.
inline bool waiting(const Step step) { return step == Step::WaitingCanonicalBase; }

}  // namespace OverdubOverlayLane
