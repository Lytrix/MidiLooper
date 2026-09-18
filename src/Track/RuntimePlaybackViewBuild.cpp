//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackViewBuild.h"
#include "Utils/NoteEditMem.h"

#include <limits>

// Placement alias: the subsystem's cold code already shares `NOTE_EDIT_MEM` (see the sibling
// companion TUs), so this name documents the local rationale without redefining placement.
#define RUNTIME_PLAYBACK_VIEW_BUILD_MEM NOTE_EDIT_MEM

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  destination_ = nullptr;
  phase_ = Grain::Denied;
  if (!fill_.begin(destination, baseline, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) return false;
  destination_ = &destination;
  phase_ = Grain::Fill;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  destination_ = nullptr;
  phase_ = Grain::Denied;
  if (!fill_.begin(destination, source, sourceCount, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) {
    return false;
  }
  destination_ = &destination;
  phase_ = Grain::Fill;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
    const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, RuntimePlaybackViewFillScope scope) {
  destination_ = nullptr;
  phase_ = Grain::Denied;
  if (!fill_.begin(destination, source, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) return false;
  destination_ = &destination;
  phase_ = Grain::Fill;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuild::Grain
RuntimePlaybackViewBuild::nextGrain() const {
  switch (phase_) {
    case Grain::Fill:
      return RuntimePlaybackViewBuild::Grain::Fill;
    case Grain::Order:
      return RuntimePlaybackViewBuild::Grain::Order;
    case Grain::Complete:
      return RuntimePlaybackViewBuild::Grain::Complete;
    case Grain::Idle:
    case Grain::Denied:
      return RuntimePlaybackViewBuild::Grain::Denied;
  }
  return RuntimePlaybackViewBuild::Grain::Denied;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuild::Grain
RuntimePlaybackViewBuild::stepFill(size_t entryQuantum) {
  if (phase_ != Grain::Fill) {
    return nextGrain();
  }
  const RuntimePlaybackViewFillResult result = fill_.step(entryQuantum);
  if (result == RuntimePlaybackViewFillResult::Denied || destination_ == nullptr) {
    phase_ = Grain::Denied;
    return RuntimePlaybackViewBuild::Grain::Denied;
  }
  if (result == RuntimePlaybackViewFillResult::Pending) {
    return RuntimePlaybackViewBuild::Grain::Fill;
  }
  if (!order_.begin(*destination_, destination_->coverage.loopLengthTicks,
      destination_->rawCoveragePrepared ? RuntimePlaybackOrderMode::RawPlaybackTick
                                        : RuntimePlaybackOrderMode::PlaybackPhase)) {
    phase_ = Grain::Denied;
    return RuntimePlaybackViewBuild::Grain::Denied;
  }
  phase_ = order_.complete() ? Grain::Complete : Grain::Order;
  return nextGrain();
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuild::Grain
RuntimePlaybackViewBuild::stepOrder(size_t entryQuantum) {
  if (phase_ != Grain::Order) {
    return nextGrain();
  }
  const RuntimePlaybackOrderBuildResult result = order_.step(entryQuantum);
  if (result == RuntimePlaybackOrderBuildResult::Denied) {
    phase_ = Grain::Denied;
    return RuntimePlaybackViewBuild::Grain::Denied;
  }
  if (result == RuntimePlaybackOrderBuildResult::Complete) {
    phase_ = Grain::Complete;
  }
  return nextGrain();
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::active() const {
  return phase_ == Grain::Fill || phase_ == Grain::Order;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::discard(
    const RuntimePlaybackView* preserveDestination) {
  fill_.discard();
  order_.discard();
  if (destination_ != nullptr && destination_ != preserveDestination &&
      !destination_->published) {
    destination_->sourceFilled = false;
    destination_->complete = false;
  }
  destination_ = nullptr;
  phase_ = Grain::Idle;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool CoverageServiceContract::isDeclared() const {
  return maximumPhaseAdvancePerTurn > 0 && requiredGrains() > 0;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM uint16_t CoverageServiceContract::requiredGrains() const {
  const uint32_t total = static_cast<uint32_t>(requiredFillGrains) + requiredOrderGrains +
                         requiredPromotionGrains;
  return total > std::numeric_limits<uint16_t>::max() ? std::numeric_limits<uint16_t>::max()
                                                        : static_cast<uint16_t>(total);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool CoverageServiceContract::canCompleteBeforeHandoff(
    uint32_t currentPhase, uint32_t handoffPhase, uint32_t loopLengthTicks,
    uint16_t additionalGrains) const {
  if (!isDeclared() || loopLengthTicks == 0 || currentPhase >= loopLengthTicks ||
      handoffPhase >= loopLengthTicks) {
    return false;
  }
  const uint32_t grains = static_cast<uint32_t>(requiredGrains()) + additionalGrains;
  if (grains == 0) {
    return false;
  }
  // Every required grain may be preceded by the declared maximum number of
  // denied owner turns. Use 64-bit arithmetic so an invalid declaration
  // becomes a denial rather than wrapping into an unsafe admission.
  const uint64_t turns = static_cast<uint64_t>(grains) *
                         (static_cast<uint64_t>(maximumConsecutiveDeniedTurns) + 1U);
  const uint64_t requiredLead =
      turns * maximumPhaseAdvancePerTurn + static_cast<uint64_t>(leadTicks);
  const uint32_t available = handoffPhase >= currentPhase
                                 ? handoffPhase - currentPhase
                                 : loopLengthTicks - currentPhase + handoffPhase;
  return requiredLead <= available;
}
