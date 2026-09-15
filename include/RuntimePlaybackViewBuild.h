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
  uint32_t maximumPhaseAdvancePerTurn = 0;
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
