#include <cstdio>
//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackPlaybackRuntime.h"
#include "Loop.h"
#include "CommittedManifestLane.h"

#include <cstdlib>
#include <limits>
#include <new>
#include <optional>

#include "Utils/ExternalPoolAccounting.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "Utils/IntervalProjection.h"
#include "Utils/RuntimeProfiler.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define TRACK_PLAYBACK_RUNTIME_COLD_MEM FLASHMEM
#else
#define TRACK_PLAYBACK_RUNTIME_COLD_MEM
#endif

static inline uint32_t replenishNowUs();

namespace {
size_t recordingCaptureSize(const void* context) {
  return static_cast<const Loop*>(context)->capture.store.size();
}
const MidiEvent& recordingCaptureEventAt(const void* context, size_t index) {
  return static_cast<const Loop*>(context)->capture.store.at(index);
}
uint64_t recordingCaptureGeneration(const void* context) {
  return static_cast<const Loop*>(context)->captureDisplayRevision;
}
}  // namespace

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint64_t StopAdmissionContract::requiredRankedTailGrainsForPasses(
    size_t capacity, size_t maximumPassCount, size_t baseEventCount, size_t historyCount,
    size_t pendingChangeCount, size_t quantum) {
  if (maximumPassCount > capacity || baseEventCount > capacity ||
      pendingChangeCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return UINT64_MAX;
  const auto tail = RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(capacity, historyCount, quantum);
  const auto passes = RuntimeCompanionPasses::requiredGrainsUpperBound(
      capacity, maximumPassCount, historyCount, pendingChangeCount, quantum);
  const auto mapping = RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(baseEventCount, capacity, quantum);
  const auto bridge = RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(baseEventCount, capacity, quantum);
  if (tail == UINT64_MAX || passes == UINT64_MAX || mapping == UINT64_MAX || bridge == UINT64_MAX) return UINT64_MAX;
  const uint64_t rows = pendingChangeCount ? 1 + (pendingChangeCount - 1) / quantum : 1;
  // All horizons are fixed-capacity validated by the children. The sum is a
  // diagnostic/service estimate; callers keep it wide and resumable rather
  // than narrowing it into a stop-admission ceiling.
  return tail + rows + passes + mapping + bridge + 8;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint64_t StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(
    size_t capacity, size_t maximumPassCount, size_t baseEventCount, size_t historyCount,
    size_t pendingChangeCount, size_t quantum) {
  const auto total = requiredRankedTailGrainsForPasses(
      capacity, maximumPassCount, baseEventCount, historyCount, pendingChangeCount, quantum);
  if (total == UINT64_MAX) return UINT64_MAX;
  // Replace only tail normalization. All other children and transitions keep
  // their conservative bounds; the first canonical sort is charged upstream.
  return total - RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(capacity, historyCount, quantum) +
      RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(capacity, historyCount, quantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint64_t StopAdmissionContract::requiredRankedPreparationGrains(
    size_t capacity, size_t historyCount, size_t pendingChangeCount, size_t quantum) {
  const auto tail = requiredRankedTailGrains(capacity, historyCount, pendingChangeCount, quantum);
  if (tail == UINT64_MAX) return UINT64_MAX;
  return tail - RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(capacity, historyCount, quantum) +
      RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(capacity, historyCount, quantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint64_t StopAdmissionContract::requiredRankedTailGrains(
    size_t capacity, size_t historyCount, size_t pendingChangeCount, size_t quantum) {
  return requiredRankedTailGrainsForPasses(capacity, capacity, capacity, historyCount, pendingChangeCount, quantum);
}

bool StopAdmissionContract::isDeclared() const {
  return maximumClosureGrains > 0 && maximumOrderGrains > 0 &&
         maximumAtomicTransferGrains == 1 && orderEntryQuantum > 0 &&
         uint32_t(maximumClosureGrains) + maximumOrderGrains + maximumAtomicTransferGrains <= UINT16_MAX;
}

uint64_t StopAdmissionContract::requiredOrderGrains(size_t frozenEventCount) const {
  if (!orderEntryQuantum || frozenEventCount > RuntimePlaybackViewConfig::kCaptureStagingEvents) return UINT64_MAX;
  const size_t orderGrains = RuntimePlaybackOrderBuilder::requiredGrains(
      frozenEventCount, orderEntryQuantum);
  const size_t rewriteGrains = frozenEventCount == 0
                                   ? 0
                                   : 1 + (frozenEventCount - 1) / orderEntryQuantum;
  const size_t trimGrains = frozenEventCount == 0
                                ? 0
                                : 1 + (frozenEventCount - 1) /
                                          LoopEventStoreConfig::CHUNK_CAPACITY + 1;
  // Canonical raw order, canonical copy and raw-chunk retirement, then audible
  // phase order. The retirement allowance also covers the legacy rewrite trim.
  const size_t grains = orderGrains >
                                 (std::numeric_limits<size_t>::max() - rewriteGrains) / 2
                             ? std::numeric_limits<size_t>::max()
                             : orderGrains * 2 + rewriteGrains + trimGrains;
  return static_cast<uint64_t>(grains);
}

bool StopAdmissionContract::canFinalize(size_t openReservationCount,
                                        size_t frozenEventCount) const {
  if (!isDeclared() || openReservationCount > maximumClosureGrains ||
      openReservationCount > RuntimePlaybackViewConfig::kMaxCaptureNoteReservations ||
      frozenEventCount > RuntimePlaybackViewConfig::kCaptureStagingEvents) {
    return false;
  }
  const auto required = requiredOrderGrains(frozenEventCount);
  return required != UINT64_MAX;
}

uint16_t StopAdmissionContract::maximumTotalGrains() const {
  if (!isDeclared()) {
    return 0;
  }
  const uint32_t total = static_cast<uint32_t>(maximumClosureGrains) +
                         maximumOrderGrains + maximumAtomicTransferGrains;
  return total > std::numeric_limits<uint16_t>::max()
             ? std::numeric_limits<uint16_t>::max()
             : static_cast<uint16_t>(total);
}

void PendingStopAdmission::reset() {
  frozenGeneration = 0;
  stopTick = 0;
  transportCommandEpoch = 0;
  remainingClosureGrains = 0;
  remainingOrderGrains = 0;
  remainingRankedPreparationGrains = 0;
  rankedPreparationQuantum = 0;
  remainingCanonicalPreparationGrains = 0;
  canonicalPreparationQuantum = 0;
  remainingHistoryRecycleGrains = 0;
  historyRecycleQuantum = 0;
  historyRecycleOwner = nullptr;
  historyRecycleGeneration = 0;
  historyRecycleStarted = false;
  phase = PendingStopPhase::ReservedClosure;
  destination = PendingStopDestination::Playing;
  retargetedFromPlaying = false;
  canonicalBaseAbandoned = false;
  canonicalBridgeUnavailable = false;
  supersededCompanionExchange = false;
  pending = false;
  sessionExitReceiptRing = static_cast<size_t>(-1);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::begin(uint32_t generation, uint32_t tick, uint32_t commandEpoch,
                                 size_t openReservationCount, size_t frozenEventCount,
                                 const StopAdmissionContract& contract, uint64_t rankedPreparationGrains,
                                 uint64_t canonicalPreparationGrains, uint64_t historyRecycleGrains,
                                 uint64_t undoCollectionGrains) {
  if (pending || openReservationCount > kMaximumClosureGrains ||
      !contract.canFinalize(openReservationCount, frozenEventCount) ||
      rankedPreparationGrains == UINT64_MAX || canonicalPreparationGrains == UINT64_MAX ||
      historyRecycleGrains == UINT64_MAX || undoCollectionGrains == UINT64_MAX) {
    return false;
  }
  frozenGeneration = generation;
  stopTick = tick;
  transportCommandEpoch = commandEpoch;
  remainingClosureGrains = static_cast<uint16_t>(openReservationCount);
  remainingOrderGrains = contract.requiredOrderGrains(frozenEventCount);
  remainingRankedPreparationGrains = rankedPreparationGrains;
  rankedPreparationQuantum = rankedPreparationGrains ? contract.orderEntryQuantum : 0;
  remainingCanonicalPreparationGrains = canonicalPreparationGrains;
  canonicalPreparationQuantum = canonicalPreparationGrains ? contract.orderEntryQuantum : 0;
  remainingHistoryRecycleGrains = historyRecycleGrains;
  historyRecycleQuantum = historyRecycleGrains ? contract.orderEntryQuantum : 0;
  remainingUndoCollectionGrains = undoCollectionGrains;
  undoCollectionQuantum = undoCollectionGrains ? contract.orderEntryQuantum : 0;
  historyRecycleOwner = nullptr;
  historyRecycleGeneration = 0;
  historyRecycleStarted = false;
  phase = remainingClosureGrains == 0 ? PendingStopPhase::CanonicalOrder
                                      : PendingStopPhase::ReservedClosure;
  destination = PendingStopDestination::Playing;
  canonicalBaseAbandoned = false;
  canonicalBridgeUnavailable = false;
  supersededCompanionExchange = false;
  pending = true;
  return true;
}

bool PendingStopAdmission::consumeClosureGrain() {
  if (!pending || remainingClosureGrains == 0) {
    return false;
  }
  --remainingClosureGrains;
  if (remainingClosureGrains == 0) {
    phase = PendingStopPhase::CanonicalOrder;
  }
  return true;
}

bool PendingStopAdmission::consumeOrderGrain() {
  if (!pending || remainingClosureGrains != 0 || remainingOrderGrains == 0) {
    return false;
  }
  --remainingOrderGrains;
  return true;
}

bool PendingStopAdmission::consumeRankedPreparationGrain() {
  if (!pending || remainingClosureGrains != 0 || !rankedPreparationQuantum) return false;
  if (remainingRankedPreparationGrains) --remainingRankedPreparationGrains;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::consumeCanonicalPreparationGrain() {
  if (!pending || remainingClosureGrains != 0 || !canonicalPreparationQuantum) return false;
  if (remainingCanonicalPreparationGrains) --remainingCanonicalPreparationGrains;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::consumeUndoCollectionGrain() {
  if (!pending || remainingClosureGrains != 0 || !undoCollectionQuantum) return false;
  if (remainingUndoCollectionGrains) --remainingUndoCollectionGrains;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::consumeHistoryRecycleGrain() {
  if (phase != PendingStopPhase::DeferredCleanup || !finalizationComplete() ||
      !historyRecycleQuantum) return false;
  if (remainingHistoryRecycleGrains) --remainingHistoryRecycleGrains;
  return true;
}

void PendingStopAdmission::requestTransportStop() {
  // Transport Stop is level-triggered: TrackManager can deliver the same
  // stopped transport state on every service turn.  Retarget the frozen
  // candidate once, then retain that epoch until the deferred owner reaches
  // its terminal STOPPED transition.  Advancing it repeatedly invalidates
  // canonical preparation that was correctly rebound to the first Stop.
  if (!pending || destination == PendingStopDestination::Stopped) {
    return;
  }
  ++transportCommandEpoch;
  destination = PendingStopDestination::Stopped;
  retargetedFromPlaying = true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::retireCompletedPreparation() {
  if (!pending || remainingClosureGrains) return false;
  switch (phase) {
    case PendingStopPhase::PrepareTransfer: remainingOrderGrains = 0; break;
    case PendingStopPhase::CompanionHistory: remainingCanonicalPreparationGrains = 0; break;
    case PendingStopPhase::UndoCollection: remainingUndoCollectionGrains = 0; break;
    case PendingStopPhase::CompanionBridge: remainingRankedPreparationGrains = 0; break;
    default: return false;
  }
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::retireSupersededPreparation() {
  if (!pending || remainingClosureGrains || remainingOrderGrains) return false;
  switch (phase) {
    case PendingStopPhase::CompanionHistory:
    case PendingStopPhase::UndoCollection:
    case PendingStopPhase::CompanionBridge:
    case PendingStopPhase::AtomicTransfer:
      remainingRankedPreparationGrains = 0;
      remainingCanonicalPreparationGrains = 0;
      remainingUndoCollectionGrains = 0;
      return true;
    default:
      return false;
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::retireSkippedCompanionPreparation(bool emptyCapture) {
  if (!pending || (!emptyCapture && destination != PendingStopDestination::Stopped) ||
      phase != PendingStopPhase::PrepareTransfer || remainingClosureGrains || remainingOrderGrains)
    return false;
  remainingRankedPreparationGrains = remainingCanonicalPreparationGrains = 0;
  // The STOPPED/empty route skips companion preparation, so it has no prepared
  // undo-collection inputs. Retire that estimate with the others.
  remainingUndoCollectionGrains = 0;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::retireCompanionSkippedForBaseAbandonment() {
  if (!pending || phase != PendingStopPhase::PrepareTransfer || remainingClosureGrains)
    return false;
  // Cause-based, destination-independent: the canonical base was abandoned, so the companion
  // path can never begin on this job and the estimates it would have consumed are not work.
  // Ordering completed before the escape fired, so un-spent order/preparation estimates retire
  // with the companion family. Closure stays an obligation (its grain completed); the caller
  // must be at the escape's phase, which the precondition pins.
  remainingOrderGrains = remainingRankedPreparationGrains = remainingCanonicalPreparationGrains = 0;
  remainingUndoCollectionGrains = 0;
  return true;
}

bool PendingStopAdmission::canCommit(uint32_t commandEpoch,
                                     PendingStopDestination requestedDestination) const {
  return finalizationComplete() && transportCommandEpoch == commandEpoch &&
         destination == requestedDestination;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool FrozenCaptureOrderBinding::matches(
    const CaptureAdmissionStaging& staging, uint64_t currentStorageGeneration) const {
  return bound && storageGeneration == currentStorageGeneration && staging.frozen && staging.captureGeneration == generation &&
         staging.frozenGeneration == generation && staging.storage.eventCount == eventCount;
}

namespace {

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackViewSet* allocateRuntimePlaybackViewSet() {
  void* memory = extmem_malloc(sizeof(RuntimePlaybackViewSet));
  if (memory == nullptr) {
    memory = std::malloc(sizeof(RuntimePlaybackViewSet));
  }
  return memory == nullptr ? nullptr : new (memory) RuntimePlaybackViewSet();
}

void releaseRuntimePlaybackViewSet(RuntimePlaybackViewSet* viewSet) {
  if (viewSet == nullptr) {
    return;
  }
  viewSet->~RuntimePlaybackViewSet();
  if (isInExternalMemoryPool(viewSet)) {
    extmem_free(viewSet);
  } else {
    std::free(viewSet);
  }
}

}  // namespace

bool LoopPlaybackRuntime::isStale(uint32_t loopRevision, uint32_t trackGeneration) const {
  return cachedLoopRevision != loopRevision || cachedTrackGeneration != trackGeneration;
}

void LoopPlaybackRuntime::syncRevision(uint32_t loopRevision, uint32_t trackGeneration) {
  cachedLoopRevision = loopRevision;
  cachedTrackGeneration = trackGeneration;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::reset(
    bool preserveLedger, const RuntimePlaybackViewBuild::PreparationTurn* primary,
    const RuntimePlaybackViewBuild::PreparationTurn* bridge) {
  std::optional<RuntimePlaybackViewBuild::PreparationTurn> localPrimary, localBridge;
  bool preparationBusy = false;
  {
    PlaybackSourceReadGuard guard;
    preparationBusy = viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
        (primary && !primary->owns(viewBuild)) || (bridge && !bridge->owns(bridgeWindowBuild)) ||
        (viewBuild.preparationInProgress() && (!primary || !primary->owns(viewBuild))) ||
        (bridgeWindowBuild.preparationInProgress() && (!bridge || !bridge->owns(bridgeWindowBuild)));
    if (preparationBusy) {
      viewBuild.cancelPreparation();
      bridgeWindowBuild.cancelPreparation();
      viewBuild.discard();
      bridgeWindowBuild.discard();
    } else {
      // Reserve both lanes before restoring IRQs; destruction itself stays unmasked.
      if (!primary) { localPrimary.emplace(viewBuild); primary = &*localPrimary; }
      if (!bridge) { localBridge.emplace(bridgeWindowBuild); bridge = &*localBridge; }
    }
  }
  if (captureStopOwner && captureStopOwner->pending) {
    captureStopOwner->requestTransportStop();
    if (auto* staging = captureStagingIfPrewarmed()) {
      (void)canonicalPassInputs.retargetFrozenCommandEpoch(
          captureStopOwner->transportCommandEpoch, *staging);
    }
    return false;
  }
  if (viewSet)
    for (const auto& staging : viewSet->captureStagings)
      if (staging.frozen) return false; // admitted interval awaiting the single worker
  // A refused reset must not clear an executing builder or its private view.
  // Cancellation completes at that call's boundary; the reset caller retries.
  if (preparationBusy) return false;
  retireCoverageSource();
  // Task 3.7a (I2): the retained-source gather is this lane's other half-owned work, so a reset drops it too -- its
  // buffer is per-slot external memory and must not outlive the operation that owed it. Task 3.7a (I3b): the page ring
  // is the same kind of work for the interval that cannot fit a window.
  discardRetainedGather();
  discardPagedWindow();
  bridgeWindowBuild.discard();
  bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
  bridgeWindowContextIndex = RuntimePlaybackBridgeLifecycle::kNoBridge;
  bridgeWindowContextGeneration = 0;
  discardCanonicalWindow();
  discardFrozenCanonicalTail();
  discardFrozenCaptureBridge();
  frozenCaptureCompanions.discard();
  frozenCompanionPasses.discard();
  canonicalPassInputs.discard();
  canonicalPrefixPhase = CanonicalPrefixPhase::Idle; canonicalPrefixPasses = nullptr;
  canonicalPrefixGeneration = 0; canonicalPrefixLength = 0;
  canonicalPrefixStorageOwner = nullptr; canonicalPrefixStorageGeneration = 0;
  // The deferred-convergence obligation is deliberately **not** cancelled here. `reset()` is also the
  // ordinary playback-state invalidation (`resetPlaybackStateForSlot` -> `invalidatePlaybackMergedMidiEvents`
  // -> `releaseEligiblePlaybackRuntimeSlots`), and cancelling there erased the obligation a stop had
  // just recorded (measured by fixture 1.3: an overdub stop reset the runtime and the obligation was
  // gone before the owner turn ever saw it). Slot clear/load/replacement is detected by the owner turn
  // from the loop incarnation itself: a reset timeline restarts `committedPassRevision()` below the
  // obligation's target, which no legitimate generation advance can do.
  frozenCompanionBridgeMap.discard();
  frozenCompanionApply.discard();
  frozenCompanionReconstruction.discard();
  frozenCompanionSpans.discard();
  frozenCompanionOverlaps.discard();
  frozenCompanionDeleteLookup.discard();
  frozenCompanionPairLookup.discard();
  bridgeStream.reset();
  bridgeProjectionCycle = 0;
  mergedMidiEvents.clear();
  // Preserve one view across the reset: the non-audible first-record return view
  // (the retained raw bar) takes priority, then the bound record-session return
  // window. The bound window has no other source once the take is sealed, so the
  // launch's own cache reset must not clear it before the first root publishes.
  uint8_t preservedIndex = RuntimePlaybackViewLifecycle::kNoView;
  if (preserveLedger && viewSet != nullptr) {
    if (viewSet->lifecycle.retained != RuntimePlaybackViewLifecycle::kNoView &&
        viewSet->lifecycle.retained < viewSet->views.size()) {
      preservedIndex = viewSet->lifecycle.retained;
    } else {
      const uint8_t window = viewSet->firstRecordReturnWindowIndex();
      if (window != RuntimePlaybackViewLifecycle::kNoView && viewSet->views[window].complete) {
        preservedIndex = window;
      }
    }
  }
  const RuntimePlaybackView* preservedView =
      (viewSet != nullptr && preservedIndex < viewSet->views.size())
          ? &viewSet->views[preservedIndex] : nullptr;
  viewBuild.discard(preservedView);
  completedPromisePendingJudgement = false;
  admittedPromiseInvalidated = false;
  viewBuildWork = {};
  readyPositionedLater = false;
  readyPositionedDeadlineTick = 0;
  readyPositionedClockEpoch = 0;
  // A preserved complete raw Record bar keeps the generation it was built from.
  // Clearing this stamp while retaining the view would make a stale view look
  // indistinguishable from an unstamped one at the stop-edge bind.
  if (preservedView == nullptr || !preservedView->rawCoveragePrepared ||
      !preservedView->complete || viewSet->lifecycle.retained != preservedIndex) {
    recordingWindowGeneration = 0;
    recordingWindowGenerationValid = false;
  }
  discardFrozenCaptureOrder();
  discardFrozenCaptureCanonicalOrder();
  if (viewSet != nullptr) {
    if (preservedIndex != RuntimePlaybackViewLifecycle::kNoView)
      viewSet->resetMetadataPreserveView(preservedIndex);
    else
      viewSet->resetMetadata();
  }
  // The two questions this reset answers are **separate**, and task 3.14 needs them separate: the revision/generation
  // caches describe the *merged representation* (which every caller of this reset invalidates, so they always clear),
  // while the ledger is the set of notes **sounding on the wire**. Coupling them is what made a content state change
  // kill soundings it never touched: `preserveLedger` now governs the ledger and only the ledger.
  cachedLoopRevision = 0;
  cachedTrackGeneration = 0;
  launchPreparation = LaunchPreparation::None;
  launchBoundTick = 0;
  rolling = RollingFrame{};
  if (!preserveLedger) {
    ledger.clear();
  }
  PlaybackSourceReadGuard completionGuard;
  const bool completed = primary->owns(viewBuild) && bridge->owns(bridgeWindowBuild);
  // Resolve cancellation and release locally-owned turns atomically at the end
  // of destruction. A releaseSlot caller retains its supplied pair for retirement.
  localBridge.reset();
  localPrimary.reset();
  return completed;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime::~LoopPlaybackRuntime() {
  viewBuild.discard(); // Detach source and destination before either can be released.
  bridgeWindowBuild.discard();
  canonicalCaptureOutput.store.clear();
  releaseRuntimePlaybackViewSet(viewSet);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prewarmRollingViews() {
  if (viewSet == nullptr) viewSet = allocateRuntimePlaybackViewSet();
  if (!viewSet) return false;
  if (viewSet->prewarmed) return true;
  // Never reset accepted capture staging when upgrading an occupancy-only slot.
  for (const auto& staging : viewSet->captureStagings)
    if (staging.storage.eventCount || staging.frozen || staging.openReservationCount()) return false;
  return viewSet->prewarm(false, false);
}

const RuntimePlaybackView* LoopPlaybackRuntime::boundLaunchView(
    const PlaybackViewSourceIdentity& source, uint32_t loopLength, uint32_t phase, uint32_t tick) {
  const auto* set = viewSetIfPrewarmed();
  if (launchPreparation != LaunchPreparation::Bound || !set || set->lifecycle.current >= set->views.size()) return nullptr;
  const auto& view = set->views[set->lifecycle.current];
  const uint32_t elapsed = tick - launchBoundTick;
  // Once bound, the interval version is fixed until its end, including after an edit.
  if (view.complete && view.published &&
      view.source.sourceGeneration == source.sourceGeneration && view.source.origin == PlaybackWindowOrigin::Committed &&
      view.coverage.loopLengthTicks == loopLength && view.coverage.startPhase == 0 &&
      elapsed < view.coverage.lengthTicks && phase == elapsed) return &view;
  // Expiry/invalidation is sticky: returning a modular phase cannot relaunch it.
  launchPreparation = LaunchPreparation::None;
  return nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prewarmCaptureStaging() {
  if (viewSet == nullptr) {
    viewSet = allocateRuntimePlaybackViewSet();
  }
  return viewSet != nullptr && viewSet->prewarmCaptureStagings();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::ensureViewSet() {
  if (viewSet == nullptr) {
    viewSet = allocateRuntimePlaybackViewSet();
  }
  return viewSet != nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::stepViewPrewarm(
    uint32_t identity, bool fullTakeAdmissible, size_t companionRowCapacity, uint8_t maxUnits,
    uint32_t budgetUs) {
  if (viewPrewarmPhase == ViewPrewarmPhase::Prepared && viewPrewarmIdentity == identity) {
    return true;
  }
  if (viewPrewarmPhase == ViewPrewarmPhase::NotPrepared || viewPrewarmIdentity != identity) {
    viewPrewarmIdentity = identity;
    viewPrewarmPhase = ViewPrewarmPhase::Preparing;
    viewPrewarmCursor = 0;
    viewPrewarmResourceMask = 0;
    viewPrewarmSteps = 0;
  }
  if (!ensureViewSet()) {
    return false;
  }
  // Upgrading must never reset an admitted interval (the same guard as `prewarmViews`).
  if (!viewSet->prewarmed) {
    for (const auto& staging : viewSet->captureStagings) {
      if (staging.frozen || staging.storage.eventCount || staging.openReservationCount()) {
        return false;
      }
    }
  }
  const uint32_t startUs = replenishNowUs();
  const uint8_t units = maxUnits == 0 ? 1u : maxUnits;
  for (uint8_t unit = 0; unit < units; ++unit) {
    bool ok = true;
    const uint8_t cursor = viewPrewarmCursor;
    if (cursor < 10) {
      ok = viewSet->captureStagings[cursor].prewarm();
    } else if (cursor < 13) {
      ok = viewSet->views[cursor - 10].prewarm(RuntimePlaybackViewConfig::kEventsPerView);
    } else if (cursor < 15) {
      if (fullTakeAdmissible) {
        ok = viewSet->bridgeBases[cursor - 13].prewarm(RuntimePlaybackViewConfig::kEventsPerView);
      }
    } else if (cursor < 17) {
      if (fullTakeAdmissible) {
        const uint8_t bridgeIndex = static_cast<uint8_t>(cursor - 15);
        if (prewarmSubOwner != cursor) {
          prewarmSubOwner = cursor;
          prewarmSubStage = 0;
          prewarmSubCursor = 0;
        }
        auto bridgeResult = FixedPlaybackBridgeStorage::PrewarmStepResult::Progress;
        do {
          bridgeResult = viewSet->bridges[bridgeIndex].stepPrewarm(
              RuntimePlaybackViewConfig::kRecordsPerBridge, prewarmSubStage,
              prewarmSubCursor);
          if (bridgeResult == FixedPlaybackBridgeStorage::PrewarmStepResult::Failed) {
            ok = false;
            break;
          }
        } while (bridgeResult != FixedPlaybackBridgeStorage::PrewarmStepResult::Complete &&
                 replenishNowUs() - startUs < budgetUs);
        if (bridgeResult != FixedPlaybackBridgeStorage::PrewarmStepResult::Complete) {
          // A sub-unit advanced (or the allocation was retried); stay on this top cursor.
          return false;
        }
      }
    } else if (cursor == 17) {
      if (fullTakeAdmissible) {
        viewSet->wholeTakeReserved = true;
      } else if (!viewSet->releaseWholeTakeViews()) {
        ok = false;
      }
      if (ok) {
        viewSet->resetMetadata();
        viewSet->prewarmed = true;
      }
    } else if (cursor == 18) {
      ok = prewarmCanonicalCaptureOutput();
      if (ok) viewPrewarmResourceMask = static_cast<uint8_t>(viewPrewarmResourceMask | 0x04u);
    } else if (cursor == 19) {
      ok = frozenCaptureCompanions.ensureCapacity(companionRowCapacity);
    } else if (cursor < 27) {
      switch (static_cast<uint8_t>(cursor - 20)) {
        case 0:
          ok = frozenCompanionPairLookup.prewarm(RuntimePlaybackViewConfig::kEventsPerView);
          break;
        case 1:
          ok = frozenCompanionSpans.prewarm(RuntimePlaybackViewConfig::kEventsPerView);
          break;
        case 2:
          ok = frozenCompanionReconstruction.prewarm(2 * RuntimePlaybackViewConfig::kEventsPerView);
          break;
        case 3:
          ok = frozenCompanionApply.prewarm(RuntimePlaybackViewConfig::kEventsPerView);
          break;
        case 4:
          ok = frozenCompanionBridgeMap.prewarm(RuntimePlaybackViewConfig::kRecordsPerBridge);
          break;
        case 5:
          ok = frozenCompanionPasses.prewarm(RuntimePlaybackViewConfig::kEventsPerView);
          break;
        case 6: {
          if (prewarmSubOwner != cursor) {
            prewarmSubOwner = cursor;
            prewarmSubStage = 0;
            prewarmSubCursor = 0;
          }
          auto canonicalResult = RuntimeCanonicalPassInputs::PrewarmStepResult::Progress;
          do {
            canonicalResult = canonicalPassInputs.stepPrewarm(
                RuntimePlaybackViewConfig::kEventsPerView, prewarmSubStage, prewarmSubCursor);
            if (canonicalResult == RuntimeCanonicalPassInputs::PrewarmStepResult::Failed) {
              ok = false;
              break;
            }
          } while (canonicalResult != RuntimeCanonicalPassInputs::PrewarmStepResult::Complete &&
                   replenishNowUs() - startUs < budgetUs);
          if (canonicalResult != RuntimeCanonicalPassInputs::PrewarmStepResult::Complete) {
            return false;
          }
          break;
        }
        default:
          ok = false;
          break;
      }
    } else if (cursor == 27) {
      viewPrewarmResourceMask = static_cast<uint8_t>(viewPrewarmResourceMask | 0x02u);
      viewPrewarmPhase = ViewPrewarmPhase::Prepared;
      return true;
    } else {
      return false;
    }
    if (!ok) {
      // The cursor stays on the failed unit: the next owner turn retries while the Record edge
      // remains armed. No hidden "finish everything now" path.
      return false;
    }
    ++viewPrewarmCursor;
    ++viewPrewarmSteps;
    if (budgetUs != 0 && replenishNowUs() - startUs >= budgetUs) {
      break;
    }
  }
  return viewPrewarmPhase == ViewPrewarmPhase::Prepared;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prewarmViews(
    size_t companionRowCapacity, bool fullTakeAdmissible) {
#if defined(PIO_UNIT_TEST_NATIVE)
  ++monolithicViewPrewarmCallsForTest();
#endif
  // DEC-239 profile scope (long: pool allocation loops, unbounded count; synchronous, no wait). No
  // result: the body has eight exits and the scope must not restructure them.
  PROFILE_SCOPE_LONG(profileScope, RuntimeProfiler::ProfileId::PlaybackPrewarm_views);
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCaptureStaging);
  const bool stagingReady = prewarmCaptureStaging();
  ExternalPoolAccounting::endTag();
  if (!stagingReady) return false;
  if (!viewSet->prewarmed)
    for (const auto& staging : viewSet->captureStagings)
      if (staging.frozen || staging.storage.eventCount || staging.openReservationCount())
        return false; // Upgrading must never reset any admitted interval.
  if (!viewSet->prewarm(fullTakeAdmissible)) return false;
  // Derived row reserve (DEC-066): a sparse slot does not take the full row horizon.
  // `ensureCapacity` only grows, so a later denser interval still fits.
  if (!frozenCaptureCompanions.ensureCapacity(companionRowCapacity)) return false;
#define RUNTIME_PREWARM_TAGGED(resultName, tag, expr) \
  ExternalPoolAccounting::beginTag(tag);              \
  const bool resultName = (expr);                     \
  ExternalPoolAccounting::endTag();                   \
  if (!resultName) return false;
  RUNTIME_PREWARM_TAGGED(prewarmPairLookupOk, ExternalPoolAccounting::TagCompanionPairLookup, frozenCompanionPairLookup.prewarm(RuntimePlaybackViewConfig::kEventsPerView))
  RUNTIME_PREWARM_TAGGED(prewarmSpansOk, ExternalPoolAccounting::TagCompanionSpans, frozenCompanionSpans.prewarm(RuntimePlaybackViewConfig::kEventsPerView))
  RUNTIME_PREWARM_TAGGED(prewarmReconstructionOk, ExternalPoolAccounting::TagCompanionReconstruction, frozenCompanionReconstruction.prewarm(2 * RuntimePlaybackViewConfig::kEventsPerView))
  RUNTIME_PREWARM_TAGGED(prewarmApplyOk, ExternalPoolAccounting::TagCompanionApply, frozenCompanionApply.prewarm(RuntimePlaybackViewConfig::kEventsPerView))
  RUNTIME_PREWARM_TAGGED(prewarmBridgeMapOk, ExternalPoolAccounting::TagCompanionBridgeMap, frozenCompanionBridgeMap.prewarm(RuntimePlaybackViewConfig::kRecordsPerBridge))
  RUNTIME_PREWARM_TAGGED(prewarmPassesOk, ExternalPoolAccounting::TagCompanionPasses, frozenCompanionPasses.prewarm(RuntimePlaybackViewConfig::kEventsPerView))
  RUNTIME_PREWARM_TAGGED(prewarmCanonicalInputsOk, ExternalPoolAccounting::TagCanonicalPassInputs, canonicalPassInputs.prewarm(RuntimePlaybackViewConfig::kEventsPerView))
#undef RUNTIME_PREWARM_TAGGED
  if (!fullTakeAdmissible) {
    viewSet->releaseWholeTakeViews();
    return true;
  }
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::publishStoppedOracleView(
    const MidiEvent* source, size_t sourceCount, const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage) {
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || !coverage.isValid() || sourceCount > RuntimePlaybackViewConfig::kEventsPerView ||
      (sourceCount != 0 && source == nullptr)) {
    return false;
  }

  RuntimePlaybackView& current = set->views[set->lifecycle.current];
  if (current.published && current.complete && current.source == sourceIdentity &&
      current.coverage.startPhase == coverage.startPhase &&
      current.coverage.lengthTicks == coverage.lengthTicks &&
      current.coverage.loopLengthTicks == coverage.loopLengthTicks &&
      current.ledgerBoundary.matches(ledger)) {
    return true;
  }

  // This oracle path is STOPPED-only. It intentionally completes fill then
  // order in one cold call, while the active path uses the resumable builder.
  current.resetMetadata();
  LedgerBoundarySummary boundary;
  boundary.capture(ledger);
  RuntimePlaybackViewBuild oracleBuild;
  const EmissionCursor anchor{0, coverage.startPhase, 0, 0, sourceIdentity.sourceGeneration};
  if (!oracleBuild.begin(current, source, sourceCount, sourceIdentity, coverage, anchor, boundary)) {
    return false;
  }
  while (oracleBuild.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) {
    if (oracleBuild.stepFill(sourceCount == 0 ? 1 : sourceCount) ==
        RuntimePlaybackViewBuildGrain::Denied) {
      current.resetMetadata();
      return false;
    }
  }
  while (oracleBuild.nextGrain() == RuntimePlaybackViewBuildGrain::Order) {
    if (oracleBuild.stepOrder(sourceCount == 0 ? 1 : sourceCount) ==
        RuntimePlaybackViewBuildGrain::Denied) {
      current.resetMetadata();
      return false;
    }
  }
  if (oracleBuild.nextGrain() != RuntimePlaybackViewBuildGrain::Complete) {
    current.resetMetadata();
    return false;
  }
  current.published = true;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::publishStoppedOracleView(
    const RuntimeCompanionPasses& baseline, const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage) {
  auto* set = viewSetIfPrewarmed();
  // RC-MEM-2: a slot that reserved no whole-take representation refuses a full-take
  // publication explicitly. The producers already decline for a loop that cannot build a
  // full-loop window; this makes the reservation and the publication contract the same
  // decision instead of two independent ones.
  if (!set || !set->wholeTakeViewsReserved() || !baseline.uneditedBaseline(sourceIdentity) ||
      !coverage.isValid() ||
      coverage.startPhase != 0 || coverage.lengthTicks != coverage.loopLengthTicks) return false;
  auto& current = set->views[set->lifecycle.current];
  if (!current.storage.isPrewarmed() || !current.passOrigins || baseline.size() > current.storage.capacity)
    return false;
  if (current.published && current.complete && current.source == sourceIdentity &&
      current.passOriginsReady && current.passOriginSource == sourceIdentity &&
      current.coverage.startPhase == 0 && current.coverage.lengthTicks == coverage.lengthTicks &&
      current.coverage.loopLengthTicks == coverage.loopLengthTicks && current.ledgerBoundary.matches(ledger))
    return true;
  // STOPPED-only throughput: copy provenance with its events before ordering.
  // A flat cached view must be rebuilt to acquire this typed receipt.
  current.resetMetadata();
  const EmissionCursor anchor{0, 0, 0, 0, sourceIdentity.sourceGeneration};
  RuntimePlaybackViewFill fill;
  LedgerBoundarySummary boundary; boundary.capture(ledger);
  if (!fill.begin(current, baseline, sourceIdentity, coverage, anchor, boundary)) return false;
  auto copied = RuntimePlaybackViewFillResult::Pending;
  while (copied == RuntimePlaybackViewFillResult::Pending) copied = fill.step(32);
  if (copied != RuntimePlaybackViewFillResult::Complete) { current.resetMetadata(); return false; }
  RuntimePlaybackOrderBuilder order;
  if (!order.begin(current, coverage.loopLengthTicks)) { current.resetMetadata(); return false; }
  auto ordered = RuntimePlaybackOrderBuildResult::Pending;
  while (ordered == RuntimePlaybackOrderBuildResult::Pending) ordered = order.step(32);
  if (ordered != RuntimePlaybackOrderBuildResult::Complete) { current.resetMetadata(); return false; }
  current.published = true;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::publishStoppedCanonicalOracleView(
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const RuntimeCanonicalPassInputs::CurrentSource& live, const MidiEvent* oracle,
    size_t oracleCount, const PlaybackCoverageInterval& coverage,
    const OverdubOverlayLane::CandidateRetention& overlayRetention,
    OverdubOverlayLane::OracleRebuildDecision* overlayDecisionOut) {
  using OverdubOverlayLane::OracleRebuildDecision;
  if (overlayDecisionOut != nullptr) *overlayDecisionOut = OracleRebuildDecision::Allowed;
  // RC-MEM-2: same explicit refusal as `publishStoppedOracleView` -- a slot with no
  // whole-take reservation has no authoritative full-take representation to publish into.
  const auto* wholeTakeSet = viewSetIfPrewarmed();
  if (!wholeTakeSet || !wholeTakeSet->wholeTakeViewsReserved() || !coverage.isValid() ||
      coverage.startPhase != 0 ||
      coverage.lengthTicks != coverage.loopLengthTicks || live.loopLengthTicks != coverage.loopLengthTicks ||
      oracleCount > RuntimePlaybackViewConfig::kEventsPerView || (oracleCount && !oracle)) return false;
  // 3.15e/2b-ii-b step 2: **the borrow's collision rule, consulted before any companion build.** A retained overlay
  // candidate owns the borrowed companion storage by identity. The check comes before the idle/admission terms on
  // purpose: a refusal or a retirement must be reported by name even when another of those terms is also false, and a
  // stale candidate must be retired *here* -- never left for the next build to reinterpret those bytes as oracle output.
  if (overlayRetention.overlayCandidateRetained) {
    const auto decision = OverdubOverlayLane::oracleRebuildDecision(overlayRetention);
    if (overlayDecisionOut != nullptr) *overlayDecisionOut = decision;
    if (decision == OracleRebuildDecision::RefusedRetainedCandidate) return false;
    if (!retireOverdubOverlayCandidate()) return false;
  }
  if (canonicalPrefixPhase != CanonicalPrefixPhase::Idle || canonicalTailPhase != CanonicalTailPhase::Idle ||
      captureBridgePhase != CaptureBridgePhase::Idle || !canonicalPassInputs.idle() ||
      !frozenCompanionPasses.idle() || !frozenCompanionApply.available()) return false;
  RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
      frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
  if (!canonicalPassInputs.beginCurrent(frozenCompanionApply, work, passes, source, live)) return false;
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  while (result == RuntimePlaybackOrderBuildResult::Pending) result = canonicalPassInputs.step(32);
  bool published = false;
  if (result == RuntimePlaybackOrderBuildResult::Complete &&
      frozenCompanionPasses.begin(frozenCompanionApply, work, canonicalPassInputs, nullptr, 0,
                                  source, coverage.loopLengthTicks)) {
    result = RuntimePlaybackOrderBuildResult::Pending;
    while (result == RuntimePlaybackOrderBuildResult::Pending) result = frozenCompanionPasses.step(32);
    bool matches = result == RuntimePlaybackOrderBuildResult::Complete && frozenCompanionPasses.size() == oracleCount;
    for (size_t i = 0; matches && i < oracleCount; ++i)
      matches = RuntimeCompanionBridgeMap::inputEventsMatch(frozenCompanionPasses.data()[i], oracle[i]);
    if (matches) published = publishStoppedOracleView(frozenCompanionPasses, source, coverage);
  }
  // These workers were idle at admission and are owned by this synchronous call.
  frozenCompanionPasses.discard(); canonicalPassInputs.discard();
  return published;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView* LoopPlaybackRuntime::overdubOverlayCandidate() const {
  const auto* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.build >= set->views.size()) return nullptr;
  // The candidate's own metadata is the identity: the mixed provenance shape only the continuation writes. No marker,
  // no extra field, and a candidate that lost `complete` after a pass commit is still recognisable as stale.
  const auto& view = set->views[set->lifecycle.build];
  return OverdubOverlayLane::isOverlayCandidate(view) ? &view : nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::overdubOverlayBuildExclusive() const {
  // One builder per slot. Until 2b-iii's role arbitration lands, the overlay lane begins only when no other owner
  // holds that builder or its prerequisite workers; otherwise it **defers by name** instead of competing.
  if (viewSetIfPrewarmed() == nullptr) return false;
  return !pendingViewRequest_.active && !previewSourceActive && !viewBuild.coverageOwned() &&
         !ownsCoverageSource() && !completedPromisePendingJudgement && !bridgeWindowBuild.active() &&
         captureBridgePhase == CaptureBridgePhase::Idle && canonicalTailPhase == CanonicalTailPhase::Idle &&
         (canonicalWindowPhase == CanonicalWindowPhase::Idle ||
          canonicalWindowPhase == CanonicalWindowPhase::Ready ||
          canonicalWindowPhase == CanonicalWindowPhase::Denied);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM PlaybackDeadlineClock LoopPlaybackRuntime::currentDeadlineClockForAdoption()
    const {
  return PlaybackDeadlineClock{static_cast<uint64_t>(rolling.lastTick), rolling.epoch};
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::readyPositionAllowsAdoption(
    PlaybackDeadlineClock now) const {
  if (!readyPositionedLater || readyPositionedClockEpoch == 0) return true;
  return now.epoch == readyPositionedClockEpoch && now.tick >= readyPositionedDeadlineTick;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::overdubOverlayBorrowHeld() const {
  return !frozenCompanionPasses.idle() && captureBridgePhase == CaptureBridgePhase::Idle &&
         canonicalTailPhase == CanonicalTailPhase::Idle &&
         (canonicalWindowPhase == CanonicalWindowPhase::Idle ||
          canonicalWindowPhase == CanonicalWindowPhase::Ready ||
          canonicalWindowPhase == CanonicalWindowPhase::Denied);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::retireOverdubOverlayCandidate() {
  auto* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.build >= set->views.size()) return false;
  auto& view = set->views[set->lifecycle.build];
  if (!OverdubOverlayLane::isOverlayCandidate(view)) return false;
  // Detach on the **binding**, not on grain currency: a re-based capture makes `nextGrain()` deny (and clears the
  // view's flags), so a currency test would leave the stale builder attached to the candidate it is retiring. Preserve
  // the destination through the detach, then clear the view ourselves so no stale flags remain readable.
  if (viewBuild.boundToDestination(view)) viewBuild.discard(&view);
  view.resetMetadata();
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM OverdubOverlayLane::Turn LoopPlaybackRuntime::stepOverdubOverlayBuild(
    const OverdubOverlayLane::BuildRequest& request, Loop& loop, size_t fillQuantum, size_t orderQuantum) {
  using OverdubOverlayLane::Turn;
  auto* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.build >= set->views.size() || fillQuantum == 0 || orderQuantum == 0)
    return Turn::Deferred;
  auto& view = set->views[set->lifecycle.build];

  // (1) The overlay half is attached: this **is** the append-stable continuation of the one window.
  if (viewBuild.overlayContinuationActive()) {
    const auto grain = viewBuild.nextGrain();
    if (grain == RuntimePlaybackViewBuildGrain::Denied) {
      // A pass commit re-based the capture (or the generation moved): the candidate's identity is gone. Retire it here
      // so no consumer can read a complete-looking window, and never reinterpret the borrowed bytes as this window.
      (void)retireOverdubOverlayCandidate();
      return Turn::RetiredStale;
    }
    if (grain == RuntimePlaybackViewBuildGrain::Complete) {
      // The live tail grows: extend the same window by the appended rows only. The capture **generation** moving is the
      // append frontier, not a new pass identity — the builder's own `liveCaptureCurrent()` (folded into `nextGrain()`
      // above) is what reports a re-based capture, and a genuinely stale half has already been refused by then.
      if (loop.capture.store.size() > viewBuild.liveCaptureSourceExtent()) {
        const PlaybackViewSourceIdentity identity{request.baseSource.playbackRevision,
            request.baseSource.sourceGeneration, nullptr, PlaybackWindowOrigin::Capture,
            request.captureGeneration};
        if (!viewBuild.extendLiveCapture(identity)) {
          (void)retireOverdubOverlayCandidate();
          return Turn::RetiredStale;
        }
        const auto extended = viewBuild.nextGrain();
        if (extended == RuntimePlaybackViewBuildGrain::Denied) {
          (void)retireOverdubOverlayCandidate();
          return Turn::RetiredStale;
        }
        const auto stepped = extended == RuntimePlaybackViewBuildGrain::Order
            ? viewBuild.stepOrder(orderQuantum) : viewBuild.stepFill(fillQuantum);
        return stepped == RuntimePlaybackViewBuildGrain::Denied ? Turn::RetiredStale : Turn::OverlayBuilding;
      }
      return Turn::CandidateReady;
    }
    const auto stepped = grain == RuntimePlaybackViewBuildGrain::Fill
        ? viewBuild.stepFill(fillQuantum) : viewBuild.stepOrder(orderQuantum);
    if (stepped == RuntimePlaybackViewBuildGrain::Denied) {
      (void)retireOverdubOverlayCandidate();
      return Turn::RetiredStale;
    }
    return Turn::OverlayBuilding;
  }

  // Continue **the same build** from the append-stable live source, then release the borrow. `beginLiveOverlayContinuation`
  // nulls the fill's baseline pointer, so the companion storage is no longer referenced by the append half -- releasing it
  // here keeps a pass commit's canonical tail and the stopped oracle from waiting on this lane for the one instance.
  const auto attachOverlay = [&]() -> Turn {
    const auto live = RuntimePlaybackViewSource::fromLiveCapture(loop, true);
    const PlaybackViewSourceIdentity identity{request.baseSource.playbackRevision,
        request.baseSource.sourceGeneration, nullptr, PlaybackWindowOrigin::Capture, request.captureGeneration};
    if (live.context == nullptr || !viewBuild.beginLiveOverlayContinuation(live, identity)) {
      viewBuild.discard(&view);
      view.resetMetadata();
      frozenCompanionPasses.discard();
      return Turn::Denied;
    }
    frozenCompanionPasses.discard();
    return Turn::OverlayStarted;
  };

  // (1b) The base half is **complete**: this turn attaches the overlay half (the previous turn's last grain completed it,
  // and an explicit step here would be a second child in one turn).
  if (viewBuild.completedDestinationIs(view) && view.source == request.baseSource &&
      view.coverage.startPhase == request.coverage.startPhase &&
      view.coverage.lengthTicks == request.coverage.lengthTicks &&
      view.coverage.loopLengthTicks == request.coverage.loopLengthTicks)
    return attachOverlay();

  // (2) The base half is in flight in the one builder. A base that no longer describes this request is abandoned, never
  // continued under a new demand.
  if (viewBuild.active()) {
    if (!(view.source == request.baseSource) || view.coverage.startPhase != request.coverage.startPhase ||
        view.coverage.lengthTicks != request.coverage.lengthTicks ||
        view.coverage.loopLengthTicks != request.coverage.loopLengthTicks) {
      viewBuild.discard(&view);
      view.resetMetadata();
      frozenCompanionPasses.discard();
      return Turn::RetiredStale;
    }
    const auto grain = viewBuild.nextGrain();
    if (grain == RuntimePlaybackViewBuildGrain::Denied) {
      viewBuild.discard(&view);
      view.resetMetadata();
      frozenCompanionPasses.discard();
      return Turn::Denied;
    }
    if (grain != RuntimePlaybackViewBuildGrain::Complete) {
      const auto stepped = grain == RuntimePlaybackViewBuildGrain::Fill
          ? viewBuild.stepFill(fillQuantum) : viewBuild.stepOrder(orderQuantum);
      if (stepped == RuntimePlaybackViewBuildGrain::Denied) {
        viewBuild.discard(&view);
        view.resetMetadata();
        frozenCompanionPasses.discard();
        return Turn::Denied;
      }
      return stepped == RuntimePlaybackViewBuildGrain::Complete ? attachOverlay() : Turn::BaseBuilding;
    }
    return attachOverlay();
  }

  // (3) The borrowed companion fill is in flight (the caller established that this lane is the only owner).
  if (!frozenCompanionPasses.idle()) {
    if (frozenCompanionPasses.ready(request.baseSource)) {
      RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
          frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
      LedgerBoundarySummary boundary;
      boundary.capture(ledger);
      const EmissionCursor anchor{0, request.coverage.startPhase, 0, 0, request.baseSource.sourceGeneration};
      if (!viewBuild.begin(view, frozenCompanionPasses, request.baseSource, request.coverage, anchor, boundary,
                           RuntimePlaybackViewFillScope::PlaybackCoverage)) {
        frozenCompanionPasses.discard();
        return Turn::Denied;
      }
      return Turn::BaseStarted;
    }
    const auto result = frozenCompanionPasses.step(fillQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      frozenCompanionPasses.discard();
      return Turn::Denied;
    }
    return Turn::BaseBuilding;
  }

  // (4) Nothing is in flight: admit the borrow by name.
  if (!frozenCompanionApply.available()) return Turn::BaseFillRefused;
  if (!canonicalPassInputs.ready() || canonicalPassInputs.generation() == 0 ||
      !(canonicalPrefixSource == request.baseSource)) return Turn::WaitingCanonicalBase;
  if (canonicalPassInputs.captureAttached()) return Turn::Denied;
  // A retained candidate whose identity moved is retired before a new build; the caller does the same, and the local
  // guard keeps a direct call from building over a candidate it does not own.
  if (OverdubOverlayLane::isOverlayCandidate(view)) return Turn::Deferred;
  viewBuild.discard();
  view.resetMetadata();
  RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
      frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
  if (!frozenCompanionPasses.begin(frozenCompanionApply, work, canonicalPassInputs, nullptr, 0,
                                   request.baseSource, request.coverage.loopLengthTicks))
    return Turn::Denied;
  return Turn::BaseStarted;
}

const RuntimePlaybackView* LoopPlaybackRuntime::publishedFullLoopView(
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks) const {
  const RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.current == RuntimePlaybackViewLifecycle::kNoView) {
    return nullptr;
  }
  const RuntimePlaybackView& current = set->views[set->lifecycle.current];
  if (!current.published || !current.complete || !(current.source == sourceIdentity) ||
      current.coverage.startPhase != 0 || current.coverage.lengthTicks != loopLengthTicks ||
      current.coverage.loopLengthTicks != loopLengthTicks) {
    return nullptr;
  }
  return &current;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const FixedPlaybackBridgeStorage* LoopPlaybackRuntime::selectedFullLoopBridge(
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks) const {
  const RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || set->bridgeLifecycle.selected >= set->bridges.size()) return nullptr;
  const auto& bridge = set->bridges[set->bridgeLifecycle.selected];
  const auto* base = fullLoopBridgeBase(bridge, loopLengthTicks);
  if (!bridge.selected || !bridge.complete || !bridge.streamPrepared ||
      !(bridge.source == sourceIdentity) ||
      bridge.coverage.loopLengthTicks != loopLengthTicks ||
      !base || base->storageGeneration != bridge.baseStorageGeneration) return nullptr;
  return &bridge;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView* LoopPlaybackRuntime::fullLoopBridgeBase(
    const FixedPlaybackBridgeStorage& bridge, uint32_t loopLengthTicks) const {
  const auto* set = viewSetIfPrewarmed();
  if (set == nullptr || !loopLengthTicks) return nullptr;
  for (const auto& base : set->bridgeBases) {
    if (base.published && base.complete && base.source == bridge.baseSource &&
        base.coverage.loopLengthTicks == loopLengthTicks &&
        base.storageGeneration == bridge.baseStorageGeneration &&
        base.coverage.startPhase == bridge.coverage.startPhase &&
        base.coverage.lengthTicks == bridge.coverage.lengthTicks) return &base;
  }
  // A canonical preparation view is published and immutable, but deliberately
  // remains outside lifecycle.current until its bridge is atomically selected.
  // Locate the exact view generation captured by the bridge rather than
  // requiring the unrelated audible-current slot.
  for (const auto& view : set->views) {
    if (view.published && view.complete && view.sourceFilled && view.passOriginsReady &&
        view.source == bridge.baseSource &&
        view.coverage.startPhase == bridge.coverage.startPhase &&
        view.coverage.lengthTicks == bridge.coverage.lengthTicks &&
        view.coverage.loopLengthTicks == loopLengthTicks &&
        view.storageGeneration == bridge.baseStorageGeneration) {
      return &view;
    }
  }
  // Legacy/full-loop bridge preparation predates pass-origin coverage. Keep
  // its current-view proof intact while canonical-window bridges use the scan
  // above.
  const auto* current = publishedFullLoopView(bridge.baseSource, loopLengthTicks);
  return current != nullptr && current->storageGeneration == bridge.baseStorageGeneration
      ? current : nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canSelectPreparedFullLoopBridge(
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
    const EmissionCursor& earliestAffectedCursor) const {
  const RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (!loopLengthTicks || earliestAffectedCursor.phase >= loopLengthTicks ||
      set == nullptr || set->bridgeLifecycle.candidate >= set->bridges.size()) return false;
  auto& bridge = set->bridges[set->bridgeLifecycle.candidate];
  const auto* base = fullLoopBridgeBase(bridge, loopLengthTicks);
  if (!bridge.complete || !bridge.streamPrepared || bridge.selected ||
      !(bridge.source == sourceIdentity) ||
      bridge.coverage.loopLengthTicks != loopLengthTicks ||
      !base || base->storageGeneration != bridge.baseStorageGeneration ||
      !(canonicalCaptureBridge ? bridge.matchesAffectedLedger(ledger) : bridge.ledgerBoundary.matches(ledger)) ||
      (canonicalCaptureBridge && earliestAffectedCursor.projectionCycle != bridge.cursorAnchor.projectionCycle) ||
      earliestAffectedCursor.effectiveSourceGeneration != sourceIdentity.sourceGeneration ||
      (bridge.protectsOpenNotes && (!bridge.hasRetainedClosure ||
       earliestAffectedCursor.phase >= bridge.retainedClosurePhaseLimit ||
       earliestAffectedCursor.phase < bridge.preparedAdmissionPhase)) ||
      !bridge.coverage.contains(earliestAffectedCursor.phase)) return false;
  // A bridge over ready coverage publishes that view in the same atomic turn.
  // Preflight the existing lifecycle handoff before canonical state can change.
  if (set->lifecycle.ready < set->views.size() && base == &set->views[set->lifecycle.ready] &&
      (set->lifecycle.current >= set->views.size() || set->lifecycle.current == set->lifecycle.ready))
    return false;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::selectPreparedFullLoopBridge(
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
    const EmissionCursor& earliestAffectedCursor) {
  if (!canSelectPreparedFullLoopBridge(sourceIdentity, loopLengthTicks, earliestAffectedCursor)) return false;
  publishPreflightedFullLoopBridge(earliestAffectedCursor);
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::publishPreflightedFullLoopBridge(
    const EmissionCursor& earliestAffectedCursor) {
  auto* set = viewSetIfPrewarmed();
  auto& bridge = set->bridges[set->bridgeLifecycle.candidate];
  const auto* base = fullLoopBridgeBase(bridge, bridge.coverage.loopLengthTicks);
  if (set->lifecycle.ready < set->views.size() && base == &set->views[set->lifecycle.ready])
    if (readyPositionAllowsAdoption(currentDeadlineClockForAdoption()))
      (void)set->handoffReadyCoverage(PlaybackViewHandoffMode::Preparation);
  // Prepared cycle indexes are relative to admission. The phase checks above
  // retain the prepared closure classification even if construction crossed
  // a wrap; the live ledger must still match before changing this anchor.
  bridge.cursorAnchor = earliestAffectedCursor;
  set->bridgeLifecycle.publishCandidate(set->bridges);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginFrozenCaptureBridge(
    const PlaybackViewSourceIdentity& baseSource, const PlaybackViewSourceIdentity& targetSource,
    uint32_t frozenGeneration, uint32_t commandEpoch, uint32_t loopLengthTicks,
    const EmissionCursor& anchor, const PlaybackBridgeRecord* companions, size_t companionCount) {
  auto* set = viewSetIfPrewarmed();
  const auto* base = publishedFullLoopView(baseSource, loopLengthTicks);
  if (captureBridgePhase != CaptureBridgePhase::Idle || set == nullptr || base == nullptr ||
      !frozenCaptureCandidateReady(frozenGeneration) || frozenCaptureOrderBinding.loopLengthTicks != loopLengthTicks ||
      anchor.phase >= loopLengthTicks || anchor.effectiveSourceGeneration != targetSource.sourceGeneration ||
      set->bridgeLifecycle.candidate >= set->bridges.size()) return false;
  // The selected bridge remains audible until this candidate is atomically
  // published.  A later overdub must therefore claim the alternate buffer,
  // rather than rejecting the normal selected-bridge state.
  if (!set->beginCandidateBridge()) return false;
  auto& bridge = set->bridges[set->bridgeLifecycle.candidate];
  if (bridge.selected || bridge.complete || bridge.recordCount != 0) return false;
  // A second selected source needs coalescing, not an insertion-only rebuild
  // over its older immutable base. Leave it intact until that owner is ready.
  if (companionCount > bridge.capacity ||
      set->frozenCaptureStaging().storage.eventCount > bridge.capacity - companionCount ||
      (companionCount != 0 && companions == nullptr)) return false;
  bridge.source = targetSource;
  bridge.coverage = base->coverage;
  bridge.cursorAnchor = anchor;
  bridge.ledgerBoundary.capture(ledger);
  if (!frozenCaptureBridgeFill.begin(set->frozenCaptureStaging(), frozenGeneration, companions, companionCount, bridge))
    return false;
  frozenCaptureSourceAdmission = {frozenGeneration, commandEpoch, baseSource, targetSource,
                                   set->bridgeLifecycle.candidate, false};
  captureBridgeBase = base;
  captureBridgePhase = CaptureBridgePhase::Fill;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canonicalPrefixNeedsService(
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source, uint32_t loopLength,
    const uint64_t& storageGeneration) const {
  if (!viewSetIfPrewarmed() || captureBridgePhase != CaptureBridgePhase::Idle || canonicalPassInputs.captureAttached()) return false;
  if (canonicalPrefixPhase == CanonicalPrefixPhase::Idle)
    return (canonicalPassInputs.idle() || canonicalPrefixRetainedHistory) && frozenCompanionApply.available();
  if (canonicalPassInputs.generation() != canonicalPrefixGeneration) return false;
  return canonicalPrefixPasses != &passes || !(canonicalPrefixSource == source) ||
      canonicalPrefixLength != loopLength || canonicalPrefixStorageOwner != &storageGeneration ||
      storageGeneration != canonicalPrefixStorageGeneration ||
      canonicalPrefixPhase == CanonicalPrefixPhase::Preparing;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::certifyCanonicalPrefixHistory(
    const LoopPasses& passes) {
  canonicalPrefixCertifiedPasses = static_cast<uint32_t>(passes.capturePassCount());
  canonicalPrefixCertifiedLastPass =
      passes.overdubPasses.empty()
          ? static_cast<uint32_t>(passes.recordPass.id)
          : static_cast<uint32_t>(passes.overdubPasses.back().id);
  canonicalPrefixCertified = canonicalPrefixCertifiedPasses != 0;
}

/// The certified shape plus exactly one appended pass: the inventory gained one row and every earlier row is still the
/// one that was certified (its id sits at its old position). Undo/redo/replacement move the id at that position, and a
/// same-count change never passes either -- so this is an *append*, not "the revision moved".
TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canonicalPrefixHistoryIsCertifiedAppend(
    const LoopPasses& passes) const {
  if (!canonicalPrefixCertified || canonicalPrefixCertifiedPasses == 0) return false;
  const size_t count = passes.capturePassCount();
  if (count != canonicalPrefixCertifiedPasses && count != canonicalPrefixCertifiedPasses + 1u) return false;
  const size_t lastCertifiedIndex = canonicalPrefixCertifiedPasses - 1u;
  const PassId idAtLastCertified =
      passes.hasRecordPass()
          ? (lastCertifiedIndex == 0 ? passes.recordPass.id
                                     : (lastCertifiedIndex - 1 < passes.overdubPasses.size()
                                            ? passes.overdubPasses[lastCertifiedIndex - 1].id
                                            : kInvalidPassId))
          : (lastCertifiedIndex < passes.overdubPasses.size()
                 ? passes.overdubPasses[lastCertifiedIndex].id
                 : kInvalidPassId);
  if (static_cast<uint32_t>(idAtLastCertified) != canonicalPrefixCertifiedLastPass) return false;
  // Every certified row must still be active, and the appended row too: an undo/redo (or a disabled append) is a
  // non-append history change and must never license a resume. Pass rows are append-only -- a slot replacement resets
  // the runtime -- so the row state plus the last-id check covers every reachable mutation.
  size_t index = 0;
  if (passes.hasRecordPass()) {
    if (passes.recordPass.state != CapturePassState::Active) return false;
    ++index;
  }
  for (const auto& row : passes.overdubPasses) {
    if (row.state != CapturePassState::Active) return false;
    ++index;
  }
  return index == count;
}

namespace {
// Report-only, RAM2: counted here as well as per runtime, because a slot's runtime object can be released and
// re-admitted under idle; the fixture and a capture need a value that survives that. A plain static here is DTCM
// (`variables`), which the RAM1 budget refuses.
#if defined(__IMXRT1062__)
DMAMEM uint32_t sCanonicalPrefixAppendResumes;
#else
uint32_t sCanonicalPrefixAppendResumes;  // host: the harness includes this TU without Arduino's DMAMEM
#endif
}  // namespace

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint32_t canonicalPrefixAppendResumeTotal() {
  return sCanonicalPrefixAppendResumes;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canRetainCanonicalHistoryPrefix() const {
  return canonicalPrefixCertified && canonicalPrefixPasses != nullptr &&
         !canonicalPassInputs.captureAttached() && canonicalPassInputs.stateAllowsBinding();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::retainCanonicalHistoryPrefix() {
  if (!canRetainCanonicalHistoryPrefix()) return false;
  // Inputs, their normalized output and the certification stay resident. The phase returns to `Idle` on purpose: the
  // next stop must request the prefix service and adopt this history from its own begin. Leaving a completed build in
  // `Ready` instead blocks that begin (measured: the stop FIFO stops retiring).
  canonicalPrefixRetainedHistory = true;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardRetainedCanonicalHistoryPrefix() {
  canonicalPrefixRetainedHistory = false;
  canonicalPrefixCertified = false;
  canonicalPrefixCertifiedPasses = 0;
  canonicalPrefixCertifiedLastPass = 0;
  canonicalPassInputs.discard();
  canonicalPrefixPhase = CanonicalPrefixPhase::Idle;
  canonicalPrefixPasses = nullptr;
  canonicalPrefixGeneration = 0;
  canonicalPrefixLength = 0;
  canonicalPrefixStorageOwner = nullptr;
  canonicalPrefixStorageGeneration = 0;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult LoopPlaybackRuntime::stepCanonicalPrefix(
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const RuntimeCanonicalPassInputs::CurrentSource& live, size_t quantum) {
  // DEC-170: name the first false term (report-only). `prefixFirstDenialReason` remembers the original transition into a
  // stall until the build restarts or completes, so the device row names what *first* blocked the base, not the sticky
  // `Denied` latch it left behind.
  const auto deny = [&](CanonicalPrefixDenial reason) {
    if (prefixFirstDenialReason == CanonicalPrefixDenial::None) prefixFirstDenialReason = reason;
    return RuntimePlaybackOrderBuildResult::Denied;
  };
  if (!quantum || !viewSetIfPrewarmed())
    return deny(CanonicalPrefixDenial::None);
  if (captureBridgePhase != CaptureBridgePhase::Idle) return deny(CanonicalPrefixDenial::BridgeActive);
  if (canonicalPassInputs.captureAttached()) return deny(CanonicalPrefixDenial::CaptureAttached);
  if (source.playbackRevision != live.playbackRevision) return deny(CanonicalPrefixDenial::RevisionMismatch);
  if (source.sourceGeneration != live.sourceGeneration) return deny(CanonicalPrefixDenial::GenerationMismatch);
  if (canonicalPrefixPhase != CanonicalPrefixPhase::Idle) {
    if (canonicalPassInputs.generation() != canonicalPrefixGeneration)
      return deny(CanonicalPrefixDenial::InputGenerationMismatch);
    if (canonicalPrefixPasses != &passes || !(canonicalPrefixSource == source) || canonicalPrefixLength != live.loopLengthTicks ||
        canonicalPrefixStorageOwner != &live.storageGeneration ||
        live.storageGeneration != canonicalPrefixStorageGeneration) {
      // Task 6.4c: a live successor moving the *window* identity must not replay the committed history. Resume is
      // licensed only by a certified append (one new pass on an unchanged inventory) or the same-revision snapshot
      // refresh; `resumeCurrentAfterAppend` re-proves the storage/owner identity, and the retained normalized output
      // (`count_`/`outputs_`) is kept, so the rest of the build costs one pass instead of the whole history.
      const bool sameRevisionRefresh =
          canonicalPrefixSource.playbackRevision == source.playbackRevision &&
          canonicalPrefixSource.snapshotIdentity != source.snapshotIdentity;
      // The window revision is not an identity (DEC-177): any newer revision qualifies once the history itself is a
      // certified append. Companion publication also bumps the revision, so `+1` could never match in a chain.
      const bool certifiedAppend = canonicalPrefixHistoryIsCertifiedAppend(passes);
      if ((sameRevisionRefresh || certifiedAppend) &&
          resumeCanonicalAfterAppend(passes, canonicalPrefixSource.playbackRevision, source, live,
                                     /*committedFrozenTail=*/false, certifiedAppend)) {
        ++canonicalPrefixAppendResumes; ++sCanonicalPrefixAppendResumes;
        return RuntimePlaybackOrderBuildResult::Pending;
      }
      // A superseded/stale source is a *restart*, not a denial: the `prefix_restart` stage row (below) already names it,
      // and the restart advances the build's identity, so it is never a `Denied` latch. Clear the denial record.
      SC_ODUB_STOP_STAGE("prefix_restart", 0, 0, UINT32_MAX, UINT32_MAX,
          canonicalPassInputs.completedPassCount(), source.playbackRevision,
          live.storageGeneration != canonicalPrefixStorageGeneration ? "history_storage_changed" : "source_changed");
      prefixFirstDenialReason = CanonicalPrefixDenial::None;
      prefixDeniedFirstReported = false;
      discardCanonicalWindow();
      canonicalPassInputs.discard(); canonicalPrefixPhase = CanonicalPrefixPhase::Idle;
      canonicalPrefixPasses = nullptr;
      return RuntimePlaybackOrderBuildResult::Pending; // Restart on a later owner turn.
    }
    if (canonicalPrefixPhase == CanonicalPrefixPhase::Denied) {
      // Sticky latch: keep reporting the original reason (already held in `prefixFirstDenialReason`).
      return RuntimePlaybackOrderBuildResult::Denied;
    }
    if (canonicalPrefixRetainedHistory) {
      // This build took the retained state over instead of replaying the history: the other half of the reuse.
      canonicalPrefixRetainedHistory = false;
      ++sCanonicalPrefixAppendResumes;
    }
    const auto result = canonicalPassInputs.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      canonicalPrefixPhase = CanonicalPrefixPhase::Ready;
      certifyCanonicalPrefixHistory(passes);
      prefixFirstDenialReason = CanonicalPrefixDenial::None;
      prefixDeniedFirstReported = false;
    } else if (result == RuntimePlaybackOrderBuildResult::Denied) {
      canonicalPrefixPhase = CanonicalPrefixPhase::Denied;
      return deny(CanonicalPrefixDenial::StepDenied);
    } else {
      // Progress: any earlier denial was transient, so a later denial is a fresh "first".
      prefixFirstDenialReason = CanonicalPrefixDenial::None;
      prefixDeniedFirstReported = false;
    }
    return result;
  }
  if (canonicalPrefixRetainedHistory) {
    if (canonicalPrefixHistoryIsCertifiedAppend(passes) &&
        resumeCanonicalAfterAppend(passes, canonicalPrefixSource.playbackRevision, source, live,
                                   /*committedFrozenTail=*/false, /*certifiedHistoryAppend=*/true)) {
      ++canonicalPrefixAppendResumes;
      canonicalPrefixRetainedHistory = false;
      return RuntimePlaybackOrderBuildResult::Pending;
    }
    // Not a certified append: the retained prefix has no authority here and is gone before anything is built.
    discardRetainedCanonicalHistoryPrefix();
  }
  if (!canonicalPassInputs.idle()) return deny(CanonicalPrefixDenial::InputsBusy);
  if (!frozenCompanionApply.available()) return deny(CanonicalPrefixDenial::ApplyUnavailable);
  canonicalPrefixSource = source; canonicalPrefixPasses = &passes; canonicalPrefixLength = live.loopLengthTicks;
  canonicalPrefixStorageOwner = &live.storageGeneration;
  canonicalPrefixStorageGeneration = live.storageGeneration;
  RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
    frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
  const bool started = canonicalPassInputs.beginCurrent(frozenCompanionApply, work, passes, canonicalPrefixSource, live);
  canonicalPrefixGeneration = canonicalPassInputs.generation();
  canonicalPrefixPhase = started ? CanonicalPrefixPhase::Preparing : CanonicalPrefixPhase::Denied;
  if (started) certifyCanonicalPrefixHistory(passes);  // a cold build certifies what it is about to replay
  prefixFirstDenialReason = CanonicalPrefixDenial::None;  // a new build starts; its first denial overwrites
  prefixDeniedFirstReported = false;
  return started ? RuntimePlaybackOrderBuildResult::Pending : deny(CanonicalPrefixDenial::StepDenied);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::retainCommittedCanonicalTail(
    Loop& loop, const PlaybackViewSourceIdentity& source) {
  const auto* owner = canonicalPassInputs.currentSourceOwner();
  if (!owner || canonicalTailPhase != CanonicalTailPhase::Complete ||
      canonicalTailPassGeneration != frozenCompanionPasses.generation() ||
      canonicalTailRowGeneration != frozenCaptureCompanions.preparationGeneration()) return false;
  const auto live = *owner;
  const auto previous = canonicalPrefixSource;
  canonicalPrefixSource = source;
  if (!canonicalPassInputs.adoptCommittedNormalization(loop.passes, canonicalPrefixSource,
                                                      live, frozenCompanionPasses)) {
    canonicalPrefixSource = previous; return false;
  }
  canonicalPrefixGeneration = canonicalPassInputs.generation();
  canonicalPrefixStorageGeneration = live.storageGeneration;
  canonicalPrefixPhase = CanonicalPrefixPhase::Preparing;
  certifyCanonicalPrefixHistory(loop.passes);
  canonicalPrefixSource = source;
  canonicalPrefixPasses = &loop.passes;
  // DEC-177: this prefix now represents committed history, capture-free, with no window or companion ownership. It is
  // exactly the retained-history-prefix state; the job's cleanup keeps it for the next queued stop. Counted as one
  // history reuse: the next stop's build no longer replays the committed passes.
  canonicalPrefixRetainedHistory = true;
  ++sCanonicalPrefixAppendResumes;
  SC_ODUB_STOP_STAGE("prefix_reuse", 0, 0, UINT32_MAX, UINT32_MAX,
      canonicalPassInputs.completedPassCount(), source.playbackRevision, "committed_normalization");
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::resumeCanonicalAfterAppend(
    const LoopPasses& passes, uint32_t priorRevision,
    const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
    bool committedFrozenTail, bool certifiedHistoryAppend) {
  // The token address remains stable for the borrowed worker. Failed reuse
  // leaves normal source invalidation responsible for a bounded restart.
  const auto previous = canonicalPrefixSource;
  canonicalPrefixSource = source;
  if (!canonicalPassInputs.resumeCurrentAfterAppend(passes, priorRevision,
          canonicalPrefixSource, live, committedFrozenTail, certifiedHistoryAppend)) {
    canonicalPrefixSource = previous;
    return false;
  }
  discardCanonicalWindow();
  canonicalPrefixGeneration = canonicalPassInputs.generation();
  canonicalPrefixPhase = CanonicalPrefixPhase::Preparing;
  canonicalPrefixPasses = &passes; canonicalPrefixLength = live.loopLengthTicks;
  canonicalPrefixStorageOwner = &live.storageGeneration;
  canonicalPrefixStorageGeneration = live.storageGeneration;
  certifyCanonicalPrefixHistory(passes);
  SC_ODUB_STOP_STAGE("prefix_reuse", 0, 0, UINT32_MAX, UINT32_MAX,
      canonicalPassInputs.completedPassCount(), source.playbackRevision,
      committedFrozenTail ? "committed_tail" : "source_append_or_snapshot");
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint64_t LoopPlaybackRuntime::requiredStopRankedGrains(
    const PlaybackViewSourceIdentity& source, size_t maximumPassCount,
    size_t historyCount, size_t pendingChangeCount, size_t quantum) const {
  const auto* set = viewSetIfPrewarmed();
  if (!set) return UINT64_MAX;
  const size_t capacity = set->frozenCaptureStaging().storage.capacity;
  const auto* base = preparedCanonicalWindow(source);
  return StopAdmissionContract::requiredPreparedRankedTailGrainsForPasses(
      capacity, maximumPassCount, base ? base->storage.eventCount : capacity,
      historyCount, pendingChangeCount, quantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult LoopPlaybackRuntime::stepCanonicalBase(
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const RuntimeCanonicalPassInputs::CurrentSource& live, const EmissionCursor& anchor, size_t quantum) {
  const bool prefixWasReady = canonicalPrefixPhase == CanonicalPrefixPhase::Ready;
  const auto result = stepCanonicalPrefix(passes, source, live, quantum);
  if (result != RuntimePlaybackOrderBuildResult::Complete) return result;
  // A ready prefix still validates its owners above, but consumes no input.
  // A prefix completed on this call yields before starting the next child.
  if (!prefixWasReady) return RuntimePlaybackOrderBuildResult::Pending;
  return stepCanonicalWindow({0, live.loopLengthTicks, live.loopLengthTicks}, anchor, quantum, true);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView* LoopPlaybackRuntime::preparedCanonicalWindow(
    const PlaybackViewSourceIdentity& source) const {
  const auto* set = viewSetIfPrewarmed();
  if (!set || canonicalWindowPhase != CanonicalWindowPhase::Ready ||
      canonicalWindowIndex >= (canonicalWindowIsBridgeBase ? set->bridgeBases.size() : set->views.size()) || !(source == canonicalPrefixSource) ||
      !canonicalPrefixStorageOwner || *canonicalPrefixStorageOwner != canonicalPrefixStorageGeneration)
    return nullptr;
  const auto& view = canonicalWindowIsBridgeBase ? set->bridgeBases[canonicalWindowIndex]
                                                : set->views[canonicalWindowIndex];
  return view.published && view.complete && view.sourceFilled && view.passOriginsReady &&
      view.source == source && view.passOriginSource == source &&
      view.coverage.startPhase == canonicalWindowCoverage.startPhase &&
      view.coverage.lengthTicks == canonicalWindowCoverage.lengthTicks &&
      view.coverage.loopLengthTicks == canonicalWindowCoverage.loopLengthTicks ? &view : nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardCanonicalWindow() {
  if (canonicalWindowPhase == CanonicalWindowPhase::Idle) return;
  auto* set = viewSetIfPrewarmed();
  if (canonicalWindowPhase != CanonicalWindowPhase::Ready) viewBuild.discard();
  if (canonicalWindowPhase != CanonicalWindowPhase::Ready &&
      frozenCompanionPasses.generation() == canonicalWindowPassGeneration)
    frozenCompanionPasses.discard();
  if (!canonicalWindowIsBridgeBase && set && canonicalWindowIndex < set->views.size() &&
      canonicalWindowIndex != set->lifecycle.current && canonicalWindowIndex != set->lifecycle.retained)
    set->views[canonicalWindowIndex].resetMetadata();
  canonicalWindowPhase = CanonicalWindowPhase::Idle;
  canonicalWindowIsBridgeBase = false;
  canonicalWindowIndex = RuntimePlaybackViewLifecycle::kNoView;
  canonicalWindowPassGeneration = 0;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::markDeferredConvergenceOwed(
    uint32_t targetRevision, uint32_t loopLengthTicks) {
  // A new obligation never inherits a candidate: the generation it was building for is not the one
  // this retire produced. `Ready` re-opens because the newest generation is not represented yet.
  if (deferredConvergencePhase == DeferredConvergencePhase::Building) {
    discardCanonicalBuildCandidate();
    deferredConvergenceDenial = DeferredConvergenceDenial::SourceChanged;
  }
  deferredConvergencePhase = DeferredConvergencePhase::Owed;
  // Coalescing, not stacking: the obligation always targets the newest committed generation.
  if (targetRevision > deferredConvergenceTarget) deferredConvergenceTarget = targetRevision;
  if (loopLengthTicks != 0) deferredConvergenceLength = loopLengthTicks;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::coalesceDeferredConvergence(
    uint32_t newestCommittedRevision) {
  if (deferredConvergencePhase == DeferredConvergencePhase::Idle) return false;
  if (newestCommittedRevision <= deferredConvergenceTarget) return false;
  if (deferredConvergencePhase == DeferredConvergencePhase::Building) {
    discardCanonicalBuildCandidate();
    deferredConvergenceDenial = DeferredConvergenceDenial::SourceChanged;
  }
  deferredConvergenceTarget = newestCommittedRevision;
  // `Ready` on an older target is still a valid *derived* artifact (its own source identity names the
  // generation it represents); it is kept, and the newer build starts once the ready role frees.
  if (deferredConvergencePhase != DeferredConvergencePhase::Ready)
    deferredConvergencePhase = DeferredConvergencePhase::Owed;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::satisfyDeferredConvergence(
    uint32_t committedRevision) {
  if (deferredConvergencePhase == DeferredConvergencePhase::Idle) return false;
  if (committedRevision < deferredConvergenceTarget) return false;
  // The inline build owns the canonical machinery and produced the artifact that covers the target;
  // there is no separate candidate to drop here.
  cancelDeferredConvergence();
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::cancelDeferredConvergence() {
  deferredConvergencePhase = DeferredConvergencePhase::Idle;
  deferredConvergenceDenial = DeferredConvergenceDenial::None;
  deferredConvergenceTarget = 0;
  deferredConvergenceLength = 0;
  deferredConvergenceSource = PlaybackViewSourceIdentity{};
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardCanonicalBuildCandidate() {
  discardCanonicalWindow();
  canonicalPassInputs.discard();
  // The prefix's apply binding is the shared companion worker: discarding only the inputs leaves the
  // worker mid-history, and a later build then reports `Preparing` forever (measured by the
  // second-obligation/preemption fixtures). The caller owns these builders by contract.
  frozenCompanionApply.discard();
  frozenCompanionPasses.discard();
  canonicalPrefixPhase = CanonicalPrefixPhase::Idle;
  canonicalPrefixPasses = nullptr;
  canonicalPrefixGeneration = 0;
  canonicalPrefixLength = 0;
  canonicalPrefixStorageOwner = nullptr;
  canonicalPrefixStorageGeneration = 0;
  prefixFirstDenialReason = CanonicalPrefixDenial::None;
  prefixDeniedFirstReported = false;
  deferredConvergenceSource = PlaybackViewSourceIdentity{};
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult LoopPlaybackRuntime::stepCanonicalWindow(
    const PlaybackCoverageInterval& coverage, const EmissionCursor& anchor, size_t quantum, bool bridgeBase) {
  auto* set = viewSetIfPrewarmed();
  if (!quantum || !set || !coverage.isValid() || coverage.loopLengthTicks != canonicalPrefixLength ||
      anchor.phase >= coverage.loopLengthTicks ||
      anchor.effectiveSourceGeneration != canonicalPrefixSource.sourceGeneration)
    return RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalWindowPhase == CanonicalWindowPhase::Ready)
    return preparedCanonicalWindow(canonicalPrefixSource) &&
        coverage.startPhase == canonicalWindowCoverage.startPhase &&
        coverage.lengthTicks == canonicalWindowCoverage.lengthTicks
        ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalWindowPhase == CanonicalWindowPhase::Denied ||
      canonicalPrefixPhase != CanonicalPrefixPhase::Ready || !canonicalPassInputs.ready() ||
      canonicalPassInputs.generation() != canonicalPrefixGeneration || canonicalPassInputs.captureAttached())
    return RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalWindowPhase == CanonicalWindowPhase::Idle) {
    // Wait for the existing view owner; never steal an in-flight or ready view.
    if (viewBuild.active() || !frozenCompanionPasses.idle() || !frozenCompanionApply.available())
      return RuntimePlaybackOrderBuildResult::Pending;
    const auto index = bridgeBase ? set->bridgeLifecycle.candidate :
        set->lifecycle.ready != RuntimePlaybackViewLifecycle::kNoView
        ? set->lifecycle.ready : set->lifecycle.build;
    if (bridgeBase && (index >= set->bridgeBases.size() || index == set->bridgeLifecycle.selected))
      return RuntimePlaybackOrderBuildResult::Denied;
    if (bridgeBase) set->bridgeBases[index].resetMetadata();
    if (index >= (bridgeBase ? set->bridgeBases.size() : set->views.size()) ||
        (!bridgeBase && (index == set->lifecycle.current || index == set->lifecycle.retained ||
         set->views[index].published || set->views[index].complete)))
      return RuntimePlaybackOrderBuildResult::Pending;
    // A completed frozen builder may still name the ready/current playback role. Starting the
    // canonical bridge-base owner ends only its builder borrow, not that published/owed window.
    const RuntimePlaybackView* preserve = nullptr;
    for (const uint8_t role : {set->lifecycle.current, set->lifecycle.ready}) {
      if (role < set->views.size() && set->views[role].complete &&
          viewBuild.completedDestinationIs(set->views[role])) preserve = &set->views[role];
    }
    viewBuild.discard(preserve);
    RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
      frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
    if (!frozenCompanionPasses.begin(frozenCompanionApply, work, canonicalPassInputs, nullptr, 0,
                                    canonicalPrefixSource, canonicalPrefixLength))
      return RuntimePlaybackOrderBuildResult::Denied;
    canonicalWindowIndex = index;
    canonicalWindowIsBridgeBase = bridgeBase;
    canonicalWindowPassGeneration = frozenCompanionPasses.generation();
    canonicalWindowCoverage = coverage;
    windowFirstDenialReason = CanonicalWindowDenial::None;  // a fresh window build starts clear
    windowDeniedFirstReported = false;
    canonicalWindowPhase = CanonicalWindowPhase::Merge;
    return RuntimePlaybackOrderBuildResult::Pending; // Setup is a separate turn.
  }
  if (bridgeBase != canonicalWindowIsBridgeBase ||
      coverage.startPhase != canonicalWindowCoverage.startPhase ||
      coverage.lengthTicks != canonicalWindowCoverage.lengthTicks ||
      frozenCompanionPasses.generation() != canonicalWindowPassGeneration ||
      canonicalWindowIndex >= set->views.size() ||
      (!canonicalWindowIsBridgeBase &&
       ((canonicalWindowIndex != set->lifecycle.ready && canonicalWindowIndex != set->lifecycle.build) ||
        canonicalWindowIndex == set->lifecycle.current || canonicalWindowIndex == set->lifecycle.retained)))
    return RuntimePlaybackOrderBuildResult::Denied;
  auto& view = canonicalWindowIsBridgeBase ? set->bridgeBases[canonicalWindowIndex]
                                          : set->views[canonicalWindowIndex];
  if (canonicalWindowPhase == CanonicalWindowPhase::Merge) {
    const auto result = frozenCompanionPasses.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      if (windowFirstDenialReason == CanonicalWindowDenial::None)
        windowFirstDenialReason = CanonicalWindowDenial::MergeDenied;
      canonicalWindowPhase = CanonicalWindowPhase::Denied;
      return result;
    }
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      LedgerBoundarySummary boundary; boundary.capture(ledger);
      if (!viewBuild.begin(view, frozenCompanionPasses, canonicalPrefixSource, coverage, anchor, boundary)) {
        if (windowFirstDenialReason == CanonicalWindowDenial::None)
          windowFirstDenialReason = CanonicalWindowDenial::FillBeginDenied;
        canonicalWindowPhase = CanonicalWindowPhase::Denied;
        return RuntimePlaybackOrderBuildResult::Denied;
      }
      canonicalWindowPhase = CanonicalWindowPhase::Fill;
    }
    return RuntimePlaybackOrderBuildResult::Pending;
  }
  if (canonicalWindowPhase == CanonicalWindowPhase::Publish) {
    if (!view.complete || !view.sourceFilled || !view.passOriginsReady)
      return RuntimePlaybackOrderBuildResult::Denied;
    if (!canonicalWindowIsBridgeBase && canonicalWindowIndex != set->lifecycle.ready &&
        !set->publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation))
      return RuntimePlaybackOrderBuildResult::Pending;
    view.published = true; // Immutable ready base; audible current is unchanged.
    viewBuild.discard();
    frozenCompanionPasses.discard(); // Owned events/origins now outlive worker reuse for the frozen tail.
    canonicalWindowPhase = CanonicalWindowPhase::Ready;
    return RuntimePlaybackOrderBuildResult::Complete;
  }
  const auto result = canonicalWindowPhase == CanonicalWindowPhase::Fill
      ? viewBuild.stepFill(quantum) : viewBuild.stepOrder(quantum);
  if (result == RuntimePlaybackViewBuildGrain::Denied) {
    if (windowFirstDenialReason == CanonicalWindowDenial::None)
      windowFirstDenialReason = CanonicalWindowDenial::FillOrderDenied;
    canonicalWindowPhase = CanonicalWindowPhase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  canonicalWindowPhase = result == RuntimePlaybackViewBuildGrain::Complete ? CanonicalWindowPhase::Publish
      : result == RuntimePlaybackViewBuildGrain::Order ? CanonicalWindowPhase::Order : CanonicalWindowPhase::Fill;
  return RuntimePlaybackOrderBuildResult::Pending;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginFrozenCanonicalTail(
    const RuntimeCanonicalPassInputs::FrozenSource& live, const PendingNoteChangeVec& changes,
    const PlaybackViewSourceIdentity& target) {
  const auto* set = viewSetIfPrewarmed();
  if (!set || &live.capture != &set->frozenCaptureStaging() || canonicalTailPhase != CanonicalTailPhase::Idle ||
      captureBridgePhase != CaptureBridgePhase::Idle || canonicalPrefixPhase != CanonicalPrefixPhase::Ready ||
      canonicalPassInputs.generation() != canonicalPrefixGeneration || !canonicalPassInputs.ready() ||
      canonicalPassInputs.captureAttached() || !frozenCaptureCompanions.idle() ||
      !frozenCompanionPasses.idle() || !frozenCompanionApply.available()) return false;
  if (!frozenCaptureCompanions.begin(live.capture, live.capture.frozenGeneration, changes)) return false;
  const bool reuseOrder = !frozenCaptureOrderBinding.bound &&
      frozenCaptureCanonicalOrderBinding.matches(live.capture, frozenCaptureOrderStorageGeneration) && frozenCaptureCanonicalOrder.complete();
  RuntimeCanonicalPassInputs::PreparedCaptureOrder preparedOrder{
      live.capture.storage.order, frozenCaptureCanonicalOrder.orderedEventCount(), frozenCaptureOrderStorageGeneration};
  if (!canonicalPassInputs.appendFrozenOverdub(live, reuseOrder ? &preparedOrder : nullptr)) {
    frozenCaptureCompanions.discard(); // Admission did not change the prefix.
    return false;
  }
  canonicalTailTarget = target;
  canonicalTailInputGeneration = canonicalPassInputs.generation();
  canonicalTailRowGeneration = frozenCaptureCompanions.preparationGeneration();
  canonicalTailPassGeneration = frozenCompanionPasses.generation();
  canonicalTailPhase = CanonicalTailPhase::Normalize;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult LoopPlaybackRuntime::stepFrozenCanonicalTail(size_t quantum) {
  if (!quantum || canonicalTailPhase == CanonicalTailPhase::Idle) return RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalTailPhase == CanonicalTailPhase::Denied ||
      canonicalPassInputs.generation() != canonicalTailInputGeneration ||
      frozenCaptureCompanions.preparationGeneration() != canonicalTailRowGeneration ||
      frozenCompanionPasses.generation() != canonicalTailPassGeneration || !frozenCaptureCompanions.sourceCurrent()) {
    canonicalTailPhase = CanonicalTailPhase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  if (canonicalTailPhase == CanonicalTailPhase::Normalize) {
    result = canonicalPassInputs.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Complete) canonicalTailPhase = CanonicalTailPhase::Rows;
  } else if (!canonicalPassInputs.ready()) {
    result = RuntimePlaybackOrderBuildResult::Denied;
  } else if (canonicalTailPhase == CanonicalTailPhase::Rows) {
    result = frozenCaptureCompanions.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Complete) canonicalTailPhase = CanonicalTailPhase::StartCompanions;
  } else if (canonicalTailPhase == CanonicalTailPhase::StartCompanions) {
    RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
      frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
    if (!frozenCompanionPasses.begin(frozenCompanionApply, work, canonicalPassInputs,
                                    frozenCaptureCompanions, canonicalTailTarget, canonicalPassInputs.loopLength())) {
      result = RuntimePlaybackOrderBuildResult::Denied;
    } else {
      canonicalTailPassGeneration = frozenCompanionPasses.generation();
      canonicalTailPhase = CanonicalTailPhase::Companions;
    }
  } else if (canonicalTailPhase == CanonicalTailPhase::Companions) {
    result = frozenCompanionPasses.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Complete) canonicalTailPhase = CanonicalTailPhase::Complete;
  } else {
    result = frozenCanonicalTailReady(canonicalTailTarget) ? RuntimePlaybackOrderBuildResult::Complete
                                                        : RuntimePlaybackOrderBuildResult::Denied;
  }
  if (result == RuntimePlaybackOrderBuildResult::Denied) canonicalTailPhase = CanonicalTailPhase::Denied;
  if (canonicalTailPhase == CanonicalTailPhase::Complete || result == RuntimePlaybackOrderBuildResult::Denied) return result;
  return RuntimePlaybackOrderBuildResult::Pending; // Never step the next child on a transition.
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult
LoopPlaybackRuntime::stepReservedFrozenCanonicalTail(PendingStopAdmission& stop, size_t quantum) {
  const auto* set = viewSetIfPrewarmed();
  if (!set || !stop.pending || !quantum || quantum != stop.rankedPreparationQuantum ||
      stop.remainingClosureGrains || stop.frozenGeneration != set->frozenCaptureStaging().frozenGeneration ||
      !canonicalPassInputs.frozenSourceCurrent(set->frozenCaptureStaging(), stop.transportCommandEpoch) ||
      canonicalTailPhase == CanonicalTailPhase::Idle || canonicalTailPhase == CanonicalTailPhase::Denied ||
      canonicalPassInputs.generation() != canonicalTailInputGeneration ||
      frozenCaptureCompanions.preparationGeneration() != canonicalTailRowGeneration ||
      frozenCompanionPasses.generation() != canonicalTailPassGeneration || !frozenCaptureCompanions.sourceCurrent())
    return RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalTailPhase == CanonicalTailPhase::Complete)
    return frozenCanonicalTailReady(canonicalTailTarget) ? RuntimePlaybackOrderBuildResult::Complete
                                                       : RuntimePlaybackOrderBuildResult::Denied;
  if (!stop.consumeRankedPreparationGrain()) return RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalTailPhase == CanonicalTailPhase::StartCompanions) {
    // Add changes contribute capture events, not companion edit passes. Bind
    // the commit revision only after the bounded row worker has counted the
    // actual companions, before any derived pass is published under that token.
    const uint32_t delta = frozenCaptureCompanions.size() == 0 ? 1u : 2u;
    if (canonicalPrefixSource.playbackRevision > UINT32_MAX - delta)
      return RuntimePlaybackOrderBuildResult::Denied;
    canonicalTailTarget.playbackRevision = canonicalPrefixSource.playbackRevision + delta;
  }
  return stepFrozenCanonicalTail(quantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult
LoopPlaybackRuntime::stepReservedFrozenCaptureBridge(PendingStopAdmission& stop, size_t quantum) {
  if (!stop.pending || !quantum || quantum != stop.rankedPreparationQuantum || stop.remainingClosureGrains ||
      !canonicalCaptureBridge || stop.frozenGeneration != frozenCaptureSourceAdmission.frozenGeneration ||
      stop.transportCommandEpoch != frozenCaptureSourceAdmission.transportCommandEpoch ||
      captureBridgePhase == CaptureBridgePhase::Idle || captureBridgePhase == CaptureBridgePhase::Denied ||
      !frozenBridgePreparationReady()) return RuntimePlaybackOrderBuildResult::Denied;
  if (captureBridgePhase == CaptureBridgePhase::Complete) return stepFrozenCaptureBridge(quantum);
  if (!stop.consumeRankedPreparationGrain()) return RuntimePlaybackOrderBuildResult::Denied;
  return stepFrozenCaptureBridge(quantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::frozenCanonicalTailReady(
    const PlaybackViewSourceIdentity& target) const {
  return canonicalTailPhase == CanonicalTailPhase::Complete && target == canonicalTailTarget &&
      canonicalPassInputs.generation() == canonicalTailInputGeneration &&
      frozenCaptureCompanions.preparationGeneration() == canonicalTailRowGeneration &&
      frozenCompanionPasses.generation() == canonicalTailPassGeneration &&
      frozenCompanionPasses.ready(target) &&
      frozenCompanionPasses.matchesOwners(canonicalPassInputs, frozenCaptureCompanions);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardFrozenCanonicalTail() {
  if (canonicalTailPhase == CanonicalTailPhase::Idle) return;
  if (frozenCompanionPasses.generation() == canonicalTailPassGeneration) frozenCompanionPasses.discard();
  if (!canonicalPrefixRetainedHistory && canonicalPassInputs.generation() == canonicalTailInputGeneration) {
    canonicalPassInputs.discard();
    canonicalPrefixPhase = CanonicalPrefixPhase::Idle; canonicalPrefixPasses = nullptr;
    canonicalPrefixGeneration = 0; canonicalPrefixLength = 0;
    canonicalPrefixStorageOwner = nullptr; canonicalPrefixStorageGeneration = 0;
  }
  if (frozenCaptureCompanions.preparationGeneration() == canonicalTailRowGeneration) frozenCaptureCompanions.discard();
  canonicalTailPhase = CanonicalTailPhase::Idle;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginFrozenCompanionBridge(
    const PlaybackViewSourceIdentity& baseSource, const PlaybackViewSourceIdentity& targetSource,
    uint32_t frozenGeneration, uint32_t commandEpoch, uint32_t loopLengthTicks,
    const EmissionCursor& anchor) {
  auto* set = viewSetIfPrewarmed();
  const auto* base = preparedCanonicalWindow(baseSource);
  if (!base) base = publishedFullLoopView(baseSource, loopLengthTicks);
  uint16_t failure = 0;
  failure |= captureBridgePhase != CaptureBridgePhase::Idle ? 0x0001u : 0u;
  failure |= !set ? 0x0002u : 0u;
  failure |= !base ? 0x0004u : 0u;
  failure |= base && (base->coverage.loopLengthTicks != loopLengthTicks ||
                      !base->coverage.contains(anchor.phase)) ? 0x0008u : 0u;
  failure |= !set || canonicalPassInputs.frozenCapture() != &set->frozenCaptureStaging() ? 0x0010u : 0u;
  failure |= !canonicalPassInputs.includesFrozenOverdub() ? 0x0020u : 0u;
  failure |= !canonicalPassInputs.matchesFrozenCommand(commandEpoch) ? 0x0040u : 0u;
  failure |= !set || set->frozenCaptureStaging().frozenGeneration != frozenGeneration ? 0x0080u : 0u;
  failure |= canonicalPassInputs.loopLength() != loopLengthTicks ? 0x0100u : 0u;
  failure |= !frozenCompanionPasses.matchesOwners(canonicalPassInputs, frozenCaptureCompanions) ? 0x0200u : 0u;
  failure |= !frozenCompanionPasses.ready(targetSource) ? 0x0400u : 0u;
  failure |= anchor.phase >= loopLengthTicks || anchor.effectiveSourceGeneration != targetSource.sourceGeneration ? 0x0800u : 0u;
  failure |= !set || set->bridgeLifecycle.candidate >= set->bridges.size() ? 0x1000u : 0u;
  const auto reportFailure = [&](uint16_t mask) {
    if (captureBridgeBeginFailureMask != 0) return;
    captureBridgeBeginFailureMask = static_cast<uint16_t>(mask | 0x8000u);
    SC_ODUB_STOP_STAGE("bridge_begin", 0, 0, UINT32_MAX, UINT32_MAX, mask,
        set ? (static_cast<size_t>(set->bridgeLifecycle.selected) << 8u) |
                  set->bridgeLifecycle.candidate : 0u,
        captureBridgePhase == CaptureBridgePhase::Complete ? "complete_not_selectable" :
        captureBridgePhase == CaptureBridgePhase::Denied ? "worker_denied" : "denied");
  };
  if (failure) {
    reportFailure(failure);
    return false;
  }
  // Keep the current bridge readable while building its replacement in the
  // lifecycle's alternate candidate buffer.
  if (!set->beginCandidateBridge()) {
    reportFailure(0x2000u);
    return false;
  }
  auto& bridge = set->bridges[set->bridgeLifecycle.candidate];
  if (bridge.selected || bridge.complete || bridge.recordCount) {
    reportFailure(0x4000u);
    return false;
  }
  if (!frozenCompanionBridgeMap.begin(frozenCompanionPasses, *base, targetSource)) {
    reportFailure(0x7fffu);
    return false;
  }
  captureBridgeBeginFailureMask = 0;
  bridge.source = targetSource; bridge.coverage = base->coverage; bridge.cursorAnchor = anchor;
  bridge.ledgerBoundary.capture(ledger);
  frozenCaptureSourceAdmission = {frozenGeneration, commandEpoch, baseSource, targetSource,
                                  set->bridgeLifecycle.candidate, false};
  captureBridgeBase = base;
  canonicalCaptureBridge = true;
  captureBridgePreparationGeneration = frozenCompanionPasses.generation();
  captureBridgeMapGeneration = frozenCompanionBridgeMap.generation();
  captureBridgePhase = CaptureBridgePhase::Map;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView* LoopPlaybackRuntime::frozenCaptureBridgeBase() const {
  const auto* set = viewSetIfPrewarmed();
  const auto& admission = frozenCaptureSourceAdmission;
  if (!set || !captureBridgeBase || admission.candidateIndex >= set->bridges.size()) return nullptr;
  // Accept only owned rolling views or the two retained bridge source bases.
  // Reject foreign storage and retired bases,
  // even when their source identity happens to equal the successor's identity.
  bool owned = false;
  for (const auto& view : set->views) owned = owned || &view == captureBridgeBase;
  for (const auto& view : set->bridgeBases) owned = owned || &view == captureBridgeBase;
  if (!owned) return nullptr;
  const auto& base = *captureBridgeBase;
  const auto& bridge = set->bridges[admission.candidateIndex];
  if (!base.published || !base.complete || !(base.source == admission.baseSource) ||
      !base.coverage.isValid() || base.coverage.startPhase != bridge.coverage.startPhase ||
      base.coverage.lengthTicks != bridge.coverage.lengthTicks ||
      base.coverage.loopLengthTicks != bridge.coverage.loopLengthTicks ||
      ((captureBridgePhase == CaptureBridgePhase::Compile || captureBridgePhase == CaptureBridgePhase::Complete) &&
       base.storageGeneration != bridge.baseStorageGeneration) ||
      (canonicalCaptureBridge && (!base.sourceFilled || !base.passOriginsReady ||
          !base.passOrigins || !(base.passOriginSource == admission.baseSource) ||
          (captureBridgePhase != CaptureBridgePhase::Map &&
           !frozenCompanionBridgeMap.matchesBase(base.storage.events, base.storage.eventCount, base.source)))))
    return nullptr;
  return &base;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::frozenBridgePreparationReady() const {
  const auto& admission = frozenCaptureSourceAdmission;
  if (!canonicalCaptureBridge) return frozenCaptureCandidateReady(admission.frozenGeneration);
  const auto* set = viewSetIfPrewarmed();
  return set && canonicalPassInputs.frozenCapture() == &set->frozenCaptureStaging() &&
      set->frozenCaptureStaging().frozenGeneration == admission.frozenGeneration &&
      canonicalPassInputs.matchesFrozenCommand(admission.transportCommandEpoch) &&
      frozenCompanionPasses.generation() == captureBridgePreparationGeneration &&
      frozenCompanionBridgeMap.generation() == captureBridgeMapGeneration &&
      frozenCompanionPasses.matchesOwners(canonicalPassInputs, frozenCaptureCompanions) &&
      frozenCompanionPasses.ready(admission.targetSource);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult
LoopPlaybackRuntime::stepFrozenCaptureBridge(size_t entryQuantum) {
  auto* set = viewSetIfPrewarmed();
  auto& admission = frozenCaptureSourceAdmission;
  if (entryQuantum == 0) return RuntimePlaybackOrderBuildResult::Denied;
  if (set == nullptr || captureBridgePhase == CaptureBridgePhase::Idle ||
      captureBridgePhase == CaptureBridgePhase::Denied ||
      !frozenBridgePreparationReady() ||
      set->bridgeLifecycle.candidate != admission.candidateIndex || admission.candidateIndex >= set->bridges.size()) {
    admission.ready = false;
    captureBridgePhase = CaptureBridgePhase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  auto& bridge = set->bridges[admission.candidateIndex];
  const auto* base = frozenCaptureBridgeBase();
  if (base == nullptr || bridge.selected || !(bridge.source == admission.targetSource)) {
    admission.ready = false;
    captureBridgePhase = CaptureBridgePhase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  if (captureBridgePhase == CaptureBridgePhase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  auto result = captureBridgePhase == CaptureBridgePhase::Fill
                    ? frozenCaptureBridgeFill.step(entryQuantum)
                    : captureBridgePhase == CaptureBridgePhase::Map
                        ? frozenCompanionBridgeMap.step(entryQuantum)
                        : frozenCaptureBridgeBuild.step(entryQuantum);
  if (result == RuntimePlaybackOrderBuildResult::Complete &&
      (captureBridgePhase == CaptureBridgePhase::Fill || captureBridgePhase == CaptureBridgePhase::Map)) {
    const auto* retained = canonicalCaptureBridge ? selectedFullLoopBridge(base->source, base->coverage.loopLengthTicks) : nullptr;
    if (frozenCaptureBridgeBuild.begin(*base, bridge, canonicalCaptureBridge ? &frozenCompanionBridgeMap : nullptr, retained)) {
      captureBridgePhase = CaptureBridgePhase::Compile;
      return RuntimePlaybackOrderBuildResult::Pending; // Compiler entries start on a later owner turn.
    }
    SC_ODUB_STOP_STAGE("bridge_compile", 0, 0, UINT32_MAX, UINT32_MAX,
        bridge.ledgerBoundary.count, base->coverage.lengthTicks,
        canonicalCaptureBridge && base->coverage.lengthTicks != base->coverage.loopLengthTicks &&
            bridge.ledgerBoundary.count != 0 ? "partial_open_ledger" : "begin_denied");
    result = RuntimePlaybackOrderBuildResult::Denied;
  }
  if (result == RuntimePlaybackOrderBuildResult::Complete) {
    SC_ODUB_STOP_STAGE("bridge_ready", 0, 0, UINT32_MAX, UINT32_MAX,
        bridge.ledgerBoundary.count, base->coverage.lengthTicks, "context_retained");
    admission.ready = true;
    captureBridgePhase = CaptureBridgePhase::Complete;
  } else if (result == RuntimePlaybackOrderBuildResult::Denied) {
    admission.ready = false;
    captureBridgePhase = CaptureBridgePhase::Denied;
  }
  return result;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::refreshFrozenCompanionBridge(
    const EmissionCursor& anchor) {
  auto* set = viewSetIfPrewarmed();
  const auto* base = frozenCaptureBridgeBase();
  auto& admission = frozenCaptureSourceAdmission;
  if (!set || !base || !canonicalCaptureBridge || captureBridgePhase != CaptureBridgePhase::Complete ||
      !frozenBridgePreparationReady() || admission.candidateIndex >= set->bridges.size() ||
      anchor.effectiveSourceGeneration != admission.targetSource.sourceGeneration) return false;
  auto& bridge = set->bridges[admission.candidateIndex];
  if (bridge.selected) return false;
  const auto* retained = selectedFullLoopBridge(base->source, base->coverage.loopLengthTicks);
  if (!frozenCaptureBridgeBuild.rebindBoundary(*base, bridge,
          frozenCompanionBridgeMap, ledger, anchor, retained)) return false;
  admission.ready = false;
  SC_ODUB_STOP_STAGE("bridge_refresh", 0, 0, UINT32_MAX, UINT32_MAX,
      anchor.phase, anchor.projectionCycle, "closure_only");
  captureBridgePhase = CaptureBridgePhase::Compile;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canSelectFrozenCaptureBridge(
    uint32_t frozenGeneration, uint32_t commandEpoch, uint32_t loopLengthTicks,
    const EmissionCursor& earliestAffectedCursor) const {
  const auto* set = viewSetIfPrewarmed();
  const auto& admission = frozenCaptureSourceAdmission;
  return set != nullptr && captureBridgePhase == CaptureBridgePhase::Complete && admission.ready &&
         admission.frozenGeneration == frozenGeneration && admission.transportCommandEpoch == commandEpoch &&
         set->bridgeLifecycle.candidate == admission.candidateIndex && frozenBridgePreparationReady() &&
         frozenCaptureBridgeBase() != nullptr &&
         admission.candidateIndex < set->bridges.size() &&
         set->bridges[admission.candidateIndex].baseSource == admission.baseSource &&
         (canonicalCaptureBridge ? canonicalPassInputs.loopLength() == loopLengthTicks
                                 : frozenCaptureOrderBinding.loopLengthTicks == loopLengthTicks) &&
         canSelectPreparedFullLoopBridge(admission.targetSource, loopLengthTicks, earliestAffectedCursor);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canSelectReservedFrozenCaptureBridge(
    const PendingStopAdmission& stop, const PlaybackViewSourceIdentity& baseSource,
    const PlaybackViewSourceIdentity& targetSource, uint32_t loopLengthTicks,
    const EmissionCursor& earliestAffectedCursor) const {
  const auto& admission = frozenCaptureSourceAdmission;
  return canonicalCaptureBridge && stop.rankedPreparationQuantum != 0 &&
      stop.phase == PendingStopPhase::AtomicTransfer &&
      stop.canCommit(admission.transportCommandEpoch, PendingStopDestination::Playing) &&
      admission.baseSource == baseSource && admission.targetSource == targetSource &&
      canSelectFrozenCaptureBridge(stop.frozenGeneration, stop.transportCommandEpoch,
                                   loopLengthTicks, earliestAffectedCursor);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::selectFrozenCaptureBridge(
    uint32_t frozenGeneration, uint32_t commandEpoch, uint32_t loopLengthTicks,
    const EmissionCursor& earliestAffectedCursor) {
  if (!canSelectFrozenCaptureBridge(frozenGeneration, commandEpoch, loopLengthTicks, earliestAffectedCursor))
    return false;
  return selectPreparedFullLoopBridge(frozenCaptureSourceAdmission.targetSource, loopLengthTicks, earliestAffectedCursor);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardFrozenCaptureBridge() {
  auto* set = viewSetIfPrewarmed();
  const uint8_t index = frozenCaptureSourceAdmission.candidateIndex;
  if (set != nullptr && index < set->bridges.size() &&
      set->bridgeLifecycle.candidate == index && !set->bridges[index].selected &&
      set->bridges[index].source == frozenCaptureSourceAdmission.targetSource)
    set->bridges[index].resetMetadata();
  frozenCaptureBridgeFill.discard();
  frozenCaptureBridgeBuild.discard();
  if (canonicalCaptureBridge && frozenCompanionBridgeMap.generation() == captureBridgeMapGeneration)
    frozenCompanionBridgeMap.discard();
  canonicalCaptureBridge = false; captureBridgePreparationGeneration = 0;
  captureBridgeMapGeneration = 0;
  captureBridgeBeginFailureMask = 0;
  captureBridgeBase = nullptr;
  frozenCaptureSourceAdmission = PlaybackSourceAdmission{};
  captureBridgePhase = CaptureBridgePhase::Idle;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackBridgeStream* LoopPlaybackRuntime::prepareFullLoopBridgeStream(
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
    uint32_t completedPhase, bool beginNextCycle) {
  const auto* bridge = selectedFullLoopBridge(sourceIdentity, loopLengthTicks);
  if (bridge == nullptr) return nullptr;
  const auto* base = fullLoopBridgeBase(*bridge, loopLengthTicks);
  const uint32_t cycle = bridgeProjectionCycle + (beginNextCycle ? 1u : 0u);
  if (!bridgeStream.matches(*base, *bridge, cycle) &&
      !bridgeStream.selectAfterPhase(*base, *bridge, cycle,
                                     beginNextCycle ? UINT32_MAX : completedPhase)) return nullptr;
  bridgeProjectionCycle = cycle;
  return &bridgeStream;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM EmissionCursor LoopPlaybackRuntime::currentCycleCursor(
    uint32_t phase, uint32_t sourceGeneration) const {
  return {bridgeProjectionCycle, phase, 0, 0, sourceGeneration};
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView* LoopPlaybackRuntime::publishedBridgeWindow(
    const PlaybackViewSourceIdentity& source, uint32_t phase) const {
  const auto* set = viewSetIfPrewarmed();
  if (!set || set->lifecycle.current >= set->views.size() ||
      bridgeWindowContextIndex != set->bridgeLifecycle.selected ||
      bridgeWindowContextIndex >= set->bridges.size() ||
      bridgeWindowContextGeneration != set->bridges[bridgeWindowContextIndex].storageGeneration)
    return nullptr;
  const auto& view = set->views[set->lifecycle.current];
  return view.published && view.complete && view.source == source &&
      view.cursorAnchor.projectionCycle == bridgeProjectionCycle && view.coverage.contains(phase)
      ? &view : nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::serviceBridgeWindow(
    uint32_t currentPhase, uint32_t windowTicks, size_t quantum) {
  RuntimePlaybackViewBuild::PreparationTurn primary(viewBuild), bridgeTurn(bridgeWindowBuild);
  if (!primary || !bridgeTurn || !windowTicks || !quantum) return false;
  RuntimePlaybackViewSet* set = nullptr;
  uint8_t index = RuntimePlaybackBridgeLifecycle::kNoBridge;
  uint64_t bridgeGeneration = 0;
  uint32_t length = 0, projectionCycle = 0;
  {
    PlaybackSourceReadGuard guard;
    set = viewSetIfPrewarmed();
    if (!primary || !bridgeTurn || !set || set->bridgeLifecycle.selected >= set->bridges.size()) return false;
    index = set->bridgeLifecycle.selected;
    const auto& bridge = set->bridges[index];
    if (!bridge.complete || !bridge.selected || !bridge.streamPrepared ||
        currentPhase >= bridge.coverage.loopLengthTicks) return false;
    bridgeGeneration = bridge.storageGeneration;
    length = bridge.coverage.loopLengthTicks;
    projectionCycle = bridgeProjectionCycle;
  }
  const auto& bridge = set->bridges[index];
  // The base query may inspect content; it never runs inside the metadata guard.
  const auto* base = fullLoopBridgeBase(bridge, length);
  if (!base) return false;
  const auto selectedCurrent = [&]() TRACK_PLAYBACK_RUNTIME_COLD_MEM {
    return primary && bridgeTurn && viewSetIfPrewarmed() == set &&
        set->bridgeLifecycle.selected == index && bridge.storageGeneration == bridgeGeneration &&
        bridge.complete && bridge.selected && bridge.streamPrepared &&
        bridge.coverage.loopLengthTicks == length && bridgeProjectionCycle == projectionCycle;
  };
  const auto contextCurrent = [&]() TRACK_PLAYBACK_RUNTIME_COLD_MEM {
    return selectedCurrent() && bridgeWindowContextIndex == index &&
        bridgeWindowContextGeneration == bridgeGeneration;
  };
  const uint32_t width = std::min(windowTicks, length);
  const uint64_t now = uint64_t(projectionCycle) * length + currentPhase;
  const auto endOf = [length](const RuntimePlaybackView& view) TRACK_PLAYBACK_RUNTIME_COLD_MEM {
    return uint64_t(view.cursorAnchor.projectionCycle) * length +
        view.coverage.startPhase + view.coverage.lengthTicks;
  };
  uint8_t targetIndex = RuntimePlaybackViewLifecycle::kNoView;
  uint8_t previousBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
  uint64_t targetGeneration = 0, start = 0;
  uint32_t completed = UINT32_MAX;
  bool existing = false;
  RuntimePlaybackViewLifecycle selectedRoles;
  RuntimePlaybackViewBuildGrain grain = RuntimePlaybackViewBuildGrain::Denied;
  {
    PlaybackSourceReadGuard guard;
    if (!selectedCurrent()) return false;
    if (bridgeWindowContextIndex != index || bridgeWindowContextGeneration != bridgeGeneration) {
      bridgeWindowBuild.discard();
      bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
      // Fixed three-view metadata only; never a content or retirement loop.
      for (auto& view : set->views) view.resetMetadata();
      set->lifecycle.reset();
      bridgeWindowContextIndex = index;
      bridgeWindowContextGeneration = bridgeGeneration;
      return true;
    }
    auto& life = set->lifecycle;
    // Select and inspect ready in the same guard. An independent ready adopter
    // remains allowed between our unmasked worker calls.
    if (life.ready < set->views.size() && set->views[life.ready].complete) {
      const auto& ready = set->views[life.ready];
      if (ready.cursorAnchor.projectionCycle == projectionCycle && ready.coverage.contains(currentPhase)) {
        if (bridgeWindowBuildIndex == life.ready &&
            bridgeWindowBuild.completedDestinationIs(ready)) {
          bridgeWindowBuild.discard(&ready);
          bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
        }
        if (readyPositionAllowsAdoption(currentDeadlineClockForAdoption()))
          (void)set->handoffReadyCoverage();
        if (bridgeWindowBuildIndex == RuntimePlaybackViewLifecycle::kNoView &&
            life.build < set->views.size() && set->views[life.build].complete &&
            !set->views[life.build].published && set->views[life.build].source == bridge.source &&
            set->views[life.build].coverage.loopLengthTicks == length)
          (void)set->publishCompletedBuildAsReady();
        return true;
      }
      if (endOf(ready) <= now) {
        const uint8_t expired = life.ready;
        if (bridgeWindowBuildIndex == expired) {
          bridgeWindowBuild.discard();
          bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
        }
        set->views[expired].resetMetadata();
        return true;
      }
    }
    previousBuildIndex = bridgeWindowBuildIndex;
    existing = previousBuildIndex < set->views.size();
    if (existing) {
      targetIndex = previousBuildIndex;
      auto& target = set->views[targetIndex];
      // A clock-edge adopter may consume this exact completed ready while the
      // preparation turn is unmasked. Detach its worker, never reset current.
      if (targetIndex == life.current && target.published &&
          bridgeWindowBuild.completedDestinationIs(target)) {
        bridgeWindowBuild.discard(&target);
        bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
        return true;
      }
      if (targetIndex == life.current || targetIndex == life.retained) return false;
      if (endOf(target) <= now) {
        bridgeWindowBuild.discard();
        target.resetMetadata();
        bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
        return true;
      }
      grain = bridgeWindowBuild.nextGrain();
    } else {
      targetIndex = life.ready < set->views.size() && !set->views[life.ready].complete
          ? life.ready : life.build;
      if (targetIndex >= set->views.size() || set->views[targetIndex].complete ||
          targetIndex == life.current || targetIndex == life.retained) return false;
      start = uint64_t(projectionCycle) * length + currentPhase / width * width;
      if (life.ready < set->views.size() && set->views[life.ready].complete)
        start = endOf(set->views[life.ready]);
      else if (publishedBridgeWindow(bridge.source, currentPhase))
        start = endOf(set->views[life.current]);
      const uint32_t cycle = start / length;
      completed = cycle == bridge.cursorAnchor.projectionCycle && bridge.preparedAdmissionPhase
          ? bridge.preparedAdmissionPhase - 1 : UINT32_MAX;
    }
    selectedRoles = life;
    targetGeneration = set->views[targetIndex].storageGeneration;
  }
  const auto rolesCurrent = [&]() TRACK_PLAYBACK_RUNTIME_COLD_MEM {
    const auto& life = set->lifecycle;
    return contextCurrent() && bridgeWindowBuildIndex == previousBuildIndex &&
        life.current == selectedRoles.current && life.ready == selectedRoles.ready &&
        life.build == selectedRoles.build && life.retained == selectedRoles.retained &&
        life.idle == selectedRoles.idle;
  };
  auto& target = set->views[targetIndex];
  if (existing) {
    if (grain == RuntimePlaybackViewBuildGrain::Fill) bridgeWindowBuild.stepFill(quantum);
    else if (grain == RuntimePlaybackViewBuildGrain::Order) bridgeWindowBuild.stepOrder(quantum);
    PlaybackSourceReadGuard guard;
    if (!rolesCurrent() || target.storageGeneration != targetGeneration) return false;
    if (grain == RuntimePlaybackViewBuildGrain::Complete) {
      if (!bridgeWindowBuild.completedDestinationIs(target)) return false;
      if (set->lifecycle.ready == RuntimePlaybackViewLifecycle::kNoView)
        (void)set->publishCompletedBuildAsReady();
      bridgeWindowBuild.discard(&target);
      bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
    } else if (grain == RuntimePlaybackViewBuildGrain::Denied) {
      // Cancellation is checked above before any direct cleanup or index write.
      bridgeWindowBuild.discard();
      target.resetMetadata();
      bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
    }
    return true;
  }
  const uint32_t phase = start % length;
  const uint32_t cycle = start / length;
  if (!bridgeWindowSource.selectAfterPhase(*base, bridge, cycle, completed)) return false;
  {
    PlaybackSourceReadGuard guard;
    if (!rolesCurrent() || target.storageGeneration != targetGeneration) return false;
  }
  const PlaybackCoverageInterval coverage{phase, std::min(width, length - phase), length};
  const EmissionCursor anchor{cycle, phase, 0, 0, bridge.source.sourceGeneration};
  if (!bridgeWindowBuild.begin(target, bridgeWindowSource.source(), bridge.source,
      coverage, anchor, bridge.ledgerBoundary, RuntimePlaybackViewFillScope::PlaybackCoverage)) return false;
  PlaybackSourceReadGuard guard;
  // begin resets exactly this destination's metadata once. Independent ready
  // adoption may rotate unrelated roles, but cannot replace this private role.
  const auto& life = set->lifecycle;
  const bool sameDestinationRole = targetIndex == selectedRoles.ready
      ? life.ready == targetIndex : life.build == targetIndex;
  if (!contextCurrent() || bridgeWindowBuildIndex != previousBuildIndex ||
      !sameDestinationRole || targetIndex == life.current || targetIndex == life.retained ||
      target.storageGeneration != targetGeneration + 1) return false;
  bridgeWindowBuildIndex = targetIndex;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginCoverageBuild(
    const CoverageServiceContract& contract, uint32_t currentPhase, uint32_t handoffPhase,
    const MidiEvent* source, size_t sourceCount, const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, uint32_t positionedLead) {
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  const bool cold = frozenViewRequest.active && rolling.phase == RollingPhase::Requested &&
      rolling.epoch == 0 && rolling.deadlineTick == 0 && frozenViewRequest.clockEpoch == 0 &&
      frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
      frozenViewRequest.operationGeneration == rolling.generation && coverage.startPhase == 0 &&
      coverage.lengthTicks == rolling.loopLength && coverage.loopLengthTicks == rolling.loopLength;
  if (set == nullptr || viewBuild.active() || completedPromisePendingJudgement || ownsCoverageSource() ||
      set->lifecycle.build == RuntimePlaybackViewLifecycle::kNoView ||
      (!cold && (positionedLead != 0
                     ? !contract.canCompleteWithinLead(positionedLead)
                     : !contract.canCompleteBeforeHandoff(currentPhase, handoffPhase, coverage.loopLengthTicks)))) {
    return false;
  }
  RuntimePlaybackView& destination = set->views[set->lifecycle.build];
  if (!viewBuild.begin(destination, source, sourceCount, sourceIdentity, coverage, cursorAnchor,
                         ledgerBoundary, RuntimePlaybackViewFillScope::PlaybackCoverage)) return false;
  viewBuild.markCoverageOwned();
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginCoverageBuild(
    const CoverageServiceContract& contract, uint32_t currentPhase, uint32_t handoffPhase,
    LoopSnapshot::EventWindowCursor& source, const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary, uint32_t positionedLead) {
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || viewBuild.active() || completedPromisePendingJudgement || ownsCoverageSource() ||
      set->lifecycle.build == RuntimePlaybackViewLifecycle::kNoView ||
      (positionedLead != 0 ? !contract.canCompleteWithinLead(positionedLead)
                           : !contract.canCompleteBeforeHandoff(currentPhase, handoffPhase, coverage.loopLengthTicks)))
    return false;
  auto& destination = set->views[set->lifecycle.build];
  if (!viewBuild.begin(destination, source, sourceIdentity, coverage, cursorAnchor, ledgerBoundary)) return false;
  viewBuild.markCoverageOwned();
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM PlaybackCoverageRoleRefusal LoopPlaybackRuntime::coverageRoleRefusal(
    bool keepCaptureReady) const {
  // One implementation of the order: the yes/no answer below cannot disagree with the reported term.
  const auto* set = viewSetIfPrewarmed();
  if (!set) return PlaybackCoverageRoleRefusal::NoViewSet;
  if (viewBuild.active()) return PlaybackCoverageRoleRefusal::BuildActive;
  if (completedPromisePendingJudgement) return PlaybackCoverageRoleRefusal::CompletedPromisePending;
  if (ownsCoverageSource()) return PlaybackCoverageRoleRefusal::OwnsCoverageSource;
  if (canonicalWindowPhase != CanonicalWindowPhase::Idle &&
      canonicalWindowPhase != CanonicalWindowPhase::Ready)
    return PlaybackCoverageRoleRefusal::CanonicalWindowBusy;
  if (set->lifecycle.build >= set->views.size()) return PlaybackCoverageRoleRefusal::BuildRoleOutOfRange;
  if (set->views[set->lifecycle.build].published) return PlaybackCoverageRoleRefusal::BuildRolePublished;
  if (set->views[set->lifecycle.build].complete) return PlaybackCoverageRoleRefusal::BuildRoleComplete;
  if (set->lifecycle.ready >= set->views.size() || !set->views[set->lifecycle.ready].complete)
    return PlaybackCoverageRoleRefusal::None;
  const auto& ready = set->views[set->lifecycle.ready];
  if (keepCaptureReady && !ready.published && ready.source.origin == PlaybackWindowOrigin::Capture &&
      set->lifecycle.build != set->lifecycle.ready &&
      set->lifecycle.build != set->lifecycle.current &&
      set->lifecycle.build != set->lifecycle.retained)
    return PlaybackCoverageRoleRefusal::None;
  return PlaybackCoverageRoleRefusal::ReadyRoleOccupied;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::coverageRoleAvailable(bool keepCaptureReady) const {
  return coverageRoleRefusal(keepCaptureReady) == PlaybackCoverageRoleRefusal::None;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginCoverageQualification(
    const LoopSnapshot& snapshot, bool keepCaptureReady) {
  if (!coverageRoleAvailable(keepCaptureReady) || !pendingViewRequest_.active ||
      (pendingViewRequest_.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
       pendingViewRequest_.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedLaunch)) return false;
  if (!coverageSource.begin(snapshot, pendingViewRequest_.coverage.startPhase,
                            pendingViewRequest_.coverage.lengthTicks)) return false;
  auto* set = viewSetIfPrewarmed();
  if (!viewBuild.beginCoverageQualification(set->views[set->lifecycle.build], coverageSource,
      pendingViewRequest_.source, pendingViewRequest_.coverage)) {
    coverageSource.beginRelease();
    coveragePreparation = CoveragePreparation::Retiring;
    return false;
  }
  frozenViewRequest = pendingViewRequest_;
  clearViewPendingRequest(); // Freeze discharges ONLY this record; later requests use the now-free slot.
  coverageEventCount = 0;
  coverageSourceUnits = 0;
  coveragePreparation = CoveragePreparation::Counting;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::stepCoverageQualification(size_t quantum) {
  if (coveragePreparation != CoveragePreparation::Counting || !quantum) return false;
  const auto result = viewBuild.stepCoverageQualification(quantum);
  coverageEventCount = static_cast<uint32_t>(viewBuild.qualifiedEventCount());
#if REVISION_MANIFEST_READER
  coverageSourceUnits = static_cast<uint32_t>(coverageSourceReadUnits());
  if (result == RuntimePlaybackViewFillResult::Denied && ownsManifestCoverage()) {
    // The read ended by name (`SourceMoved`, `IndexOwed`, `CapacityDenied`): the candidate is discarded, never
    // published, and the demand is owed again. Nothing else is read in its place.
    const auto why = CommittedManifestLane::gReader->windowResult();
    ++CommittedManifestLane::gCounters.owed[static_cast<uint8_t>(why)];
    frozenViewRequest.reason = why == CommittedManifestLane::State::CapacityDenied ||
            coverageEventCount > RuntimePlaybackViewConfig::kWindowAdmissionLimit
        ? PlaybackViewPendingReason::WindowExceedsRetainedCapacity : PlaybackViewPendingReason::SourceNotCurrent;
    retireCoverageSource();
    return false;
  }
#else
  coverageSourceUnits = static_cast<uint32_t>(coverageSource.readUnits());
#endif
  if (result == RuntimePlaybackViewFillResult::Denied) {
    frozenViewRequest.reason = coverageEventCount > RuntimePlaybackViewConfig::kWindowAdmissionLimit
        ? PlaybackViewPendingReason::WindowExceedsRetainedCapacity : PlaybackViewPendingReason::SourceNotCurrent;
    retireCoverageSource();
    return false;
  }
  if (result == RuntimePlaybackViewFillResult::Complete) {
    coveragePreparation = CoveragePreparation::AwaitAdmission;
#if REVISION_MANIFEST_READER
    if (ownsManifestCoverage()) ++CommittedManifestLane::gCounters.windowsCompleted;
#endif
  }
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginQualifiedCoverageBuild(
    const CoverageServiceContract& contract, PlaybackDeadlineClock clock) {
  const bool launch = frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
  const auto& frame = rolling;
  const bool cold = frame.phase == RollingPhase::Requested && frame.epoch == 0 &&
      frame.deadlineTick == 0 && frame.startPhase == 0 &&
      frame.loopLength == frozenViewRequest.coverage.loopLengthTicks &&
      frozenViewRequest.coverage.startPhase == 0 &&
      frozenViewRequest.coverage.lengthTicks == frame.loopLength &&
      frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
      frozenViewRequest.operationGeneration == frame.generation &&
      frozenViewRequest.clockEpoch == 0 && frozenViewRequest.deadlineTick == 0;
  // DEC-136: a demand the caller waived the lead test for (the bound frozen reader's privately prepared
  // root successor) is owed **without a deadline**, so it has no position to check and no promise to keep --
  // the frozen reader serves until this build is complete and adopted. Every other requirement below still
  // applies, so the window is only ever prepared privately and never served before it is valid.
  const bool privateRootReturn =
      frozenViewRequest.reason == PlaybackViewPendingReason::PrivateRootReturnPreparation;
#if PLAYBACK_ROLLING_SUCCESSOR_PROBE
  // C5.3b host probe: the same DEC-147 lead price `Track::stepCommittedPlaybackRetry` skips for the serving frame's own
  // successor demand is skipped here too -- it is one decision charged twice, not a second rule.
  const bool successorPriceWaived = RollingSuccessorProbe::gKnobs.enabled &&
      RollingSuccessorProbe::gKnobs.bypassLeadPrice && frame.phase == RollingPhase::Serving &&
      frame.successorState == 1 && frame.deadlineTick != 0 && frame.startPhase < frame.loopLength &&
      frozenViewRequest.deadlineTick ==
          frame.deadlineTick + std::min(uint32_t(2 * Config::TICKS_PER_BAR), frame.loopLength - frame.startPhase);
#else
  constexpr bool successorPriceWaived = false;
#endif
#if REVISION_MANIFEST_READER
  if (coveragePreparation != CoveragePreparation::AwaitAdmission || !coverageSourceComplete() ||
#else
  if (coveragePreparation != CoveragePreparation::AwaitAdmission || !coverageSource.complete() ||
#endif
      !frozenViewRequest.active || frozenViewRequest.positionInvalidated ||
      (!launch && !cold && !privateRootReturn &&
       (clock.epoch == 0 || clock.epoch != frozenViewRequest.clockEpoch ||
        clock.tick < frozenViewRequest.work.admittedTick ||
        clock.tick >= frozenViewRequest.deadlineTick ||
        (!successorPriceWaived && !contract.canCompleteWithinLead(frozenViewRequest.deadlineTick - clock.tick))))) return false;
  auto* set = viewSetIfPrewarmed();
  // Qualification may run privately beside a Capture ready, but admission
  // cannot discharge that request until the single successor role is free.
  if (!set || set->lifecycle.build >= set->views.size() ||
      (set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete) ||
#if REVISION_MANIFEST_READER
      coverageSourceReadUnits() != coverageSourceUnits) return false;
#else
      coverageSource.readUnits() != coverageSourceUnits) return false;
#endif
  const auto& candidate = set->views[set->lifecycle.build];
  if (!(candidate.source == frozenViewRequest.source) || candidate.storage.eventCount != coverageEventCount ||
      candidate.coverage.startPhase != frozenViewRequest.coverage.startPhase ||
      candidate.coverage.lengthTicks != frozenViewRequest.coverage.lengthTicks ||
      candidate.coverage.loopLengthTicks != frozenViewRequest.coverage.loopLengthTicks) return false;
  if (!viewBuild.admitCoverageQualification()) {
    retireCoverageSource();
    return false;
  }
  viewBuild.markCoverageOwned();
  coveragePreparation = CoveragePreparation::Building;
  if (launch) launchPreparation = LaunchPreparation::Building;
  viewBuildWork = frozenViewRequest.work;
  viewBuildWork.sourceEntries = coverageEventCount;
  viewBuildWork.fillGrains = 0; // Qualification already filled this private candidate.
  viewBuildWork.orderGrains = contract.requiredOrderGrains;
  viewBuildWork.promotionGrains = contract.requiredPromotionGrains;
  viewBuildOperationGeneration = frozenViewRequest.operationGeneration;
  countedMissPromiseTick = viewBuildWork.admittedTick;
  countedMissedTraversals = frozenViewRequest.countedMisses;
  admittedPromiseInvalidated = false;
  completedPromisePendingJudgement = false;
  if (!launch && !lagState.coversInterval(viewBuildWork.handoffPhase, viewBuildWork.admittedIntervalLength))
    lagState.resetEpisode(viewBuildWork.handoffPhase, viewBuildWork.admittedIntervalLength);
  frozenViewRequest = {}; // Promise is transferred whole into viewBuildWork, not re-granted.
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::retireCoverageSource() {
#if REVISION_MANIFEST_READER
  const bool manifest = ownsManifestCoverage();
  if (coverageSource.idle() && !manifest) return;
#else
  if (coverageSource.idle()) return;
#endif
  if (viewBuild.coverageOwned()) viewBuild.discard();
  // Cancellation must not erase a newer request. Coalesce the frozen obligation
  // behind it using the existing retention rule (oldest compatible position).
  if (frozenViewRequest.active) {
    if (!pendingViewRequest_.active) pendingViewRequest_ = frozenViewRequest;
    else if (pendingViewRequest_.operationGeneration == frozenViewRequest.operationGeneration &&
             pendingViewRequest_.clockEpoch == frozenViewRequest.clockEpoch &&
             pendingViewRequest_.loopLengthTicks == frozenViewRequest.loopLengthTicks &&
             pendingViewRequest_.deadlineTick > frozenViewRequest.deadlineTick) {
      pendingViewRequest_.deadlineTick = frozenViewRequest.deadlineTick;
      pendingViewRequest_.work.deadlineTick = frozenViewRequest.deadlineTick;
      pendingViewRequest_.work.admittedTick = frozenViewRequest.work.admittedTick;
      pendingViewRequest_.requestedPhase = frozenViewRequest.requestedPhase;
      pendingViewRequest_.positionInvalidated = frozenViewRequest.positionInvalidated;
      pendingViewRequest_.countedMisses = frozenViewRequest.countedMisses;
    }
    frozenViewRequest = {};
  }
#if REVISION_MANIFEST_READER
  if (manifest) {
    releaseManifestCoverage();  // no retirement steps: the read's pin ends here
    return;
  }
#endif
  coverageSource.beginRelease();
  coveragePreparation = CoveragePreparation::Retiring;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::stepCoverageSourceRetirement(size_t quantum) {
  if (!coverageSource.retiring()) return false;
  if (coverageSource.stepRelease(quantum)) coveragePreparation = CoveragePreparation::Idle;
  return true;
}

#if REVISION_MANIFEST_READER
#if defined(__IMXRT1062__)
// C5.3: the lane's report-only counters, RAM2 NOLOAD (zeroed by the reader owner before it installs itself).
DMAMEM CommittedManifestLane::Counters CommittedManifestLane::gCounters;
#endif
TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::ownsManifestCoverage() const {
  return CommittedManifestLane::gWindowOwner == this;
}

/// C5.3: the snapshot qualification's own preconditions and freeze, over the reader's manifest window read. The read
/// was begun by the lane's producer turn for exactly the demand's manifest identity; this runtime now holds it.
TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginManifestCoverageQualification(
    CommittedWindowRowSource& source, bool keepCaptureReady) {
  if (CommittedManifestLane::gWindowOwner != nullptr || !coverageRoleAvailable(keepCaptureReady) ||
      !pendingViewRequest_.active ||
      (pendingViewRequest_.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
       pendingViewRequest_.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedLaunch)) return false;
  CommittedManifestLane::gWindowOwner = this;
  CommittedManifestLane::gWindowSource = &source;
  auto* set = viewSetIfPrewarmed();
  if (!viewBuild.beginCoverageQualification(set->views[set->lifecycle.build], source,
      pendingViewRequest_.source, pendingViewRequest_.coverage)) {
    releaseManifestCoverage();
    return false;
  }
  frozenViewRequest = pendingViewRequest_;
  clearViewPendingRequest();
  coverageEventCount = 0;
  coverageSourceUnits = 0;
  coveragePreparation = CoveragePreparation::Counting;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::releaseManifestCoverage() {
  if (!ownsManifestCoverage()) return;
  if (CommittedManifestLane::gReader != nullptr) CommittedManifestLane::gReader->endWindow();
  CommittedManifestLane::gWindowOwner = nullptr;
  CommittedManifestLane::gWindowSource = nullptr;
  coveragePreparation = CoveragePreparation::Idle;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::coverageSourceComplete() const {
  return ownsManifestCoverage() ? CommittedManifestLane::gWindowSource->complete() : coverageSource.complete();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM size_t LoopPlaybackRuntime::coverageSourceReadUnits() const {
  return ownsManifestCoverage() ? CommittedManifestLane::gWindowSource->readUnits() : coverageSource.readUnits();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM size_t LoopPlaybackRuntime::coverageSourceReleaseUnitBound() const {
  return ownsManifestCoverage() ? 0 : coverageSource.qualifiedReleaseUnitBound();
}
#endif

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prepareFirstRecordingBar(
    uint32_t ticksPerBar, const RuntimePlaybackViewBuild::PreparationTurn* primary,
    const RuntimePlaybackViewBuild::PreparationTurn* bridge) {
  RuntimePlaybackViewBuild::PreparationTurn localPrimary(viewBuild), localBridge(bridgeWindowBuild);
  const auto& primaryTurn = primary ? *primary : localPrimary;
  const auto& bridgeTurn = bridge ? *bridge : localBridge;
  if (!primaryTurn.owns(viewBuild) || !bridgeTurn.owns(bridgeWindowBuild) ||
      viewBuild.callInProgress() || bridgeWindowBuild.callInProgress()) return false;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr) {
    firstRecordingBarDenial_ = "no_prewarmed_view_set";
    return false;
  }
  if (ticksPerBar == 0) {
    firstRecordingBarDenial_ = "zero_bar_length";
    return false;
  }
  if (set->lifecycle.retained != RuntimePlaybackViewLifecycle::kNoView) {
    // A previous take's return window is still held. `releaseUnlaunchedFirstRecordingBar`
    // retires an unlaunched one; a bound window belongs to the audible handoff and is a
    // programming error on the record-start path. This is reported before the role check
    // because a held return window also leaves no free build role.
    firstRecordingBarDenial_ = "return_window_held";
    return false;
  }
  if (set->lifecycle.ready == RuntimePlaybackViewLifecycle::kNoView) {
    firstRecordingBarDenial_ = "no_free_build_role";
    return false;
  }
  if (viewBuild.active()) {
    firstRecordingBarDenial_ = "shared_build_active";
    return false;
  }
  RuntimePlaybackView& destination = set->views[set->lifecycle.ready];
  const PlaybackCoverageInterval coverage{0, ticksPerBar, 0};
  if (!viewBuild.begin(destination, nullptr, 0, PlaybackViewSourceIdentity{}, coverage,
                       EmissionCursor{}, LedgerBoundarySummary{},
                       RuntimePlaybackViewFillScope::RawCoverage)) {
    firstRecordingBarDenial_ = "fill_admission_denied";
    return false;
  }
  if (viewBuild.stepFill(1) == RuntimePlaybackViewBuildGrain::Denied ||
      !destination.complete || !destination.rawCoveragePrepared) {
    viewBuild.discard();
    firstRecordingBarDenial_ = "fill_grain_denied";
    return false;
  }
  PlaybackSourceReadGuard publicationGuard;
  if (!primaryTurn || !bridgeTurn || !viewBuild.completedDestinationIs(destination)) return false;
  if (!set->lifecycle.installPreparedCurrent(set->views)) {
    viewBuild.discard();
    firstRecordingBarDenial_ = "lifecycle_install_denied";
    return false;
  }
  firstRecordingBarDenial_ = "none";
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const char*
LoopPlaybackRuntime::firstRecordingBarDenialLabel() const {
  return firstRecordingBarDenial_ != nullptr ? firstRecordingBarDenial_ : "unknown";
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::releaseFirstRecordingBarReturnWindow(
    const RuntimePlaybackViewBuild::PreparationTurn* primary,
    const RuntimePlaybackViewBuild::PreparationTurn* bridge) {
  PlaybackSourceReadGuard guard;
  if ((primary && !primary->owns(viewBuild)) ||
      (bridge && !bridge->owns(bridgeWindowBuild))) return false;
  if (viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
      (viewBuild.preparationInProgress() && (!primary || !primary->owns(viewBuild))) ||
      (bridgeWindowBuild.preparationInProgress() && (!bridge || !bridge->owns(bridgeWindowBuild))))
    return false;
  // A stop job that is still retiring owns the record-session lifecycle; do not
  // release a window under it.
  if (captureStopOwner && captureStopOwner->pending) {
    return false;
  }
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr) {
    return false;
  }
  // A take that handed off keeps the window as `current`; one that did not keeps
  // it as `retained`. Either way the record session is over for this slot.
  uint8_t window = set->firstRecordReturnWindowIndex();
  if (window == RuntimePlaybackViewLifecycle::kNoView) {
    window = set->lifecycle.retained;
  }
  if (window == RuntimePlaybackViewLifecycle::kNoView || window >= set->views.size()) {
    return false;
  }
  viewBuild.discard();
  set->views[window].resetMetadata();
  // Restore the prewarm role layout so the next preparation installs a current view
  // exactly as it does at boot. `installPreparedCurrent` resets the retired current.
  set->lifecycle.reset();
  if (set->lifecycle.ready < set->views.size()) {
    set->views[set->lifecycle.ready].resetMetadata();
  }
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView*
LoopPlaybackRuntime::adoptFirstRecordingBarReturnWindow(
    Loop& loop, const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
    uint32_t phase) {
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || loopLengthTicks == 0) {
    return nullptr;
  }
  const uint8_t index = set->firstRecordReturnWindowIndex();
  if (index >= set->views.size()) {
    return nullptr;
  }
  RuntimePlaybackView& window = set->views[index];
  // A bound return window: raw coverage already replaced by final loop geometry.
  // It holds only the first bar, so it serves the launch while `phase` is inside
  // that coverage; past it the caller falls back (the root has normally published
  // by then). A note whose closing Off lies beyond the window is closed by the
  // published root when the playhead reaches it; if the root is late, the loop
  // wrap releases it at the latest. Carrying that closure in the window itself is
  // the remaining bundle work (CURRENT_WORK S2).
  const bool phaseInsideCoverage =
      phase >= window.coverage.startPhase &&
      phase - window.coverage.startPhase < window.coverage.lengthTicks;
  if (window.rawCoveragePrepared || !window.complete || !phaseInsideCoverage ||
      window.coverage.loopLengthTicks != loopLengthTicks) {
    return nullptr;
  }
  if (!(window.source == sourceIdentity)) {
    window.source = sourceIdentity;
    // The previous serviced phase, not this turn's phase, is the exclusion
    // floor. Publication can change identity on a tick whose Off/On are due;
    // seeking past that current tick would omit both edges. A wrap or an
    // unserviced interval has no exclusion floor in the new traversal.
    const uint32_t servedThroughPhase = phase < loop.lastTickInLoop
        ? UINT32_MAX : loop.lastTickInLoop;
    size_t cursor = 0;
    while (cursor < window.storage.eventCount) {
      const MidiEvent& event = window.storage.events[window.storage.order[cursor]];
      if (servedThroughPhase == UINT32_MAX ||
          IntervalProjection::playbackEventPhase(event.tick, loopLengthTicks) > servedThroughPhase) {
        break;
      }
      ++cursor;
    }
    loop.nextEventIndex = static_cast<uint16_t>(cursor);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    DebugSessionCapture::firstRecordReturnWindowAdopted(loopLengthTicks,
                                                        window.storage.eventCount);
#endif
  }
  // A preserve-reset clears the published flag across the launch; the window is
  // immutable now, so re-publish it for the audible fixed-view path.
  window.published = true;
  return &window;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::bindFirstRecordingBar(
    uint32_t loopLengthTicks, const Loop* recordedLoop) {
  PlaybackSourceReadGuard guard;
  if (viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
      viewBuild.preparationInProgress() || bridgeWindowBuild.preparationInProgress()) return false;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView ||
      set->lifecycle.retained >= set->views.size()) {
    return false;
  }
  if (recordedLoop != nullptr && discardStaleFirstRecordingBar(*recordedLoop)) return false;
  if (!set->views[set->lifecycle.retained].bindRawCoverage(loopLengthTicks)) return false;
  return set->lifecycle.handoffToRetained(set->views);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::discardStaleFirstRecordingBar(
    const Loop& recordedLoop) {
  if (viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
      viewBuild.preparationInProgress() || bridgeWindowBuild.preparationInProgress()) return false;
  PlaybackSourceReadGuard guard;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.retained >= set->views.size()) return false;
  RuntimePlaybackView& retained = set->views[set->lifecycle.retained];
  if (!retained.complete || !retained.rawCoveragePrepared ||
      (recordingWindowGenerationValid &&
       recordingWindowGeneration == recordedLoop.captureDisplayRevision)) return false;
  // The unpublished raw bar predates accepted Capture input. Never publish it
  // as current; the existing frozen-range owner builds the complete bar idle.
  viewBuild.discard();
  retained.complete = false;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::bindDeferredRecordReturnWindow(
    const LoopEventStore::CaptureRange& range, uint64_t expectedGeneration,
    uint32_t loopLengthTicks, const uint64_t* openAtStopLanes) {
  RuntimePlaybackViewBuild::PreparationTurn primary(viewBuild), bridgeTurn(bridgeWindowBuild);
  if (!primary || !bridgeTurn) return false;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || loopLengthTicks == 0 || !range.ready() ||
      range.generation() != expectedGeneration) {
    return false;
  }
  // The Record stop edge can already have bound the complete first bar and
  // handed `retained` to `current`. That published view is immutable: the
  // deferred closure acknowledges the same final geometry, not a second
  // projection into the view the MIDI edge may be reading. Short takes cannot
  // pass the stop-edge bind and still take the raw retained path below.
  if (set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView &&
      set->lifecycle.current < set->views.size()) {
    const RuntimePlaybackView& current = set->views[set->lifecycle.current];
    if (current.firstRecordReturnWindow && current.complete &&
        !current.rawCoveragePrepared && current.coverage.startPhase == 0 &&
        current.coverage.lengthTicks == Config::TICKS_PER_BAR &&
        current.coverage.loopLengthTicks == loopLengthTicks) {
      return true;
    }
  }
  if (set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView ||
      set->lifecycle.retained >= set->views.size()) {
    return false;
  }
  RuntimePlaybackView& retained = set->views[set->lifecycle.retained];
  const uint64_t retainedGeneration = retained.storageGeneration;
  if (!retained.complete || !retained.rawCoveragePrepared) {
    return false;
  }
  // Project the raw bar onto this interval's final geometry first: remap an
  // overflowing NoteOn into the loop, drop other overflowing rows, close a note left
  // open at the loop end, and re-order canonically. A shorter final length (a sub-bar
  // take) is the normal case here, not a refusal: binding raw rows past the length
  // would emit out-of-loop content. A refusal leaves the window untouched.
  if (!retained.projectRawCoverageToLoopLength(loopLengthTicks, openAtStopLanes)) {
    return false;
  }
  PlaybackSourceReadGuard guard;
  if (!primary || !bridgeTurn || viewSetIfPrewarmed() != set ||
      set->lifecycle.retained >= set->views.size() ||
      &set->views[set->lifecycle.retained] != &retained ||
      retained.storageGeneration != retainedGeneration || !range.ready() ||
      range.generation() != expectedGeneration) return false;
  return retained.bindRawCoverage(loopLengthTicks) &&
      set->lifecycle.handoffToRetained(set->views);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool
LoopPlaybackRuntime::hasServableDeferredRecordView() const {
  if (!hasBoundRecordReturnWindowForPendingJob()) return false;
  const RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr) return false;
  if (set->firstRecordReturnWindowIndex() != RuntimePlaybackViewLifecycle::kNoView) {
    return true;
  }
  const auto onward = [&](uint8_t index) {
    return index < set->views.size() && set->views[index].complete &&
        !set->views[index].rawCoveragePrepared &&
        !set->views[index].firstRecordReturnWindow;
  };
  return onward(set->lifecycle.current) || onward(set->lifecycle.ready);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool
LoopPlaybackRuntime::hasBoundRecordReturnWindowForPendingJob() const {
  // The finalizer owns the reader until publication; the presentation owns it afterward. A ring
  // job at RecordPublished (notably an empty result) must never impersonate a published reader.
  const bool finalizing = captureStopOwner != nullptr && captureStopOwner->pending &&
      captureStopOwner->phase != PendingStopPhase::RecordPublished;
  return (presentationOwner != nullptr || finalizing) && viewSetIfPrewarmed() != nullptr;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::serviceFrozenRecordWindow(
    const LoopEventStore::CaptureRange& range, uint64_t expectedGeneration,
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
    uint32_t phase, uint32_t windowTicks, size_t entryQuantum) {
  if (ownsLaunchPreparation() || (viewBuild.coverageOwned() && (viewBuild.active() || completedPromisePendingJudgement))) return false;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || loopLengthTicks == 0 || windowTicks == 0 || entryQuantum == 0 ||
      !range.ready() || range.generation() != expectedGeneration ||
      phase >= loopLengthTicks) {
    return false;
  }
  if (!viewBuild.active()) {
    if (set->lifecycle.build == RuntimePlaybackViewLifecycle::kNoView) return false;
    // The window that contains the playhead, clamped to the loop. `RecordCoverage`
    // applies the record stop projection, so the frozen rows arrive in-loop.
    const uint32_t start = (phase / windowTicks) * windowTicks;
    const uint32_t remaining = loopLengthTicks - start;
    const uint32_t length = remaining < windowTicks ? remaining : windowTicks;
    const PlaybackCoverageInterval coverage{start, length, loopLengthTicks};
    const RuntimePlaybackViewSource source =
        sourceIdentity.origin == PlaybackWindowOrigin::Capture
            ? RuntimePlaybackViewSource::fromFrozenRecordCapture(range, sourceIdentity.captureContentGeneration)
            : RuntimePlaybackViewSource::fromCaptureRange(range);
    if (sourceIdentity.origin == PlaybackWindowOrigin::Capture &&
        sourceIdentity.captureContentGeneration == 0) return false;
    if (!viewBuild.begin(set->views[set->lifecycle.build], source, sourceIdentity, coverage,
                         EmissionCursor{}, LedgerBoundarySummary{},
                         RuntimePlaybackViewFillScope::RecordCoverage)) {
      return false;
    }
  }
  const uint8_t completedRole = set->lifecycle.build;
  if (completedRole >= set->views.size()) return false;
  const uint64_t completedGeneration = set->views[completedRole].storageGeneration;
  const RuntimePlaybackViewBuildGrain grain = viewBuild.nextGrain();
  if (grain == RuntimePlaybackViewBuildGrain::Fill) {
    if (viewBuild.stepFill(entryQuantum) == RuntimePlaybackViewBuildGrain::Denied) return false;
  } else if (grain == RuntimePlaybackViewBuildGrain::Order) {
    if (viewBuild.stepOrder(entryQuantum) == RuntimePlaybackViewBuildGrain::Denied) return false;
  }
  if (viewBuild.preparationCancelled() || bridgeWindowBuild.preparationCancelled()) return false;
  if (viewBuild.nextGrain() != RuntimePlaybackViewBuildGrain::Complete) return true;
  PlaybackSourceReadGuard publicationGuard;
  if (viewBuild.preparationCancelled() || bridgeWindowBuild.preparationCancelled() ||
      viewSetIfPrewarmed() != set || set->lifecycle.build != completedRole ||
      set->views[completedRole].storageGeneration != completedGeneration ||
      !viewBuild.completedDestinationIs(set->views[completedRole])) return false;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  // Cold live-Capture promotion reclaimed the raw retained shell and restored
  // the third prewarmed view as an incomplete ready shell. Its mismatching-stop
  // successor uses the ordinary current/ready/build rotation, not raw prepare.
  if (set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView &&
      set->lifecycle.current < set->views.size() &&
      set->lifecycle.ready < set->views.size()) {
    const auto& current = set->views[set->lifecycle.current];
    if (current.complete && current.published &&
        current.source.origin == PlaybackWindowOrigin::Capture &&
        sourceIdentity.origin == PlaybackWindowOrigin::Capture)
      return set->lifecycle.publishCompletedBuildAsReadySuccessor(set->views);
  }
#endif
  if (!set->publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation)) {
    viewBuild.discard();
    return false;
  }
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::frozenRecordWindowReady() const {
  const RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  return set != nullptr && set->lifecycle.ready != RuntimePlaybackViewLifecycle::kNoView &&
      set->views[set->lifecycle.ready].complete;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const RuntimePlaybackView*
LoopPlaybackRuntime::adoptFrozenRecordWindow(const PlaybackViewSourceIdentity& sourceIdentity,
                                             uint32_t loopLengthTicks, uint32_t phase) {
  PlaybackSourceReadGuard guard;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || loopLengthTicks == 0) return nullptr;
  const auto covers = [&](const RuntimePlaybackView& window) {
    return window.complete && !window.rawCoveragePrepared &&
        window.coverage.loopLengthTicks == loopLengthTicks &&
        phase >= window.coverage.startPhase &&
        phase - window.coverage.startPhase < window.coverage.lengthTicks;
  };
  const auto sameSourceGeneration = [&](const RuntimePlaybackView& window) {
    // A committed root may replace another committed root without changing the
    // copied Record revision. A Capture-origin view, however, has its own frozen
    // content receipt: the legacy committed adopter may not erase that identity.
    return window.source.playbackRevision == sourceIdentity.playbackRevision &&
        window.source.sourceGeneration == sourceIdentity.sourceGeneration &&
        window.source.origin == sourceIdentity.origin &&
        window.source.captureContentGeneration == sourceIdentity.captureContentGeneration;
  };
  // Prefer a complete successor built from the now-current checkpoint. Only if
  // none exists may the current copied Record window be re-identified: revision
  // and source generation must still match, so an overdub mutation cannot make
  // older content masquerade as current.
  if (set->lifecycle.ready < set->views.size()) {
    RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
    if (covers(ready) && ready.source == sourceIdentity) {
      if (!readyPositionAllowsAdoption(currentDeadlineClockForAdoption()) ||
          !set->handoffReadyCoverage(PlaybackViewHandoffMode::Audible)) return nullptr;
      ready.published = true;
      return &set->views[set->lifecycle.current];
    }
  }
  if (set->lifecycle.current < set->views.size()) {
    RuntimePlaybackView& current = set->views[set->lifecycle.current];
    if (!current.firstRecordReturnWindow && covers(current) && sameSourceGeneration(current)) {
      current.source = sourceIdentity;
      current.published = true;
      return &current;
    }
  }
  if (set->lifecycle.ready == RuntimePlaybackViewLifecycle::kNoView ||
      set->lifecycle.ready >= set->views.size()) return nullptr;
  RuntimePlaybackView& window = set->views[set->lifecycle.ready];
  if (!covers(window) || !sameSourceGeneration(window)) return nullptr;
  window.source = sourceIdentity;
  if (!readyPositionAllowsAdoption(currentDeadlineClockForAdoption()) ||
      !set->handoffReadyCoverage(PlaybackViewHandoffMode::Audible)) return nullptr;
  window.published = true;
  return &window;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::frozenWindowBuildActive() const {
  return viewBuild.active() && !viewBuild.coverageOwned();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackViewBuildGrain
LoopPlaybackRuntime::frozenWindowBuildGrain() const {
  return viewBuild.nextGrain();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::releaseFrozenRecordWindow() {
  PlaybackSourceReadGuard guard;
  if (ownsLaunchPreparation() || (viewBuild.coverageOwned() && (viewBuild.active() || completedPromisePendingJudgement))) return true;
  if (viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
      viewBuild.preparationInProgress() || bridgeWindowBuild.preparationInProgress()) return false;
  viewBuild.discard();
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr) return true;
  const auto clearOnward = [&](uint8_t index) {
    if (index >= set->views.size()) return;
    RuntimePlaybackView& window = set->views[index];
    if (window.firstRecordReturnWindow) return;  // the bound bar-1 window is served
    window.resetMetadata();
  };
  clearOnward(set->lifecycle.current);
  clearOnward(set->lifecycle.ready);
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::serviceFirstRecordingBar(
    const Loop& loop, const PlaybackViewSourceIdentity& sourceIdentity,
    uint32_t ticksPerBar, size_t entryQuantum) {
  RuntimePlaybackViewBuild::PreparationTurn primary(viewBuild), bridgeTurn(bridgeWindowBuild);
  if (!primary || !bridgeTurn) return false;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  if (pendingViewRequest_.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture) {
    // VisualCache must not restart or discard the rolling owner's valid private
    // append work. Only destructive/semantic staleness may cancel its borrow.
    cancelStaleLiveCaptureBuildBorrows();
    return false;
  }
  if (auto* candidateSet = viewSetIfPrewarmed()) {
    const uint8_t index = candidateSet->lifecycle.build;
    if (index < candidateSet->views.size() &&
        candidateSet->views[index].source.origin == PlaybackWindowOrigin::Capture &&
        !candidateSet->views[index].published) {
      const bool current = loop.capture.phase == CapturePhase::Record &&
          loop.capture.store.empty() &&
          candidateSet->views[index].source.captureContentGeneration == loop.captureContentGeneration();
      if (!current) {
        viewBuild.discard();
        candidateSet->views[index].resetMetadata();
      }
      // The Track window owner alone owns this private builder. After a
      // superseding input, a later VisualCache turn may resume legacy raw fill.
      return false;
    }
  }
#endif
  if (ownsLaunchPreparation() || (viewBuild.coverageOwned() && (viewBuild.active() || completedPromisePendingJudgement))) return false;
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || ticksPerBar == 0 || entryQuantum == 0 ||
      set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView) return false;
  const uint64_t generation = loop.captureDisplayRevision;
  if (!viewBuild.active() && recordingWindowGenerationValid &&
      recordingWindowGeneration == generation) return false;
  if (!viewBuild.active()) {
    if (set->lifecycle.build == RuntimePlaybackViewLifecycle::kNoView) return false;
    RuntimePlaybackViewSource source;
    source.context = &loop;
    source.size = recordingCaptureSize;
    source.eventAt = recordingCaptureEventAt;
    source.generation = recordingCaptureGeneration;
    source.count = loop.capture.store.size();
    source.generationToken = generation;
    const PlaybackCoverageInterval coverage{0, ticksPerBar, 0};
    if (!viewBuild.begin(set->views[set->lifecycle.build], source, sourceIdentity, coverage,
                         EmissionCursor{}, LedgerBoundarySummary{},
                         RuntimePlaybackViewFillScope::RawCoverage)) return false;
  }
  const uint8_t completedRole = set->lifecycle.build;
  if (completedRole >= set->views.size()) return false;
  const uint64_t completedGeneration = set->views[completedRole].storageGeneration;
  const RuntimePlaybackViewBuildGrain grain = viewBuild.nextGrain();
  if (grain == RuntimePlaybackViewBuildGrain::Fill) {
    if (viewBuild.stepFill(entryQuantum) == RuntimePlaybackViewBuildGrain::Denied) return false;
  } else if (grain == RuntimePlaybackViewBuildGrain::Order) {
    if (viewBuild.stepOrder(entryQuantum) == RuntimePlaybackViewBuildGrain::Denied) return false;
  }
  if (!primary || !bridgeTurn) return false;
  if (viewBuild.nextGrain() != RuntimePlaybackViewBuildGrain::Complete) return true;
  PlaybackSourceReadGuard publicationGuard;
  if (!primary || !bridgeTurn || viewSetIfPrewarmed() != set ||
      set->lifecycle.build != completedRole ||
      set->views[completedRole].storageGeneration != completedGeneration ||
      !viewBuild.completedDestinationIs(set->views[completedRole])) return false;
  if (!set->publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation) ||
      !set->lifecycle.replaceRetainedWithReady(set->views)) {
    viewBuild.discard();
    return false;
  }
  recordingWindowGeneration = generation;
  recordingWindowGenerationValid = true;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackViewBuildGrain
LoopPlaybackRuntime::nextCoverageBuildGrain() const {
  return viewBuild.nextGrain();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackViewBuildGrain
LoopPlaybackRuntime::stepCoverageBuildFill(size_t entryQuantum) {
  if (!coverageBuildNeedsService()) return RuntimePlaybackViewBuildGrain::Denied;
  return viewBuild.stepFill(entryQuantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackViewBuildGrain
LoopPlaybackRuntime::stepCoverageBuildOrder(size_t entryQuantum) {
  if (!coverageBuildNeedsService()) return RuntimePlaybackViewBuildGrain::Denied;
  return viewBuild.stepOrder(entryQuantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardCoverageBuild() {
  retireCoverageSource();
  completedPromisePendingJudgement = false;
  admittedPromiseInvalidated = false;
  // **3.15f / C:** discarding the *build worker* must not clear the **ready** window's positioned handoff -- the
  // ready owns that position until it is adopted or retired. Only a full reset clears it here.
  viewBuildWork = {};
  viewBuild.discard();
}

/// Task 3.7a (I2): drop the retained-source gather and **release the window it gathered**. The demand it serves is not
/// dropped here: a refusal retains it (with its reason) and an admission discharges it, so this only returns the
/// per-slot external buffer and the cursor. The last refusal name is kept, because it is the reason the demand is still
/// owed -- the next gather clears it.
/// Task 3.7a (I3b): drop the page ring and **release its pages' storage**. The ring's pages are fixed-capacity vectors,
/// so the pages themselves are cleared and re-reserved on the next `reset` -- nothing a consumer holds is touched, and a
/// consumed cursor is simply invalid afterwards (the next `reset` stamps a new revision).
TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardPagedWindow() {
  pagedActive = false;
  pagedOutcome = PagedPlaybackOutcome::None;
  pagedRevision = 0;
  pagedInterval = PlaybackCoverageInterval{};
  pagedPagesProduced = 0;
  pagedCursor = WindowPageCursor{};
  // Task 3.7a (I3c-a): the emission state goes with the ring it addressed -- a position without its pages would
  // address ordinals nobody holds any more.
  pagedEmission.unbind();
  // The ring releases its own pages (one owner for the fixed storage), so the slot does not leave `kWindowPageCount`
  // page buffers resident after the preparation is discarded.
  pagedWindow.release();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardRetainedGather() {
  retainedGatherPhase = RetainedGatherPhase::Idle;
  CommittedEventRange::resetWindowTraversalCursor(retainedGatherCursor);
  retainedGatherUnits = 0;
  retainedGatherInspectedChunks = 0;
  if (!retainedGatherEvents.empty() || retainedGatherEvents.capacity() != 0) {
    // Swap rather than `clear()`: the capacity is the window's own external allocation, and a discharged demand must
    // not keep it resident per slot.
    SessionMidiEventVec released;
    retainedGatherEvents.swap(released);
  }
  if (!retainedGatherEditRows.empty() || retainedGatherEditRows.capacity() != 0) {
    // Task 3.7a (I7): the layer scratch is released with the window it was resolving, by the same rule.
    SessionMidiEventVec released;
    retainedGatherEditRows.swap(released);
  }
  if (!previewSourceEvents.empty() || previewSourceEvents.capacity() != 0) {
    // Task 3.15d: the preview composition scratch is the same kind of resident window buffer -- released with the
    // gather it belongs to, so a discharged demand never keeps the interval's capacity per slot.
    SessionMidiEventVec released;
    previewSourceEvents.swap(released);
  }
}

/// DEC-108 §13(a)/(c) / task 3.10 (increment 24): **retain or coalesce the slot's pending preparation demand.** One
/// record per slot, and the **newest** demand wins its identity, interval, phase and bounded cost -- while the
/// **oldest outstanding deadline** of the same operation survives (progress toward the oldest deadline is the thing
/// that may not be thrown away). The deadline is carried over only from the same operation generation, clock epoch and
/// loop geometry: a reset voids the promise's *position* (DEC-108 §13(c)) and a returning slot must not inherit an
/// earlier operation's deadline (DEC-108 §13(d)), so those cases start the record fresh instead of keeping a tick that
/// no longer names anything. A demand retained with no qualified stamp never overwrites a qualified one it coalesces
/// with: an unqualified re-attempt does not un-promise a positioned demand.
TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::retainViewPendingRequest(
    const PlaybackViewSourceIdentity& source, const PlaybackCoverageInterval& coverage, uint32_t requestedPhase,
    uint32_t operationGeneration, const PlaybackViewAdmittedWork& work, uint64_t deadlineTick, uint32_t clockEpoch,
    uint32_t loopLengthTicks, PlaybackViewPendingReason reason) {
  const PlaybackViewPendingRequest& retained = pendingViewRequest_;
  const bool sameOperation = retained.active && retained.operationGeneration == operationGeneration;
  const bool samePosition = sameOperation && retained.clockEpoch != 0 && retained.loopLengthTicks == loopLengthTicks;
  const bool keepRetainedDeadline =
      samePosition &&
      (clockEpoch == 0 || (retained.clockEpoch == clockEpoch && retained.deadlineTick <= deadlineTick));
  // **Every retained field is read out before the record is replaced.** `retained` is an alias *of* the object written
  // below, so a read placed after the reset (or after `work` is copied in) would silently observe the new attempt: the
  // first version of this function did exactly that and handed the retained promise the newest attempt's origin stamp,
  // which is the incoherence the O4 fixture caught.
  const bool keptInvalidation = keepRetainedDeadline && retained.positionInvalidated;
  const uint16_t keptMisses = keepRetainedDeadline ? retained.countedMisses : 0;
  const uint64_t keptDeadlineTick = keepRetainedDeadline ? retained.deadlineTick : deadlineTick;
  const uint32_t keptClockEpoch = keepRetainedDeadline ? retained.clockEpoch : clockEpoch;
  const uint32_t keptLoopLength = keepRetainedDeadline ? retained.loopLengthTicks : loopLengthTicks;
  const uint32_t keptPhase = keepRetainedDeadline ? retained.requestedPhase : requestedPhase;
  const uint64_t keptAdmittedTick = keepRetainedDeadline ? retained.work.admittedTick : work.admittedTick;
  // Nothing of the superseded demand stays readable: the record is replaced whole, so no reader can mix one attempt's
  // deadline with another's identity. The **content** fields (source identity, interval, bounded cost) are the newest
  // attempt's -- a coalesced demand is for the newest window, since a newer window may satisfy the older obligation --
  // while the **position** (deadline, epoch, geometry, origin phase and origin stamp) is the retained promise, kept as
  // ONE coherent set: pairing a deadline from one attempt with an origin stamp from another would describe a traversal
  // that never existed. `work` therefore carries the newest attempt's cost with the retained position's stamps.
  pendingViewRequest_ = PlaybackViewPendingRequest{};
  pendingViewRequest_.active = true;
  pendingViewRequest_.positionInvalidated = keptInvalidation;
  pendingViewRequest_.countedMisses = keptMisses;
  pendingViewRequest_.operationGeneration = operationGeneration;
  pendingViewRequest_.source = source;
  pendingViewRequest_.coverage = coverage;
  pendingViewRequest_.reason = reason;
  pendingViewRequest_.work = work;
  pendingViewRequest_.work.admittedTick = keptAdmittedTick;
  pendingViewRequest_.work.deadlineTick = keptDeadlineTick;
  pendingViewRequest_.work.clockEpoch = keptClockEpoch;
  pendingViewRequest_.deadlineTick = keptDeadlineTick;
  pendingViewRequest_.clockEpoch = keptClockEpoch;
  pendingViewRequest_.loopLengthTicks = keptLoopLength;
  pendingViewRequest_.requestedPhase = keptPhase;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginFrozenCaptureOrder(
    uint32_t loopLengthTicks, uint32_t frozenGeneration) {
  CaptureAdmissionStaging* set = captureStagingIfPrewarmed();
  if (set == nullptr || !set->frozen ||
      set->frozenGeneration != frozenGeneration ||
      set->captureGeneration != frozenGeneration ||
      frozenCaptureCanonicalOrder.active() || frozenCaptureRewrite.active) return false;
  if (frozenCaptureOrderBinding.bound) {
    // The owner checks admission on every turn. A matching in-flight build
    // resumes; neither active nor complete state may silently change source.
    return frozenCaptureOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration) &&
           frozenCaptureOrderBinding.loopLengthTicks == loopLengthTicks &&
           (frozenCaptureOrder.active() || frozenCaptureOrder.complete());
  }
  if (set->openReservationCount() != 0 || !loopLengthTicks) return false;
  ++frozenCaptureOrderStorageGeneration;
  if (!frozenCaptureOrder.begin(set->storage, loopLengthTicks)) return false;
  frozenCaptureOrderBinding = {frozenGeneration, loopLengthTicks, set->storage.eventCount, true, frozenCaptureOrderStorageGeneration};
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const char* frozenCaptureCanonicalOrderBeginName(
    FrozenCaptureCanonicalOrderBegin result) {
  switch (result) {
    case FrozenCaptureCanonicalOrderBegin::Ready: return "ready";
    case FrozenCaptureCanonicalOrderBegin::StagingUnavailable: return "staging unavailable";
    case FrozenCaptureCanonicalOrderBegin::StagingNotFrozen: return "staging not frozen";
    case FrozenCaptureCanonicalOrderBegin::FrozenGenerationMoved: return "frozen generation moved";
    case FrozenCaptureCanonicalOrderBegin::StagingGenerationMoved:
      return "staging generation moved";
    case FrozenCaptureCanonicalOrderBegin::BindingBusy:
      return "order scratch owned by an in-flight build";
    case FrozenCaptureCanonicalOrderBegin::StagingContentMoved:
      return "staging content moved under the binding";
    case FrozenCaptureCanonicalOrderBegin::OpenReservation: return "open reservation";
    case FrozenCaptureCanonicalOrderBegin::BuildDenied: return "canonical capture build denied";
  }
  return "unnamed refusal";
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM FrozenCaptureCanonicalOrderBegin
LoopPlaybackRuntime::beginFrozenCaptureCanonicalOrder(uint32_t frozenGeneration) {
  CaptureAdmissionStaging* set = captureStagingIfPrewarmed();
  if (set == nullptr) return FrozenCaptureCanonicalOrderBegin::StagingUnavailable;
  if (!set->frozen) return FrozenCaptureCanonicalOrderBegin::StagingNotFrozen;
  if (set->frozenGeneration != frozenGeneration)
    return FrozenCaptureCanonicalOrderBegin::FrozenGenerationMoved;
  if (set->captureGeneration != frozenGeneration)
    return FrozenCaptureCanonicalOrderBegin::StagingGenerationMoved;
  if (frozenCaptureCanonicalOrderBinding.bound) {
    if (frozenCaptureCanonicalOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration) &&
        (frozenCaptureCanonicalOrder.active() || frozenCaptureCanonicalOrder.complete()))
      return FrozenCaptureCanonicalOrderBegin::Ready;
    // This is the phase's only re-bind site, so a stale binding has no other way back. Two cases,
    // deliberately separated: a binding that names *this* interval but whose rows, epoch or
    // projection moved describes a real in-flight invalidation and must stay refused (`step` is
    // Denied for the same reason); a binding that names *another* interval describes nothing in
    // flight, so retiring it loses no work and refusing it wedged `CanonicalOrder` forever.
    if (frozenCaptureCanonicalOrderBinding.generation == set->captureGeneration)
      return FrozenCaptureCanonicalOrderBegin::StagingContentMoved;
    if (frozenCaptureCanonicalOrder.active() || frozenCaptureOrder.active() ||
        frozenCaptureRewriteOwner != nullptr)
      return FrozenCaptureCanonicalOrderBegin::BindingBusy;
    discardFrozenCaptureCanonicalOrder();
  }
  if (set->openReservationCount() != 0)
    return FrozenCaptureCanonicalOrderBegin::OpenReservation;
  ++frozenCaptureOrderStorageGeneration;
  if (!frozenCaptureCanonicalOrder.beginCanonicalCapture(set->storage))
    return FrozenCaptureCanonicalOrderBegin::BuildDenied;
  frozenCaptureCanonicalOrderBinding = {frozenGeneration, 0, set->storage.eventCount, true,
                                        frozenCaptureOrderStorageGeneration};
  return FrozenCaptureCanonicalOrderBegin::Ready;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::frozenCaptureCandidateReady(uint32_t frozenGeneration) const {
  const CaptureAdmissionStaging* set = captureStagingIfPrewarmed();
  return set != nullptr && set->frozen &&
         set->frozenGeneration == frozenGeneration &&
         set->captureGeneration == frozenGeneration &&
         frozenCaptureOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration) &&
         set->openReservationCount() == 0 && frozenCaptureOrder.complete();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult
LoopPlaybackRuntime::stepFrozenCaptureOrder(size_t entryQuantum) {
  const auto* set = captureStagingIfPrewarmed();
  if (set == nullptr || !frozenCaptureOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration))
    return RuntimePlaybackOrderBuildResult::Denied;
  return frozenCaptureOrder.step(entryQuantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult
LoopPlaybackRuntime::stepFrozenCaptureCanonicalOrder(size_t entryQuantum) {
  const auto* set = captureStagingIfPrewarmed();
  if (set == nullptr ||
      !frozenCaptureCanonicalOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration))
    return RuntimePlaybackOrderBuildResult::Denied;
  return frozenCaptureCanonicalOrder.step(entryQuantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopEventStoreRewriteResult
LoopPlaybackRuntime::stepFrozenCaptureCanonicalRewrite(LoopEventStore& captureStore,
                                                        uint32_t frozenGeneration,
                                                        size_t eventQuantum) {
  CaptureAdmissionStaging* set = captureStagingIfPrewarmed();
  if (frozenCaptureRewriteIsCopy ||
      (frozenCaptureRewriteOwner && frozenCaptureRewriteOwner != &captureStore) ||
      set == nullptr || !set->frozen ||
      set->frozenGeneration != frozenGeneration ||
      set->captureGeneration != frozenGeneration ||
      !frozenCaptureCanonicalOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration) ||
      !frozenCaptureCanonicalOrder.complete()) {
    return LoopEventStoreRewriteResult::Denied;
  }
  FixedPlaybackEventStorage& source = set->storage;
  const size_t orderedCount = frozenCaptureCanonicalOrder.orderedEventCount();
  if (!frozenCaptureRewrite.active && frozenCaptureRewrite.sourceIndex == 0 &&
      !captureStore.beginRewriteFromOrderedSource(frozenCaptureRewrite, orderedCount)) {
    return LoopEventStoreRewriteResult::Denied;
  }
  frozenCaptureRewriteOwner = &captureStore;
  if (!frozenCaptureRewrite.active) {
    return frozenCaptureRewrite.sourceIndex == orderedCount
               ? LoopEventStoreRewriteResult::Complete
               : LoopEventStoreRewriteResult::Denied;
  }
  return captureStore.stepRewriteFromOrderedSource(frozenCaptureRewrite, source.events,
                                                    source.order, orderedCount, source.eventCount,
                                                    eventQuantum);
}

static_assert(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS * LoopEventStoreConfig::CHUNK_CAPACITY >=
              RuntimePlaybackViewConfig::kCaptureStagingEvents,
              "canonical copy credits must cover every accepted staging row");

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prewarmCanonicalCaptureOutput() {
  return canonicalCaptureOutput.store.empty() &&
      canonicalCaptureOutput.store.prewarmCaptureDirectory() &&
      canonicalCaptureChunks.reserveChunks(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::releaseCanonicalCaptureReservation() {
  if (!canonicalCaptureOutput.store.empty()) return false;
  const size_t available = canonicalCaptureChunks.availableChunks();
  if (available == 0) return true;
  return canonicalCaptureChunks.releaseGrain(available);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canonicalCaptureOutputAdmitted() const {
  return canonicalCaptureOutput.store.empty() && canonicalCaptureOutput.store.reservedAppendReady() &&
      canonicalCaptureChunks.availableChunks() >= PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS;
}

/// S7.5a: this owner's own microsecond source. The device build reads `micros()`; a native build has no device
/// clock at all, and neither a timestamp nor a wall-clock wait can be invented there — so the native value is 0,
/// which the telemetry module reads as "no instant" (a refusal at 0 never completes, so nothing is recorded).
/// The request-to-completion logic itself is exercised natively with injected timestamps
/// (`test_runtime_timing_telemetry`); its real values are device measurements.
static inline uint32_t replenishNowUs() {
#if defined(PIO_UNIT_TEST_NATIVE)
  return 0;
#else
  return micros();
#endif
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::replenishCanonicalCaptureChunk() {
#if RUNTIME_TIMING_ENABLED
  const uint32_t replenishStartUs = replenishNowUs();
#endif
  const auto available = canonicalCaptureChunks.availableChunks();
  const bool belowTarget = available < PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS;
  const bool reserved = belowTarget && canonicalCaptureChunks.reserveChunks(available + 1);
#if RUNTIME_TIMING_ENABLED
  // S7.5a envelope instrument — the replenishment owner's own accounting: one call's work, an attempt that could
  // not top the reserve up although it was below target (a request that could not complete), and the pair that
  // resolves it. The wait is computed by the telemetry module from its own refusal timestamp.
  RUNTIME_TIMING_NOTE_REPLENISH_WORK(replenishNowUs() - replenishStartUs);
  if (belowTarget) {
    if (reserved) {
      RUNTIME_TIMING_NOTE_REPLENISH_COMPLETION(replenishNowUs());
    } else {
      RUNTIME_TIMING_NOTE_REPLENISH_REFUSED(replenishNowUs());
    }
  }
#endif
  return reserved;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopEventStoreRewriteResult
LoopPlaybackRuntime::stepFrozenCaptureCanonicalCopy(uint32_t generation, size_t quantum) {
  auto* staging = captureStagingIfPrewarmed();
  auto& output = canonicalCaptureOutput.store;
  if (!quantum || !staging || !staging->frozen || staging->frozenGeneration != generation ||
      staging->captureGeneration != generation ||
      !frozenCaptureCanonicalOrderBinding.matches(*staging, frozenCaptureOrderStorageGeneration) ||
      !frozenCaptureCanonicalOrder.complete() ||
      (frozenCaptureRewriteOwner && (!frozenCaptureRewriteIsCopy || frozenCaptureRewriteOwner != &output)))
    return LoopEventStoreRewriteResult::Denied;
  const auto count = frozenCaptureCanonicalOrder.orderedEventCount();
  auto& cursor = frozenCaptureRewrite;
  if (!frozenCaptureRewriteOwner) {
    if (!output.empty() || !output.beginRewriteFromOrderedSource(cursor, 0))
      return LoopEventStoreRewriteResult::Denied;
    cursor.targetCount = count;
    frozenCaptureRewriteOwner = &output;
    frozenCaptureRewriteIsCopy = true;
    canonicalCaptureOutput.phase = CapturePhase::Overdub;
  }
  if (!output.ownsRewriteCursor(cursor) || cursor.targetCount != count ||
      output.size() != cursor.sourceIndex) return LoopEventStoreRewriteResult::Denied;
  while (quantum-- && cursor.sourceIndex < count) {
    const auto source = staging->storage.order[cursor.sourceIndex];
    if (source >= staging->storage.eventCount ||
        !output.appendReserved(staging->storage.events[source], canonicalCaptureChunks))
      return LoopEventStoreRewriteResult::Denied;
    ++cursor.sourceIndex;
  }
  cursor.active = cursor.sourceIndex != count;
  return cursor.active ? LoopEventStoreRewriteResult::Pending : LoopEventStoreRewriteResult::Complete;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardFrozenCaptureOrder() {
  ++frozenCaptureOrderStorageGeneration;
  frozenCaptureOrder.discard();
  frozenCaptureOrderBinding = FrozenCaptureOrderBinding{};
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardFrozenCaptureCanonicalOrder() {
  ++frozenCaptureOrderStorageGeneration;
  frozenCaptureCanonicalOrder.discard();
  frozenCaptureCanonicalOrderBinding = FrozenCaptureOrderBinding{};
  frozenCaptureRewrite = LoopEventStoreRewriteCursor{};
  frozenCaptureRewriteOwner = nullptr;
  frozenCaptureRewriteIsCopy = false;
}

void LoopPlaybackRuntimeDeleter::operator()(LoopPlaybackRuntime* runtime) const noexcept {
  if (runtime == nullptr) {
    return;
  }
  runtime->~LoopPlaybackRuntime();
  if (isInExternalMemoryPool(runtime)) {
    extmem_free(runtime);
  } else {
    std::free(runtime);
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime* allocateLoopPlaybackRuntime() {
  void* mem = extmem_malloc(sizeof(LoopPlaybackRuntime));
  if (mem == nullptr) {
    mem = std::malloc(sizeof(LoopPlaybackRuntime));
  }
  if (mem == nullptr) {
    return nullptr;
  }
  return new (mem) LoopPlaybackRuntime();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::clearAllLedgers() {
  // The deliberate whole-track sweep (global transport stop). Per-interval stops use the slot's own
  // `clearLedger()` so a sibling that is still finalizing does not lose its suppression state (S4.1).
  for (auto& rt : runtimeBySlot_) {
    if (!rt) {
      continue;
    }
    rt->clearLedger();
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM uint16_t TrackPlaybackRuntime::slotGeneration(
    uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return 0;
  const LoopPlaybackRuntimePtr& rt = runtimeBySlot_[slotIndex];
  return rt ? rt->workingSetGeneration() : 0;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::ownsLiveCaptureBuildBorrow() const {
  return viewBuild.ownsLiveCaptureBorrow() || bridgeWindowBuild.ownsLiveCaptureBorrow();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::cancelLiveCaptureBuildBorrows() {
  if (viewBuild.ownsLiveCaptureBorrow() || viewBuild.callInProgress()) viewBuild.discard();
  if (bridgeWindowBuild.ownsLiveCaptureBorrow() || bridgeWindowBuild.callInProgress()) bridgeWindowBuild.discard();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::cancelStaleLiveCaptureBuildBorrows() {
  if (viewBuild.ownsLiveCaptureBorrow() && !viewBuild.liveCaptureCurrent()) viewBuild.discard();
  if (bridgeWindowBuild.ownsLiveCaptureBorrow() && !bridgeWindowBuild.liveCaptureCurrent())
    bridgeWindowBuild.discard();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::cancelLiveCaptureBuilds() {
  for (auto& runtime : runtimeBySlot_)
    if (runtime) runtime->cancelLiveCaptureBuildBorrows();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::cancelStaleLiveCaptureBuilds() {
  // All source Loops are still physically retained by this runtime's Track.
  for (auto& runtime : runtimeBySlot_)
    if (runtime) runtime->cancelStaleLiveCaptureBuildBorrows();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM WorkingSetOutcome TrackPlaybackRuntime::releaseSlot(
    uint8_t slotIndex, uint16_t generation, bool preserveLedger) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return WorkingSetOutcome::NotHeld;
  LoopPlaybackRuntimePtr& rt = runtimeBySlot_[slotIndex];
  if (!rt) return WorkingSetOutcome::NotHeld;
  // Identity first: a stale generation is a rejected no-op, and this is the check that makes a late
  // release on a reused slot harmless (S4.2). The release call itself re-validates and bumps.
  std::optional<RuntimePlaybackViewBuild::PreparationTurn> primary, bridge;
  {
    PlaybackSourceReadGuard guard;
    if (rt->workingSetGeneration() != generation) return WorkingSetOutcome::StaleGeneration;
    if (rt->ownsLiveCaptureBuildBorrow() || rt->viewBuild.callInProgress() ||
        rt->bridgeWindowBuild.callInProgress() || rt->viewBuild.preparationInProgress() ||
        rt->bridgeWindowBuild.preparationInProgress()) return WorkingSetOutcome::RefusedBusy;
    primary.emplace(rt->viewBuild);
    bridge.emplace(rt->bridgeWindowBuild);
  }
  // The same exact capabilities span reset and lease retirement, with IRQs restored.
  if (rt->ownsLaunchPreparation() || !rt->reset(preserveLedger, &*primary, &*bridge) ||
      rt->ownsCoverageSource()) return WorkingSetOutcome::RefusedBusy;
  const WorkingSetOutcome released = rt->releaseAllWorkingSetLeases(&*primary, &*bridge);
  return released;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime* TrackPlaybackRuntime::trySlot(
    uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return nullptr;
  }
  LoopPlaybackRuntimePtr& rt = runtimeBySlot_[slotIndex];
  if (!rt) {
    rt.reset(allocateLoopPlaybackRuntime());
  }
  return rt.get();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime* TrackPlaybackRuntime::slotIfAllocated(
    uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return nullptr;
  }
  return runtimeBySlot_[slotIndex].get();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const LoopPlaybackRuntime* TrackPlaybackRuntime::slotIfAllocated(
    uint8_t slotIndex) const {
  return const_cast<TrackPlaybackRuntime*>(this)->slotIfAllocated(slotIndex);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime& TrackPlaybackRuntime::slot(uint8_t slotIndex) {
  // Hot path: runtime must already exist (boot/prewarm/trySlot). Deferred/cold paths
  // must use trySlot — never embed a static LoopPlaybackRuntime fallback (sparse
  // ActiveNoteLedger still must not live in RAM1; session_20260718_235713 null-deref was alloc failure).
  LoopPlaybackRuntime* runtime = trySlot(slotIndex);
  return *runtime;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const LoopPlaybackRuntime& TrackPlaybackRuntime::slot(
    uint8_t slotIndex) const {
  return const_cast<TrackPlaybackRuntime*>(this)->slot(slotIndex);
}

// ---------------------------------------------------------------------------
// S3.0 placement pins for the per-slot working set (runtime-working-set-ownership).
//
// The change sits on the RAM1 FlexRAM bank edge: one crossed bank costs 32,768 B
// and drops the stack into the documented silent boot hang, so a layout regression
// must be a build error and not a linker surprise. Re-measure with
// `template <size_t N> struct Probe; Probe<sizeof(X)> p;` before changing a number,
// and record the RAM1 line in the change's review artifact. Device-only: pointer
// width differs natively. `Track`'s own pins (and the private latch/ring types) live
// in `Track::placementPins()`, a member context.
// ---------------------------------------------------------------------------
// Configuration-specific: see the note in `Track::placementPins()`. The measured values are for
// `teensy41-capture-serial`; the configuration-independent guard is the post-link budget check.
#if defined(__IMXRT1062__) && defined(SESSION_CAPTURE) && defined(PERF_TELEMETRY)
// Record closure output adds an owner+generation cursor receipt: measured +8 B.
// S4f.19 bounded retirement wait: measured +8 B (a 3-byte `uint16_t` count + `uint8_t` phase pair,
// 8-byte aligned) for the frozen-window wait's progress count and phase. It cannot live on `Track`
// -- that is the pinned eight-way DTCM array (`sizeof(Track) == 384`), so a by-value member costs
// 8x there -- and the wait must be bounded: a build no writer can advance otherwise parks the job,
// its raw range and the capture-stop owner, and refuses every later stop on the track.
// The interval-qualified canonical-release diagnostic adds one externally allocated 8-byte identity
// field to the per-slot runtime; keep the target-ABI pin explicit.
// Slice 3.6 re-based this pin: the slot now holds its **retained sources** in explicit roles (serving / candidate /
// retiring) — two entries, each an identity, descriptor pins and its own store references, so a retained revision
// survives both the pass-level reclamation consumer and `Loop::resetPassTimeline`. DEC-108 item 4 charges that cost
// to the envelope, and it is one deliberate re-base, recorded, exactly as DEC-107 recorded 6,352 -> 6,360.
// The pins are **per ABI**: the holders' pin lists are pointer-sized, so the target (32-bit) holds +144 B over the
// pre-slice figure (6,368 -> 6,512: +96 B retained sources, +24 B lag accounting, +24 B interval index) while the host
// holds +176 B (6,544), its larger slack absorbing the later members. The **target** figure is the one charged.
// Slice 3.13 re-based the **target** pin again, and the reason is recorded here as the rule requires: the preview lane's
// three fields (`previewSourceRevision`, `previewSourceActive`, `previewHandoffOwed`) are only 8 B of payload, but they
// land before members whose alignment then moves -- 6,512 -> **6,624** (**+112 B**, measured with the temporary
// compile-time probe, which was removed again). The **host** figure is unchanged at 6,544 (its slack absorbed them), and
// this state is extmem per-slot storage: it costs no RAM1 `variables` and no DTCM. The delta is a deliberate, recorded
// re-base, exactly as DEC-107 recorded 6,352 -> 6,360, slice 3.6 recorded the one above, and task 3.10's review step 1
// recorded 6,624 -> 6,640 for the **admitted-work record** retained where a window build begins. The host-ABI figure
// below is **unchanged** at 6,544: the host layout had 16 B of tail padding there, so the same field costs the host
// nothing -- which is why only the target pin moves.
// Absolute deadline clock identity = 6,680 B; the retained pending preparation demand (increment 24) added +112 B
// (6,680 -> 6,792), and the deadline service (increment 27, blocker 3's half 2) adds +24 B: **compiler size probe,
// 6,792 -> 6,816** -- the promise's interval length (4 B) plus the per-promise traversal bookkeeping (an 8-byte key and
// a 2-byte count, 8-byte aligned). Per-slot external storage: no RAM1 `variables`, no DTCM.
// Bounded snapshot retry owns one <=3-membership cursor and a frozen request:
// measured target 6,832 -> 7,072 (+240 B), host 8,784 -> 9,096 (+312 B).
// Eight target runtimes add 1,920 external bytes; no event buffer or RAM1 rebase.
// Gated rolling frame: compiler probe measured7,072 ->7,120 (+48 external bytes).
// Task 3.7a (I2): the **retained-source gather** adds its cursor, phase, slice counters, the refusal name and the
// gathered window's vector header -- per-slot external state, no RAM1 `variables`, no DTCM. Measured +72 B on the host
// ABI (9,144 -> 9,216) and +40 B on the target ABI (7,120 -> 7,160): the cursor is smaller there and the
// vector header is pointer-sized. Both re-bases are compiler-measured, not estimated.
// Task 3.7a (I3b): the **paged legacy path** adds one `WindowPagedStream` (three fixed pages, each a vector header plus
// revision/sequence/published, the traversal cursor, the consumer cursor, the interval and the outcome name) --
// per-slot external state again, no RAM1 `variables` and no DTCM. Measured +152 B on the target ABI (7,160 -> 7,312)
// and +232 B on the host (9,216 -> 9,448): the difference is the two ABIs' vector headers and padding, and both figures
// are compiler-measured by the probe this commit removes, not estimated. The page *buffers* are not part of this: they
// are allocated by `reset` when a preparation begins and released by `release`/`discardPagedWindow`.
// Task 3.7a (I3c-a) re-base: +48 B for the paged **emission** state -- the emitted ordinal, the pass base, the pass
// and dry accounting and the two flags. Deliberately **not** a staging buffer: the adapter reads the ring's own pages
// by stream ordinal, so an 8-event stage (192 B here) was written, measured and removed. Compiler-measured.
// Task 3.7a (I7) re-base: +16 B for the caller-owned **layer scratch** the per-layer edit resolution stages the current
// layer in (one `SessionMidiEventVec` header; the events themselves are its external allocation, taken and released with
// the window). Measured by the compiler on this ABI, not estimated.
// E2 Capture source receipt plus bounded source-index fill state; externally
// allocated per slot, not a Track DTCM field.
// I5 raw-prefix token: compiler-measured +8 B in each embedded store.
// Price: SOURCE-STABILITY-EXECUTION.md; no cursor embedded or RAM1 budget change.
// Exact destination generations add16 external runtime bytes across two builders.
// Live recipe receipts add64 external bytes across pending/frozen requests.
// Measured ARM ABI; no Track field or RAM1 budget change.
// DEC-136 completion (S7.5b.5 release edge): the root-return retirement hold's identity key
// and grain count add 8 B (u32 key + u16 grains, this ABI's padding). Externally allocated
// per-slot runtime state; no Track field or RAM1 budget change.
// DEC-147 bound: the refused-span attempt counter fits the rolling frame's existing padding, so both
// pins are unchanged (7,896 target / 10,080 host); compiler measured, no RAM1 budget change.
// Task 3.15d: the preview interval composition scratch (one vector header) adds 8 B on the target ABI
// and 24 B on the host ABI (7,896 -> 7,904 target, 10,080 -> 10,104 host).
// Task 3.15f/C: the ready-carried positioned handoff (uint64 deadline + uint32 epoch + bool) adds 16 B on both
// ABIs (7,904 -> 7,920 target, 10,104 -> 10,120 host); the runtime stays externally allocated per slot and no Track
// field or RAM1 budget changes.
// 2.1: removing the prepared pair descriptor + counters shrinks the same runtime (7,920 -> 7,768 target,
// 10,120 -> 9,896 host), compiler measured; the pair's fixed event storage was external, so RAM1 is unchanged
// apart from the external-pool accounting.
// deferred-canonical-convergence 2.1: the per-slot deferred-convergence obligation adds 40 B on this
// ABI (7,768 -> 7,808; compiler measured with the probe): a packed 2+2 state byte, the target committed
// generation, the loop coverage, and the bound authoritative source identity (24 B on the target ABI's
// 4-byte pointers). The runtime is extmem-allocated per slot and no `Track` field grows, so RAM1 is
// unchanged -- the cost is external-pool bytes, re-based here explicitly as the architectural price.
// task 6.4c (DEC-177): the committed-history certification splits the prepared prefix's identity from its window
// identity, and adds 16 B on this ABI (7,808 -> 7,824; compiler measured): two counts + the certified flag + a
// report-only resume counter. The runtime is extmem-allocated per slot, so no `Track` field and no RAM1 byte grows;
// the cost is external-pool bytes, re-based here explicitly as the architectural price.
#if REVISION_MANIFEST_READER
// C5.3 (DEC-246 amendment 22): 7,848 -> 8,024 B (+176) on the flag-1 probe, compiler measured: the manifest identity in
// every view source identity the runtime holds. Extmem per slot, no RAM1 byte; the default image (flag 0) is unchanged.
// DEC-246 amendment 27: + the quiescent price refusal (`PriceRefusal`), 8,024 -> 8,088 (+64) here and 7,848 -> 7,904
// (+56) on the default image, compiler measured; extmem per slot, RAM1 unchanged.
static_assert(sizeof(LoopPlaybackRuntime) == 8088,
              "I5/C5.3: target per-slot runtime with the manifest identity in each view source identity");
#else
static_assert(sizeof(LoopPlaybackRuntime) == 7904,
              "I5: target per-slot runtime measured after the committed-history certification");
#endif
// CONTRACTS.md C3 (P-B): the private-predecessor source cursor moves the shell 1,008 -> 1,784 B
// (compiler measured on `teensy41-capture-serial`). The cursor is interval-qualified state, and it
// lives here rather than in the custody receipt because that receipt's size is contract-pinned by C2.1
// (120 B) and `Track::placementPins` pins `sizeof(CaptureStopJobs)`. Shells are `extmem_malloc`ed, so
// this is external-pool bytes (eight shells per allocated Track), not RAM1.
static_assert(sizeof(Loop) == 2288,
              "I5/C3: LoopPool shell 1,888 B including the private-predecessor source cursor "
              "(external-first, eight shells per allocated Track); 1,784 -> 1,848 in the review's "
              "bound-cost fix: the open-pair table carries each pair's staged index (+64); "
              "1,848 -> 1,856 in `realtime-idle-owner-admission-bound` 4.3: the span cache records the "
              "interval its notes actually cover (`coverageStart`/`coverageLength`, +8), which is what "
              "lets the windowed derive be reused instead of re-derived and prevents stale coverage "
              "from being served; 1,856 -> 1,888 in task 4.4: the per-slot touched-coverage tile state "
              "(`CoverageState`, +32 with alignment), which cannot live in `Track` (its 384-byte DTCM pin "
              "forbids a member); 1,888 -> 1,944 in task 4.6/4.7: the worker's own resumable cursor "
              "(`TilePrepCursor` + tile + generation, +56 by the compiler's measurement), which is "
              "per-slot worker state and must survive turns; 1,944 -> 2,288 in tasks 4.7/4.8: the bounded "
              "deferred overlap completions (16 x 20 B = 320) and the closure pad (+4, with alignment), "
              "both per-slot worker state. Shells are extmem_malloc'ed, so this is external-pool bytes, not RAM1.");
static_assert(sizeof(TrackPlaybackRuntime) == 32,
              "S3.0: TrackPlaybackRuntime is the eight per-slot pointers; no second "
              "per-track authority may hide here");
#endif
// ---------------------------------------------------------------------------
// **Host-ABI pin.** It was **unreachable** before this slice: the host assertion sat in the `#else` of a branch whose
// `#if` already required `__IMXRT1062__`, so no configuration ever compiled it -- and the figure it carried (6,544 B)
// was stale as well as unchecked: measured on the clean tree at the previous commit, the host object is **8,624 B**,
// not 6,544. The phrase "the host pin is unchanged" in the earlier records therefore described an assertion no
// compiler ever evaluated. It is live from here, and this slice's measured host delta is **+120 B** (8,624 -> 8,744)
// against the target's +112 B: the same record, with the host's 64-bit pointers and its own padding. Native-only --
// the target figure above is the one charged to RAM1, and the target is the shipped ABI.
#if defined(PIO_UNIT_TEST_NATIVE)
// Gated rolling episode adds48 external bytes including the physical view token: measured9,096 ->9,144.
// Task 3.7a (I2) re-base: +72 B for the retained-source gather (cursor 32 + window vector header 24 + phase/units/
// chunks/refusal 16, with padding). Measured by the compiler on this ABI, not estimated.
// Task 3.7a (I3b) re-base: +232 B for the paged ring (three page headers with their 64-bit vector headers, the two
// cursors, the interval and the outcome name, with padding). Measured by the compiler on this ABI, not estimated.
// Task 3.7a (I3c-a) re-base: +40 B for the paged **emission** state (the same fields, this ABI's padding). The
// staging variant measured +296 B here and was removed in favour of reading the ring's pages by ordinal.
// Task 3.7a (I7) re-base: +24 B for the same layer scratch on this ABI (9,528 -> 9,552). Compiler-measured here too.
// E2 source-index fill receipt adds 32 B to the host per-slot runtime
// (9,736 -> 9,768); no Track member or additional view.
// Call cancellation fits target padding; two host preserve pointers add16 B.
// Exact destination generations add16 host bytes across the two builders.
// Live recipe receipts add64 host bytes; compiler measured with production headers.
// DEC-136 completion (S7.5b.5 release edge): root-return retirement hold key + grains add 8 B
// on this ABI as well (10,056 -> 10,064); compiler measured, same external-pool caveat.
// DEC-147 bound: refused-span attempt counter fits existing padding (10,080 unchanged); compiler measured.
// Task 3.15d: the preview interval composition scratch (one vector header) adds 24 B on this ABI
// (10,080 -> 10,104); the capacity it grows is the external pool's, not the runtime's, and the runtime is
// extmem-allocated per slot.
// 2.1: removing the prepared pair descriptor + its counters shrinks the host runtime 10,120 -> 9,896 B
// (compiler measured); the pair's fixed event storage was external, so RAM1 pins are re-baselined below.
// deferred-canonical-convergence 2.1: the per-slot deferred-convergence obligation (packed 2+2 state
// byte, target generation, loop coverage, bound source identity) adds 48 B on this ABI
// (9,896 -> 9,944 host; compiler measured). The runtime is extmem-allocated per slot, so RAM1 is
// unchanged and no `Track` field grows.
// task 6.4c (DEC-177): 9,944 -> 9,960 host (+16 B), compiler measured; extmem per slot, RAM1 unchanged.
#if REVISION_MANIFEST_READER
// C5.3 (DEC-246 amendment 22): 9,984 -> 10,160 host (+176 B), compiler measured: the manifest identity (4 B, 8 with
// this ABI's padding) in every view source identity the runtime holds. Extmem per slot; the default device image does
// not carry the field (flag 0).
// DEC-246 amendment 27: + the quiescent price refusal (`PriceRefusal`), 10,160 -> 10,232 host (+72 B), compiler
// measured; extmem per slot, RAM1 unchanged.
static_assert(sizeof(LoopPlaybackRuntime) == 10232,
              "I5/C5.3: host per-slot runtime with the manifest identity in each view source identity");
#else
static_assert(sizeof(LoopPlaybackRuntime) == 9984,
              "I5: host per-slot runtime measured after the committed-history certification");
#endif
#endif
// (the temporary compile-time size probe that measured both figures above has been removed)
