//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "RuntimePlaybackOrderBuilder.h"
#include "RuntimePlaybackViewFill.h"

enum class RuntimePlaybackViewBuildGrain : uint8_t { Fill, Order, Complete, Denied };

/// Persistent build state for one unpublished view. Fill and order remain
/// distinct owner-selected grains: a call can never copy source entries and
/// merge order entries in the same active-transport turn.
class RuntimePlaybackViewBuild {
 public:
  bool begin(RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillScope scope = RuntimePlaybackViewFillScope::PlaybackCoverage);
  bool begin(RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillScope scope = RuntimePlaybackViewFillScope::AllSource);
  bool begin(RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillScope scope = RuntimePlaybackViewFillScope::AllSource);

  RuntimePlaybackViewBuildGrain nextGrain() const;
  RuntimePlaybackViewBuildGrain stepFill(size_t entryQuantum);
  RuntimePlaybackViewBuildGrain stepOrder(size_t entryQuantum);
  bool active() const;
  // Cancel an in-flight build. When a retained view must survive a runtime
  // reset, pass its address so the borrowed destination metadata is kept.
  void discard(const RuntimePlaybackView* preserveDestination = nullptr);

 private:
  enum class Phase : uint8_t { Idle, Fill, Order, Complete, Denied };

  RuntimePlaybackViewFill fill_;
  RuntimePlaybackOrderBuilder order_;
  RuntimePlaybackView* destination_ = nullptr;
  Phase phase_ = Phase::Idle;
};

/// The proof inputs for one owner-local long-loop refill. The contract does
/// not infer a wall-clock budget: it converts declared per-turn phase advance,
/// denied-turn allowance, grain count, and lead into a deterministic admission
/// decision. A false result keeps windowed work out of active transport.
struct CoverageServiceContract {
  /// **The unit is "the phase the playhead advances between two of THIS owner's own opportunities"** -- i.e. between two
  /// `PlaybackViewMaintenance` dispatches, which already contains the other six deferred owners' share of the rotation.
  /// It does **not** contain the contention *inside* the lane: one *track* of every `H` eligible tracks per lane turn
  /// (`src/main.cpp`'s offer round-robin), nor the sibling slots of the chosen track (the spec's within-holder
  /// earliest-deadline-first lets a sibling's earlier interval take that turn). Those multiply onto the **turn count**,
  /// not onto this field, so `canCompleteBeforeHandoff`'s `grains x (denied + 1)` is **not yet a complete bound**: it
  /// needs the `H x K` contention factors before any envelope can be called supported, and the unit above must be
  /// *measured* rather than assumed. Measured 2026-10-02: conflating the two readings -- injecting the owner rotation
  /// here *and* keeping it in the turn count -- double-counts it, which is the error an earlier record made (see
  /// `evidence/2026-10-02-s310h-lifetime-fairness-derivation/`).
  ///
  /// **Measured 2026-10-03, and the measurement decides how this field may be read.** The device measures the
  /// steady state at **2.9-5.5 ticks per opportunity** (a rotation of ~7.3-7.5 ms) and the rotation is
  /// **tempo-invariant** while this tick-denominated allowance is not: doubling the tempo doubles the
  /// ticks-per-opportunity, so the declared 768 is 2.0 s at 120 BPM and 1.0 s at 240 BPM. A separate and much
  /// larger quantity appears in the same runs -- **starvation episodes** of 2 138 / 4 355 / 8 520 ticks
  /// (5.6-11.3 s), from cumulative *lost* opportunities rather than from one blocking call. This field is
  /// therefore an upper bound on the **scheduling** advance and is **not** an upper bound on how far the playhead
  /// can move between two of this owner's opportunities. A window admitted on this figure can be starved past its
  /// handoff; that miss is handled and reported by the lag rule (`one_pass_late` / `lag_breach`), not by this
  /// constant, and the constant must not be enlarged towards the starvation figure to make the gap disappear.
  uint32_t maximumPhaseAdvancePerTurn = 0;
  /// The **within-lane** contention allowance -- `H` eligible tracks x `K` sibling slots of the chosen track -- not
  /// the owner rotation: the rotation is already inside `maximumPhaseAdvancePerTurn` by that field's unit, so
  /// putting it here as well would double-count it. It is an **allowance**, not a proved bound; declaring `H x K`
  /// is task 3.10's remaining work.
  uint16_t maximumConsecutiveDeniedTurns = 0;
  uint16_t requiredFillGrains = 0;
  uint16_t requiredOrderGrains = 0;
  uint16_t requiredPromotionGrains = 0;
  uint32_t leadTicks = 0;

  bool isDeclared() const;
  uint16_t requiredGrains() const;
  bool canCompleteBeforeHandoff(uint32_t currentPhase, uint32_t handoffPhase,
                                uint32_t loopLengthTicks,
                                uint16_t additionalGrains = 0) const;
};

/// DEC-108 §13b / task 3.10: **the tick distance from the playhead to a handoff** -- how long the caller has
/// before its interval is needed, measured along the loop (`handoffPhase < currentPhase` means the next entry is
/// a full pass away). It is the contract's own `available` sub-term, named once so the admission decision and any
/// *scheduling* reader of the same quantity cannot disagree; a raw phase is not comparable between holders whose
/// loops differ in length, and this is.
inline uint32_t handoffLeadTicks(uint32_t currentPhase, uint32_t handoffPhase, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || currentPhase >= loopLengthTicks || handoffPhase >= loopLengthTicks) {
    return 0;
  }
  return handoffPhase >= currentPhase ? handoffPhase - currentPhase
                                      : loopLengthTicks - currentPhase + handoffPhase;
}
