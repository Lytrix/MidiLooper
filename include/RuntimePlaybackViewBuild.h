//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/PlaybackDeadlineClock.h"
#include "RuntimePlaybackOrderBuilder.h"
#include "RuntimePlaybackViewFill.h"

enum class RuntimePlaybackViewBuildGrain : uint8_t { Fill, Order, Complete, Denied };

/// Persistent build state for one unpublished view. Fill and order remain
/// distinct owner-selected grains: a call can never copy source entries and
/// merge order entries in the same active-transport turn.
class RuntimePlaybackViewBuild {
 public:
  /// Selection-through-publication exclusion; only acquisition/release mask IRQs.
  /// Cancellation retains the turn until its owning scope unwinds.
  class PreparationTurn {
   public:
    explicit PreparationTurn(RuntimePlaybackViewBuild& builder);
    ~PreparationTurn();
    PreparationTurn(const PreparationTurn&) = delete;
    PreparationTurn& operator=(const PreparationTurn&) = delete;
    explicit operator bool() const;
    bool owns(const RuntimePlaybackViewBuild& builder) const;
   private:
    RuntimePlaybackViewBuild& builder_;
    bool acquired_ = false;
  };
  bool preparationInProgress() const { return preparing_; }
  void cancelPreparation();
  bool preparationCancelled() const { return preparationCancelled_; }
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

  bool begin(RuntimePlaybackView& destination, LoopSnapshot::EventWindowCursor& source,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary);
  // Qualification fills only the private candidate, then pauses before ordering.
  bool beginCoverageQualification(RuntimePlaybackView& destination,
      LoopSnapshot::EventWindowCursor& source, const PlaybackViewSourceIdentity& identity,
      const PlaybackCoverageInterval& coverage);
  RuntimePlaybackViewFillResult stepCoverageQualification(size_t quantum);
  size_t qualifiedEventCount() const { return fill_.copiedEventCount(); }
  bool admitCoverageQualification();
  bool coverageOwned() const { return coverageOwned_; }
  void markCoverageOwned() { coverageOwned_ = true; }
  /// A stale live-overlay continuation (a pass commit re-based the capture) clears the candidate's `complete`/`sourceFilled`
  /// here, so a refused window can never be mistaken for an adoptable one by a consumer that only reads the view's flags.
  RuntimePlaybackViewBuildGrain nextGrain() const;
  RuntimePlaybackViewBuildGrain stepFill(size_t entryQuantum);
  RuntimePlaybackViewBuildGrain stepOrder(size_t entryQuantum);
  bool active() const;
  /// Executing state is retained until this public call resolves cancellation.
  bool callInProgress() const { return executing_; }
  bool ownsLiveCaptureBorrow() const { return fill_.ownsLiveCaptureBorrow(); }
  /// Caller retains the physical source; this is currency, never a lifetime test.
  bool liveCaptureCurrent() const { return fill_.liveCaptureCurrent(); }
  bool extendLiveCapture(const PlaybackViewSourceIdentity& identity);
  /// **Slice 3.15e — the live-overlay continuation of one window.** After the committed base is complete and ordered,
  /// continue this **same** build (one coverage interval, one ordered index, one source identity) from one append-stable
  /// live capture source. Overlay rows are appended with the `kLiveOverlayPassOrigin` provenance sentinel; the ordered index
  /// is extended off-clock by the existing Fill → Order grains. The window is not complete again until ordering covers the
  /// appended rows, and a pass commit that re-bases the capture denies the continuation by name.
  bool beginLiveOverlayContinuation(const RuntimePlaybackViewSource& live,
                                    const PlaybackViewSourceIdentity& identity);
  bool overlayContinuationActive() const { return fill_.continuingOverlay(); }
  /// Named refusal of the continuation half (capacity, stale generation, identity mismatch) — never a partial window.
  RuntimePlaybackViewFillRefusal continuationRefusal() const {
    return fill_.continuingOverlay() && !fill_.liveCaptureCurrent()
        ? RuntimePlaybackViewFillRefusal::ContinuationStale : fill_.refusal();
  }
  /// Content generation of the live overlay this window carries (0 when it has no overlay half).
  uint64_t overlayCaptureGeneration() const { return fill_.continuationCaptureGeneration(); }
  size_t liveCaptureSourceExtent() const { return fill_.frozenSourceExtent(); }
  size_t liveCaptureSourceFrontier() const { return fill_.sourceFrontier(); }
  size_t privateOrderedEventCount() const { return order_.complete() ? order_.orderedEventCount() : 0; }
  /// Exact physical generation completed by this builder; caller retains storage.
  bool completedDestinationIs(const RuntimePlaybackView& destination) const;
  /// True while this builder is *writing* the given destination (any fill/order/snapshot phase). Owner-selection
  /// queries use it to retire a candidate **with** its in-flight builder instead of guessing from the view's flags.
  bool activeDestinationIs(const RuntimePlaybackView& destination) const {
    return active() && destination_ == &destination &&
           destination.storageGeneration == destinationGeneration_;
  }
  /// True while this builder is **bound** to the given destination, regardless of grain currency. Retirement uses this
  /// rather than the two completion queries: after a re-based capture `nextGrain()` reports `Denied` (and clears the
  /// view's flags), so a currency-based test would leave the stale builder attached to the very candidate being retired.
  bool boundToDestination(const RuntimePlaybackView& destination) const {
    return destination_ == &destination;
  }
  // Cancel an in-flight build. When a retained view must survive a runtime
  // reset, pass its address so the borrowed destination metadata is kept.
  void discard(const RuntimePlaybackView* preserveDestination = nullptr);

