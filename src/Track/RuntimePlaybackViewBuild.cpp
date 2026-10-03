//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackViewBuild.h"

#include <limits>

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define RUNTIME_PLAYBACK_VIEW_BUILD_MEM FLASHMEM
#else
#define RUNTIME_PLAYBACK_VIEW_BUILD_MEM
#endif

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, baseline, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) return false;
  destination_ = &destination;
  phase_ = Phase::Fill;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, source, sourceCount, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) {
    return false;
  }
  destination_ = &destination;
  phase_ = Phase::Fill;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
    const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, RuntimePlaybackViewFillScope scope) {
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, source, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) return false;
  destination_ = &destination;
  phase_ = Phase::Fill;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuildGrain
RuntimePlaybackViewBuild::nextGrain() const {
  switch (phase_) {
    case Phase::Fill:
      return RuntimePlaybackViewBuildGrain::Fill;
    case Phase::Order:
      return RuntimePlaybackViewBuildGrain::Order;
    case Phase::Complete:
      return RuntimePlaybackViewBuildGrain::Complete;
    case Phase::Idle:
    case Phase::Denied:
      return RuntimePlaybackViewBuildGrain::Denied;
  }
  return RuntimePlaybackViewBuildGrain::Denied;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuildGrain
RuntimePlaybackViewBuild::stepFill(size_t entryQuantum) {
  if (phase_ != Phase::Fill) {
    return nextGrain();
  }
  const RuntimePlaybackViewFillResult result = fill_.step(entryQuantum);
  if (result == RuntimePlaybackViewFillResult::Denied || destination_ == nullptr) {
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  if (result == RuntimePlaybackViewFillResult::Pending) {
    return RuntimePlaybackViewBuildGrain::Fill;
  }
  if (!order_.begin(*destination_, destination_->coverage.loopLengthTicks,
      destination_->rawCoveragePrepared ? RuntimePlaybackOrderMode::RawPlaybackTick
                                        : RuntimePlaybackOrderMode::PlaybackPhase)) {
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  phase_ = order_.complete() ? Phase::Complete : Phase::Order;
  return nextGrain();
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuildGrain
RuntimePlaybackViewBuild::stepOrder(size_t entryQuantum) {
  if (phase_ != Phase::Order) {
    return nextGrain();
  }
  const RuntimePlaybackOrderBuildResult result = order_.step(entryQuantum);
  if (result == RuntimePlaybackOrderBuildResult::Denied) {
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  if (result == RuntimePlaybackOrderBuildResult::Complete) {
    phase_ = Phase::Complete;
  }
  return nextGrain();
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::active() const {
  return phase_ == Phase::Fill || phase_ == Phase::Order;
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
  phase_ = Phase::Idle;
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
  // The lead available to the caller is the distance to the handoff, **named once** (`handoffLeadTicks`, in the
  // header beside this contract) so a scheduling reader of the same quantity cannot disagree with this admission
  // decision (DEC-108 §13b / task 3.10).
  const uint32_t available = handoffLeadTicks(currentPhase, handoffPhase, loopLengthTicks);
  return requiredLead <= available;
}
