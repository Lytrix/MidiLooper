//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackViewBuild.h"
#include "Utils/PlaybackSourceReadGuard.h"

#include <limits>

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define RUNTIME_PLAYBACK_VIEW_BUILD_MEM FLASHMEM
#else
#define RUNTIME_PLAYBACK_VIEW_BUILD_MEM
#endif

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuild::PreparationTurn::PreparationTurn(
    RuntimePlaybackViewBuild& builder) : builder_(builder) {
  PlaybackSourceReadGuard guard;
  if (builder_.executing_ || builder_.preparing_) return;
  builder_.preparing_ = true;
  builder_.preparationCancelled_ = false;
  acquired_ = true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuild::PreparationTurn::~PreparationTurn() {
  PlaybackSourceReadGuard guard;
  if (!acquired_) return;
  builder_.preparing_ = false;
  builder_.preparationCancelled_ = false;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuild::PreparationTurn::operator bool() const {
  return acquired_ && !builder_.preparationCancelled_;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::PreparationTurn::owns(
    const RuntimePlaybackViewBuild& builder) const {
  return &builder_ == &builder && static_cast<bool>(*this);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::cancelPreparation() {
  PlaybackSourceReadGuard guard;
  if (preparing_) preparationCancelled_ = true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  return execute([&]() -> bool {
  if (active()) return false;
  coverageOwned_ = false;
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, baseline, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) return false;
  destination_ = &destination;
  destinationGeneration_ = destination.storageGeneration;
  phase_ = Phase::Fill;
  return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    RuntimePlaybackViewFillScope scope) {
  return execute([&]() -> bool {
  if (active()) return false;
  coverageOwned_ = false;
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, source, sourceCount, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) {
    return false;
  }
  destination_ = &destination;
  destinationGeneration_ = destination.storageGeneration;
  phase_ = Phase::Fill;
  return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
    const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, RuntimePlaybackViewFillScope scope) {
  return execute([&]() -> bool {
  if (active()) return false;
  coverageOwned_ = false;
  destination_ = nullptr;
  order_.discard();
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, source, sourceIdentity, coverage, cursorAnchor,
                   ledgerBoundary, scope)) return false;
  destination_ = &destination;
  destinationGeneration_ = destination.storageGeneration;
  phase_ = Phase::Fill;
  return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, LoopSnapshot::EventWindowCursor& source,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary) {
  return execute([&]() -> bool {
  if (active()) return false;
  coverageOwned_ = false;
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, source, sourceIdentity, coverage, cursorAnchor, ledgerBoundary)) return false;
  destination_ = &destination;
  destinationGeneration_ = destination.storageGeneration;
  snapshotRankCursor_ = 0;
  phase_ = Phase::SnapshotFill;
  return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::beginCoverageQualification(
    RuntimePlaybackView& destination, LoopSnapshot::EventWindowCursor& source,
    const PlaybackViewSourceIdentity& identity, const PlaybackCoverageInterval& coverage) {
  return execute([&]() -> bool {
    if (active()) return false;
    coverageOwned_ = false; destination_ = nullptr; phase_ = Phase::Denied;
    if (!fill_.begin(destination, source, identity, coverage, {}, {})) return false;
    destination_ = &destination; snapshotRankCursor_ = 0;
    destinationGeneration_ = destination.storageGeneration;
    phase_ = Phase::Qualification; coverageOwned_ = true;
    return true;
  }, false);
}

#if REVISION_MANIFEST_READER
// C5: the manifest entries. The bodies live in one flash-resident helper; the guarded lambdas are single calls.
RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::beginManifestNow(
    RuntimePlaybackView& destination, CommittedWindowRowSource& source, const PlaybackViewSourceIdentity& identity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, bool qualification) {
  if (active()) return false;
  coverageOwned_ = false;
  destination_ = nullptr;
  phase_ = Phase::Denied;
  if (!fill_.begin(destination, source, identity, coverage, cursorAnchor, ledgerBoundary)) return false;
  destination_ = &destination;
  destinationGeneration_ = destination.storageGeneration;
  snapshotRankCursor_ = 0;
  phase_ = qualification ? Phase::Qualification : Phase::SnapshotFill;
  coverageOwned_ = qualification;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::begin(
    RuntimePlaybackView& destination, CommittedWindowRowSource& source,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary) {
  return execute([&]() -> bool {
    return beginManifestNow(destination, source, sourceIdentity, coverage, cursorAnchor, ledgerBoundary, false);
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::beginCoverageQualification(
    RuntimePlaybackView& destination, CommittedWindowRowSource& source,
    const PlaybackViewSourceIdentity& identity, const PlaybackCoverageInterval& coverage) {
  return execute([&]() -> bool { return beginManifestNow(destination, source, identity, coverage, {}, {}, true); },
                 false);
}
#endif

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewFillResult
RuntimePlaybackViewBuild::stepCoverageQualification(size_t quantum) {
  return execute([&]() -> RuntimePlaybackViewFillResult {
  if (phase_ != Phase::Qualification || destination_ == nullptr)
    return RuntimePlaybackViewFillResult::Denied;
  const auto result = fill_.step(quantum);
  if (result == RuntimePlaybackViewFillResult::Denied) phase_ = Phase::Denied;
  return result; // Even completed fill remains paused and unpublished.
  }, RuntimePlaybackViewFillResult::Denied);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::admitCoverageQualification() {
  return execute([&]() -> bool {
  if (phase_ != Phase::Qualification || !coverageOwned_ || destination_ == nullptr ||
      !destination_->sourceFilled || destination_->published || destination_->complete) return false;
  const Phase qualified = phase_;
  beginCanonicalRanks();
  if (phase_ == Phase::Denied) { phase_ = qualified; return false; }
  return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::beginCanonicalRanks() {
  auto& view = *destination_;
  snapshotRankCursor_ = 0;
  if (fill_.readsManifestWindow() && view.passOrigins != nullptr && view.canonicalRanks != nullptr &&
      view.storage.mergeScratch != nullptr && view.storage.capacity >= kManifestRankOriginSlots &&
      !view.passOriginsReady) {
    phase_ = Phase::ManifestRankCount;
    return;
  }
  if (!order_.begin(view, view.coverage.loopLengthTicks, RuntimePlaybackOrderMode::SnapshotCanonical)) {
    phase_ = Phase::Denied;
    return;
  }
  phase_ = order_.complete() ? Phase::SnapshotRanks : Phase::SnapshotCanonicalOrder;
}

// rank(i) = base[origin] + (rows of that origin before i): one count pass, then one assign pass in stream order. Each
// origin's rows arrive in their pass order (`ManifestWindowCursor` numbers them in stream order), so this is exactly the
// position the pass-major canonical sort gives, dense from 0 over the retained rows only. The counters live in the
// unpublished candidate's `mergeScratch`, which no sort uses on this path; clearing and prefix-summing the 256 slots is
// a fixed cost inside the first and last count grains, never a grain of its own.
RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::stepManifestRanks(size_t entryQuantum) {
  auto& view = *destination_;
  auto& storage = view.storage;
  uint16_t* slot = storage.mergeScratch;
  const size_t rows = storage.eventCount;
  if (phase_ == Phase::ManifestRankCount) {
    if (snapshotRankCursor_ == 0)
      for (size_t o = 0; o < kManifestRankOriginSlots; ++o) slot[o] = 0;
    for (size_t unit = 0; unit < entryQuantum && snapshotRankCursor_ < rows; ++unit, ++snapshotRankCursor_) {
      if (!storage.eventRetained(snapshotRankCursor_)) continue;
      const uint16_t origin = view.passOrigins[snapshotRankCursor_].pass;
      if (origin >= kManifestRankOriginSlots) { phase_ = Phase::Denied; return; }
      ++slot[origin];
    }
    if (snapshotRankCursor_ < rows) return;
    uint16_t base = 0;
    for (size_t o = 0; o < kManifestRankOriginSlots; ++o) {
      const uint16_t count = slot[o];
      slot[o] = base;
      base = static_cast<uint16_t>(base + count);
    }
    snapshotRankCursor_ = 0;
    phase_ = Phase::ManifestRankAssign;
    return;
  }
  for (size_t unit = 0; unit < entryQuantum && snapshotRankCursor_ < rows; ++unit, ++snapshotRankCursor_) {
    if (!storage.eventRetained(snapshotRankCursor_)) continue;
    view.canonicalRanks[snapshotRankCursor_] = slot[view.passOrigins[snapshotRankCursor_].pass]++;
  }
  if (snapshotRankCursor_ < rows) return;
  view.canonicalRanksReady = true;
  if (!order_.begin(view, view.coverage.loopLengthTicks, RuntimePlaybackOrderMode::SnapshotPlayback))
    phase_ = Phase::Denied;
  else phase_ = order_.complete() ? Phase::Complete : Phase::SnapshotPlaybackOrder;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuildGrain
RuntimePlaybackViewBuild::nextGrain() const {
  PlaybackSourceReadGuard guard;
  if (preparationCancelled_) return RuntimePlaybackViewBuildGrain::Denied;
  if (destination_ != nullptr && destination_->storageGeneration != destinationGeneration_)
    return RuntimePlaybackViewBuildGrain::Denied;
  if (fill_.appendStableCapture() && !fill_.liveCaptureCurrent()) {
    // The live generation the appended half was built against is gone (a pass commit re-based the capture). Invalidate the
    // candidate here so no consumer can read `complete` and adopt a window whose overlay half is stale.
    if (destination_ != nullptr && !destination_->published) {
      destination_->sourceFilled = false;
      destination_->complete = false;
    }
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  switch (phase_) {
    case Phase::Fill:
    case Phase::SnapshotFill:
      return RuntimePlaybackViewBuildGrain::Fill;
    case Phase::Order:
    case Phase::SnapshotCanonicalOrder:
    case Phase::SnapshotRanks:
    case Phase::SnapshotPlaybackOrder:
    case Phase::ManifestRankCount:
    case Phase::ManifestRankAssign:
      return RuntimePlaybackViewBuildGrain::Order;
    case Phase::Complete:
      return fill_.liveCaptureCurrent() ? RuntimePlaybackViewBuildGrain::Complete
                                        : RuntimePlaybackViewBuildGrain::Denied;
    case Phase::Qualification:
    case Phase::Idle:
    case Phase::Denied:
      return RuntimePlaybackViewBuildGrain::Denied;
  }
  return RuntimePlaybackViewBuildGrain::Denied;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuildGrain
RuntimePlaybackViewBuild::stepFill(size_t entryQuantum) {
  return execute([&]() -> RuntimePlaybackViewBuildGrain {
  if (phase_ != Phase::Fill && phase_ != Phase::SnapshotFill) {
    return nextGrain();
  }
  const RuntimePlaybackViewFillResult result = fill_.step(entryQuantum);
  if (result == RuntimePlaybackViewFillResult::Denied || destination_ == nullptr) {
    // A refused append (combined capacity, or a stale live-overlay generation) must never leave a window that still looks
    // complete: the previous ordered index no longer describes the content the build tried to cover.
    if (destination_ != nullptr && !destination_->published && fill_.appendStableCapture()) {
      destination_->sourceFilled = false;
      destination_->complete = false;
    }
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  if (result == RuntimePlaybackViewFillResult::Pending) {
    return RuntimePlaybackViewBuildGrain::Fill;
  }
  if (phase_ == Phase::SnapshotFill) {
    beginCanonicalRanks();
    return nextGrain();
  }
  if (fill_.appendStableCapture()) {
    // Slice 3: a live-overlay extension orders the appended tail only, so the published readable prefix never moves under
    // a consuming reader. The Record lane keeps the full delta order (nothing reads its live window).
    // The **first** overlay continuation is not yet readable: it must merge canonically with the base rows (an overlay
    // Off at the base's tick sorts before the base On). Only once a frontier has been published does an extension switch
    // to preserving the prefix; the caller is then reading [0, readableEventCount).
    const bool preservePrefix = fill_.continuingOverlay() && destination_->readableEventCount != 0;
    const bool ordered = order_.complete()
        ? (preservePrefix ? order_.extendPrivateOrderPreservingPrefix() : order_.extendPrivateOrder())
        : order_.begin(destination_->storage, destination_->coverage.loopLengthTicks);
    if (!ordered) { phase_ = Phase::Denied; return RuntimePlaybackViewBuildGrain::Denied; }
    destination_->complete = order_.complete();
    if (destination_->complete && fill_.continuingOverlay()) publishReadableFrontier();
    phase_ = order_.complete() ? Phase::Complete : Phase::Order;
    return nextGrain();
  }
  // The **storage** overload, so this order can be extended in place when a live-overlay continuation appends to the very
  // same window (`extendPrivateOrder` refuses a builder that owns the view's completion flag). The build owns
  // `destination_->complete` instead: it is true exactly when the ordered index covers every retained row.
  if (!order_.begin(destination_->storage, destination_->coverage.loopLengthTicks, nullptr,
      destination_->rawCoveragePrepared ? RuntimePlaybackOrderMode::RawPlaybackTick
                                        : RuntimePlaybackOrderMode::PlaybackPhase)) {
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  destination_->complete = order_.complete();
  phase_ = order_.complete() ? Phase::Complete : Phase::Order;
  return nextGrain();
  }, RuntimePlaybackViewBuildGrain::Denied);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM RuntimePlaybackViewBuildGrain
RuntimePlaybackViewBuild::stepOrder(size_t entryQuantum) {
  return execute([&]() -> RuntimePlaybackViewBuildGrain {
  if ((phase_ == Phase::Order || phase_ == Phase::Complete) &&
      !fill_.liveCaptureCurrent()) {
    if (destination_ != nullptr && !destination_->published) {
      destination_->sourceFilled = false;
      destination_->complete = false;
    }
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  if (phase_ == Phase::ManifestRankCount || phase_ == Phase::ManifestRankAssign) {
    if (entryQuantum != 0) stepManifestRanks(entryQuantum);
    return nextGrain();
  }
  if (phase_ == Phase::SnapshotRanks) {
    if (entryQuantum == 0) return nextGrain();
    auto& view = *destination_;
    for (size_t unit = 0; unit < entryQuantum && snapshotRankCursor_ < view.storage.eventCount; ++unit) {
      view.canonicalRanks[view.storage.order[snapshotRankCursor_]] = snapshotRankCursor_;
      ++snapshotRankCursor_;
    }
    if (snapshotRankCursor_ == view.storage.eventCount) {
      view.canonicalRanksReady = true;
      if (!order_.begin(view, view.coverage.loopLengthTicks, RuntimePlaybackOrderMode::SnapshotPlayback))
        phase_ = Phase::Denied;
      else phase_ = order_.complete() ? Phase::Complete : Phase::SnapshotPlaybackOrder;
    }
    return nextGrain();
  }
  if (phase_ != Phase::Order && phase_ != Phase::SnapshotCanonicalOrder && phase_ != Phase::SnapshotPlaybackOrder)
    return nextGrain();
  const bool canonical = phase_ == Phase::SnapshotCanonicalOrder;
  const RuntimePlaybackOrderBuildResult result = order_.step(entryQuantum);
  if (result == RuntimePlaybackOrderBuildResult::Denied) {
    phase_ = Phase::Denied;
    return RuntimePlaybackViewBuildGrain::Denied;
  }
  if (result == RuntimePlaybackOrderBuildResult::Complete) {
    if (!canonical) destination_->complete = true;
    // The ordered index now covers every retained row: publish the readable frontier atomically (one 16-bit store). This
    // is the only point a live window's frontier may advance; a partially ordered tail is never exposed.
    if (!canonical && fill_.continuingOverlay()) publishReadableFrontier();
    phase_ = canonical ? Phase::SnapshotRanks : Phase::Complete;
  }
  return nextGrain();
  }, RuntimePlaybackViewBuildGrain::Denied);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::active() const {
  return phase_ == Phase::Qualification || phase_ == Phase::Fill || phase_ == Phase::Order || phase_ == Phase::SnapshotFill ||
      phase_ == Phase::SnapshotCanonicalOrder || phase_ == Phase::SnapshotRanks ||
      phase_ == Phase::ManifestRankCount || phase_ == Phase::ManifestRankAssign ||
      phase_ == Phase::SnapshotPlaybackOrder;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::extendLiveCapture(
    const PlaybackViewSourceIdentity& identity) {
  return execute([&]() -> bool {
    if ((phase_ != Phase::Fill && phase_ != Phase::Complete) || !destination_ ||
        !fill_.extendLiveCapture(identity)) return false;
    phase_ = Phase::Fill;
    return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::beginLiveOverlayContinuation(
    const RuntimePlaybackViewSource& live, const PlaybackViewSourceIdentity& identity) {
  return execute([&]() -> bool {
    // One build identity: the committed base must be filled and ordered, and this call continues that same window.
    if (phase_ != Phase::Complete || destination_ == nullptr || fill_.continuingOverlay()) return false;
    if (!fill_.beginLiveContinuation(*destination_, live, identity)) return false;
    phase_ = Phase::Fill;
    return true;
  }, false);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::completedDestinationIs(
    const RuntimePlaybackView& destination) const {
  PlaybackSourceReadGuard guard;
  return !executing_ && destination_ == &destination &&
      destination.storageGeneration == destinationGeneration_ && destination.complete &&
      phase_ == Phase::Complete && nextGrain() == RuntimePlaybackViewBuildGrain::Complete;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::discard(
    const RuntimePlaybackView* preserveDestination) {
  PlaybackSourceReadGuard guard;
  if (executing_) {
    if (!discardRequested_) pendingPreserveDestination_ = preserveDestination;
    else if (pendingPreserveDestination_ != preserveDestination) pendingPreserveDestination_ = nullptr;
    discardRequested_ = true;
    return;
  }
  discardNow(preserveDestination);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::enterCall() {
  PlaybackSourceReadGuard guard;
  if (executing_ || preparationCancelled_) return false;
  if (destination_ != nullptr && destination_->storageGeneration != destinationGeneration_) {
    discardNow(nullptr); // Detach, but do not invalidate the replacement generation.
  }
  executing_ = true;
  return true;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool RuntimePlaybackViewBuild::finishCall() {
  PlaybackSourceReadGuard guard;
  if ((destination_ != nullptr && destination_->storageGeneration != destinationGeneration_) ||
      (fill_.ownsLiveCaptureBorrow() && !fill_.liveCaptureCurrent())) {
    discardRequested_ = true;
    pendingPreserveDestination_ = nullptr; // Never retain a stale private result.
  }
  const bool retained = !discardRequested_ && !preparationCancelled_;
  if (!retained) discardNow(pendingPreserveDestination_);
  pendingPreserveDestination_ = nullptr;
  discardRequested_ = false;
  executing_ = false;
  return retained;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::publishReadableFrontier() {
  if (destination_ == nullptr) return;
  const size_t ordered = order_.orderedEventCount();
  if (ordered > destination_->storage.eventCount) return; // never publish beyond the retained rows
  const uint16_t frontier = ordered > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(ordered);
  if (frontier > destination_->readableEventCount) destination_->readableEventCount = frontier;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void RuntimePlaybackViewBuild::discardNow(
    const RuntimePlaybackView* preserveDestination) {
  fill_.discard();
  order_.discard();
  if (destination_ != nullptr && destination_->storageGeneration == destinationGeneration_ &&
      destination_ != preserveDestination && !destination_->published) {
    destination_->sourceFilled = false;
    destination_->complete = false;
  }
  destination_ = nullptr;
  phase_ = Phase::Idle;
  coverageOwned_ = false;
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
  return canCompleteWithinLead(handoffLeadTicks(currentPhase, handoffPhase, loopLengthTicks), additionalGrains);
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM bool CoverageServiceContract::canCompleteWithinLead(
    uint64_t availableTicks, uint16_t additionalGrains) const {
  if (!isDeclared() || availableTicks < leadTicks) return false;
  const uint64_t grains = static_cast<uint64_t>(requiredGrains()) + additionalGrains;
  const uint64_t turns = grains * (static_cast<uint64_t>(maximumConsecutiveDeniedTurns) + 1u);
  // Divide the available lead instead of multiplying by phase advance: no uint64 overflow
  // can turn an impossible deadline into a successful admission.
  return turns <= (availableTicks - leadTicks) / maximumPhaseAdvancePerTurn;
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void CoverageServiceContract::deriveSnapshotGrainsFrom(
    size_t sourceUnits, size_t eventCount, size_t fillQuantum, size_t orderQuantum) {
  deriveWindowGrainsFrom(sourceUnits, eventCount, 0, fillQuantum, orderQuantum);
}

/// Task 3.7h — the **one** price for a window build, shared by the rolling selector's position and the
/// admission's lead test. `releaseUnits == 0` is the source-only shape (`deriveSnapshotGrainsFrom`), which
/// charges the single promotion grain and nothing for retirement.
RUNTIME_PLAYBACK_VIEW_BUILD_MEM void CoverageServiceContract::deriveManifestWindowGrantsFrom(
    size_t sourceUnits, size_t eventCount, size_t fillQuantum, size_t orderQuantum) {
  const auto grants = [](size_t work, size_t quantum) -> size_t {
    return quantum == 0 ? SIZE_MAX : work == 0 ? 0 : 1 + (work - 1) / quantum;
  };
  const size_t reads = grants(sourceUnits, fillQuantum);
  const size_t pass = grants(eventCount, orderQuantum);
  const size_t fills = reads == SIZE_MAX ? SIZE_MAX : reads + 1;  // + the admission turn
  const size_t orders = pass > SIZE_MAX / 3 ? SIZE_MAX : 3 * pass;  // dense count, dense assign, playback order
  requiredFillGrains = static_cast<uint16_t>(fills > UINT16_MAX ? UINT16_MAX : fills);
  requiredOrderGrains = static_cast<uint16_t>(orders > UINT16_MAX ? UINT16_MAX : orders);
  requiredPromotionGrains = 1;  // publication
}

RUNTIME_PLAYBACK_VIEW_BUILD_MEM void CoverageServiceContract::deriveWindowGrainsFrom(
    size_t sourceUnits, size_t eventCount, size_t releaseUnits, size_t fillQuantum, size_t orderQuantum) {
  const auto grains = [](size_t work, size_t quantum) -> size_t {
    return quantum == 0 ? SIZE_MAX : work == 0 ? 1 : 1 + (work - 1) / quantum;
  };
  const size_t fills = grains(sourceUnits, fillQuantum);
  const size_t sort = RuntimePlaybackOrderBuilder::requiredGrains(eventCount, orderQuantum);
  const size_t ranks = grains(eventCount, orderQuantum);
  const size_t orders = sort > (SIZE_MAX - ranks) / 2 ? SIZE_MAX : 2 * sort + ranks;
  const size_t releaseGrains = grains(releaseUnits, fillQuantum);
  const size_t promotion = releaseUnits == 0 ? 1 : (releaseGrains == SIZE_MAX ? SIZE_MAX : 1 + releaseGrains);
  requiredFillGrains = static_cast<uint16_t>(fills > UINT16_MAX ? UINT16_MAX : fills);
  requiredOrderGrains = static_cast<uint16_t>(orders > UINT16_MAX ? UINT16_MAX : orders);
  requiredPromotionGrains = static_cast<uint16_t>(promotion > UINT16_MAX ? UINT16_MAX : promotion);
}