 private:
  bool enterCall();
  bool finishCall();
  void discardNow(const RuntimePlaybackView* preserveDestination);
  /// Publish the readable ordered prefix after an order completes. One 16-bit store; monotonic within a window; never
  /// beyond the retained rows. This is the live-overlay read contract (slice 3), not a second meaning for `complete`.
  void publishReadableFrontier();
  template <typename Work, typename Result>
  Result execute(Work&& work, Result denied) {
    if (!enterCall()) return denied;
    const Result result = work();
    return finishCall() ? result : denied;
  }
  enum class Phase : uint8_t { Idle, Fill, Order, Complete, Denied, SnapshotFill, SnapshotCanonicalOrder, SnapshotRanks, SnapshotPlaybackOrder, Qualification };

  RuntimePlaybackViewFill fill_;
  RuntimePlaybackOrderBuilder order_;
  uint64_t destinationGeneration_ = 0;
  RuntimePlaybackView* destination_ = nullptr;
  uint16_t snapshotRankCursor_ = 0;
  Phase phase_ = Phase::Idle;
  bool coverageOwned_ = false;
  volatile bool executing_ = false;
  volatile bool discardRequested_ = false;
  volatile bool preparing_ = false;
  volatile bool preparationCancelled_ = false;
  const RuntimePlaybackView* pendingPreserveDestination_ = nullptr;
};

/// The proof inputs for one owner-local long-loop refill. The contract does
/// not infer a wall-clock budget: it converts declared per-turn phase advance,
/// denied-turn allowance, grain count, and lead into a deterministic admission
/// decision. A false result keeps windowed work out of active transport.
struct CoverageServiceContract {
  /// Phase advance between consecutive PlaybackViewMaintenance lane dispatches.
  /// Includes every intervening owner turn (including extra Snapshot turns), not
  /// track contention. A permitted upper bound plus an overrun policy is required;
  /// an observed maximum or average is not a guarantee. Legacy selected-worker-only
  /// samples cannot distinguish no-work intervals from pending-work service gaps.
  uint32_t maximumPhaseAdvancePerTurn = 0;
  /// Across-track denial allowance: Hmax-1 for round-robin tracks. Charge same-track
  /// sibling WORK through additionalGrains, never a fixed slot multiplier. The
  /// current value is provisional and live window activation remains gated.
  uint16_t maximumConsecutiveDeniedTurns = 0;
  uint16_t requiredFillGrains = 0;
  uint16_t requiredOrderGrains = 0;
  uint16_t requiredPromotionGrains = 0;
  uint32_t leadTicks = 0;

  /// Same admission arithmetic with an already-qualified, non-modular remaining lead.
  bool canCompleteWithinLead(uint64_t availableTicks, uint16_t additionalGrains = 0) const;
  bool isDeclared() const;
  uint16_t requiredGrains() const;
  /// DEC-108 §13b / task 3.10 (the review's step 1, *"bound the work of each request"*): **the grain cost of a
  /// request, derived from the entries the fill must inspect** -- `ceil(events / kPlaybackViewFillQuantum)` fills
  /// plus `RuntimePlaybackOrderBuilder::requiredGrains(events, kPlaybackViewOrderQuantum)` order grains plus one
  /// promotion grain. It is the **single source of truth** for cost: the admission, the retained work record and
  /// the fixtures all read this one function, so they cannot disagree about what a window costs. It *sets* the
  /// three grain fields and leaves every other declared field (the advance, the denial allowance, the lead)
  /// untouched -- deriving cost is not the same act as changing what may be admitted.
  void deriveGrainsFrom(size_t windowEventCount);
  /// Prices inspected source fill + two bounded sorts + rank assignment.
  /// Source qualification and retirement must be charged separately by the owner.
  void deriveSnapshotGrainsFrom(size_t sourceUnits, size_t eventCount,
                                size_t fillQuantum, size_t orderQuantum);
  /// **Task 3.7h: the one price for a window build.** Fill pages + the two bounded sorts and the rank
  /// pass over `eventCount` + the retirement of the pinned `releaseUnits` (one grain when nothing is
  /// pinned). The rolling selector positions its demand with this price and the admission prices the
  /// counted window with it, so the position and the lead test cannot drift into two estimates.
  void deriveWindowGrainsFrom(size_t sourceUnits, size_t eventCount, size_t releaseUnits,
                              size_t fillQuantum, size_t orderQuantum);
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
