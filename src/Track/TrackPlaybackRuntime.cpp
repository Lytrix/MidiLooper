//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackPlaybackRuntime.h"
#include "Loop.h"

#include <cstdlib>
#include <limits>
#include <new>

#include "Utils/ExternalPoolAccounting.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/IntervalProjection.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define TRACK_PLAYBACK_RUNTIME_COLD_MEM FLASHMEM
#else
#define TRACK_PLAYBACK_RUNTIME_COLD_MEM
#endif

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
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool PendingStopAdmission::retireCompletedPreparation() {
  if (!pending || remainingClosureGrains) return false;
  switch (phase) {
    case PendingStopPhase::PrepareTransfer: remainingOrderGrains = 0; break;
    case PendingStopPhase::CompanionHistory: remainingCanonicalPreparationGrains = 0; break;
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

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::reset(bool preserveLedger) {
  if (captureStopOwner && captureStopOwner->pending) {
    captureStopOwner->requestTransportStop();
    if (auto* staging = captureStagingIfPrewarmed()) {
      (void)canonicalPassInputs.retargetFrozenCommandEpoch(
          captureStopOwner->transportCommandEpoch, *staging);
    }
    return false;
  }
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
  recordingWindowGeneration = 0;
  recordingWindowGenerationValid = false;
  discardFrozenCaptureOrder();
  discardFrozenCaptureCanonicalOrder();
  if (viewSet != nullptr) {
    if (preservedIndex != RuntimePlaybackViewLifecycle::kNoView)
      viewSet->resetMetadataPreserveView(preservedIndex);
    else
      viewSet->resetMetadata();
  }
  if (!preserveLedger) {
    cachedLoopRevision = 0;
    cachedTrackGeneration = 0;
    ledger.clear();
  }
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime::~LoopPlaybackRuntime() {
  canonicalCaptureOutput.store.clear();
  releaseRuntimePlaybackViewSet(viewSet);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prewarmCaptureStaging() {
  if (viewSet == nullptr) {
    viewSet = allocateRuntimePlaybackViewSet();
  }
  return viewSet != nullptr && viewSet->liveCaptureStaging().prewarm() &&
      viewSet->frozenCaptureStaging().prewarm();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prewarmViews(
    size_t companionRowCapacity) {
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCaptureStaging);
  const bool stagingReady = prewarmCaptureStaging();
  ExternalPoolAccounting::endTag();
  if (!stagingReady) return false;
  if (!viewSet->prewarmed && (viewSet->frozenCaptureStaging().frozen ||
      viewSet->frozenCaptureStaging().storage.eventCount ||
      viewSet->frozenCaptureStaging().openReservationCount() ||
      viewSet->liveCaptureStaging().frozen ||
      viewSet->liveCaptureStaging().storage.eventCount ||
      viewSet->liveCaptureStaging().openReservationCount()))
    return false; // Upgrading must never reset accepted staging state.
  if (!viewSet->prewarm()) return false;
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
  if (!set || !baseline.uneditedBaseline(sourceIdentity) || !coverage.isValid() ||
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
    size_t oracleCount, const PlaybackCoverageInterval& coverage) {
  if (!viewSetIfPrewarmed() || !coverage.isValid() || coverage.startPhase != 0 ||
      coverage.lengthTicks != coverage.loopLengthTicks || live.loopLengthTicks != coverage.loopLengthTicks ||
      oracleCount > RuntimePlaybackViewConfig::kEventsPerView || (oracleCount && !oracle) ||
      canonicalPrefixPhase != CanonicalPrefixPhase::Idle || canonicalTailPhase != CanonicalTailPhase::Idle ||
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
  if (canonicalPrefixPhase == CanonicalPrefixPhase::Idle) return canonicalPassInputs.idle() && frozenCompanionApply.available();
  if (canonicalPassInputs.generation() != canonicalPrefixGeneration) return false;
  return canonicalPrefixPasses != &passes || !(canonicalPrefixSource == source) ||
      canonicalPrefixLength != loopLength || canonicalPrefixStorageOwner != &storageGeneration ||
      storageGeneration != canonicalPrefixStorageGeneration ||
      canonicalPrefixPhase == CanonicalPrefixPhase::Preparing;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackOrderBuildResult LoopPlaybackRuntime::stepCanonicalPrefix(
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const RuntimeCanonicalPassInputs::CurrentSource& live, size_t quantum) {
  if (!quantum || !viewSetIfPrewarmed() || captureBridgePhase != CaptureBridgePhase::Idle ||
      canonicalPassInputs.captureAttached() || source.playbackRevision != live.playbackRevision ||
      source.sourceGeneration != live.sourceGeneration) return RuntimePlaybackOrderBuildResult::Denied;
  if (canonicalPrefixPhase != CanonicalPrefixPhase::Idle) {
    if (canonicalPassInputs.generation() != canonicalPrefixGeneration) return RuntimePlaybackOrderBuildResult::Denied;
    if (canonicalPrefixPasses != &passes || !(canonicalPrefixSource == source) || canonicalPrefixLength != live.loopLengthTicks ||
        canonicalPrefixStorageOwner != &live.storageGeneration ||
        live.storageGeneration != canonicalPrefixStorageGeneration) {
      if (canonicalPrefixSource.playbackRevision == source.playbackRevision &&
          canonicalPrefixSource.snapshotIdentity != source.snapshotIdentity &&
          resumeCanonicalAfterAppend(passes, source.playbackRevision, source, live))
        return RuntimePlaybackOrderBuildResult::Pending;
      SC_ODUB_STOP_STAGE("prefix_restart", 0, 0, UINT32_MAX, UINT32_MAX,
          canonicalPassInputs.completedPassCount(), source.playbackRevision,
          live.storageGeneration != canonicalPrefixStorageGeneration ? "history_storage_changed" : "source_changed");
      discardCanonicalWindow();
      canonicalPassInputs.discard(); canonicalPrefixPhase = CanonicalPrefixPhase::Idle;
      canonicalPrefixPasses = nullptr;
      return RuntimePlaybackOrderBuildResult::Pending; // Restart on a later owner turn.
    }
    if (canonicalPrefixPhase == CanonicalPrefixPhase::Denied) return RuntimePlaybackOrderBuildResult::Denied;
    const auto result = canonicalPassInputs.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Complete) canonicalPrefixPhase = CanonicalPrefixPhase::Ready;
    else if (result == RuntimePlaybackOrderBuildResult::Denied) canonicalPrefixPhase = CanonicalPrefixPhase::Denied;
    return result;
  }
  if (!canonicalPassInputs.idle() || !frozenCompanionApply.available()) return RuntimePlaybackOrderBuildResult::Denied;
  canonicalPrefixSource = source; canonicalPrefixPasses = &passes; canonicalPrefixLength = live.loopLengthTicks;
  canonicalPrefixStorageOwner = &live.storageGeneration;
  canonicalPrefixStorageGeneration = live.storageGeneration;
  RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
    frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
  const bool started = canonicalPassInputs.beginCurrent(frozenCompanionApply, work, passes, canonicalPrefixSource, live);
  canonicalPrefixGeneration = canonicalPassInputs.generation();
  canonicalPrefixPhase = started ? CanonicalPrefixPhase::Preparing : CanonicalPrefixPhase::Denied;
  return started ? RuntimePlaybackOrderBuildResult::Pending : RuntimePlaybackOrderBuildResult::Denied;
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
  SC_ODUB_STOP_STAGE("prefix_reuse", 0, 0, UINT32_MAX, UINT32_MAX,
      canonicalPassInputs.completedPassCount(), source.playbackRevision, "committed_normalization");
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::resumeCanonicalAfterAppend(
    const LoopPasses& passes, uint32_t priorRevision,
    const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
    bool committedFrozenTail) {
  // The token address remains stable for the borrowed worker. Failed reuse
  // leaves normal source invalidation responsible for a bounded restart.
  const auto previous = canonicalPrefixSource;
  canonicalPrefixSource = source;
  if (!canonicalPassInputs.resumeCurrentAfterAppend(passes, priorRevision,
          canonicalPrefixSource, live, committedFrozenTail)) {
    canonicalPrefixSource = previous;
    return false;
  }
  discardCanonicalWindow();
  canonicalPrefixGeneration = canonicalPassInputs.generation();
  canonicalPrefixPhase = CanonicalPrefixPhase::Preparing;
  canonicalPrefixPasses = &passes; canonicalPrefixLength = live.loopLengthTicks;
  canonicalPrefixStorageOwner = &live.storageGeneration;
  canonicalPrefixStorageGeneration = live.storageGeneration;
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
    viewBuild.discard();
    RuntimeCompanionApply::Work work{frozenCompanionPairLookup, frozenCompanionDeleteLookup,
      frozenCompanionSpans, frozenCompanionReconstruction, frozenCompanionOverlaps};
    if (!frozenCompanionPasses.begin(frozenCompanionApply, work, canonicalPassInputs, nullptr, 0,
                                    canonicalPrefixSource, canonicalPrefixLength))
      return RuntimePlaybackOrderBuildResult::Denied;
    canonicalWindowIndex = index;
    canonicalWindowIsBridgeBase = bridgeBase;
    canonicalWindowPassGeneration = frozenCompanionPasses.generation();
    canonicalWindowCoverage = coverage;
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
      canonicalWindowPhase = CanonicalWindowPhase::Denied;
      return result;
    }
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      LedgerBoundarySummary boundary; boundary.capture(ledger);
      if (!viewBuild.begin(view, frozenCompanionPasses, canonicalPrefixSource, coverage, anchor, boundary)) {
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
  if (canonicalPassInputs.generation() == canonicalTailInputGeneration) {
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
  auto* set = viewSetIfPrewarmed();
  if (!set || !windowTicks || !quantum || set->bridgeLifecycle.selected >= set->bridges.size()) return false;
  const auto index = set->bridgeLifecycle.selected;
  const auto& bridge = set->bridges[index];
  const auto* base = fullLoopBridgeBase(bridge, bridge.coverage.loopLengthTicks);
  if (!base || !bridge.complete || !bridge.selected || !bridge.streamPrepared ||
      currentPhase >= bridge.coverage.loopLengthTicks) return false;
  const uint32_t length = bridge.coverage.loopLengthTicks;
  const uint32_t width = std::min(windowTicks, length);
  if (bridgeWindowContextIndex != index || bridgeWindowContextGeneration != bridge.storageGeneration) {
    bridgeWindowBuild.discard();
    bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
    // A source handoff invalidates old emission windows, never the selected
    // immutable bridge/base. That compiled stream covers refill immediately.
    for (auto& view : set->views) view.resetMetadata();
    set->lifecycle.reset();
    bridgeWindowContextIndex = index;
    bridgeWindowContextGeneration = bridge.storageGeneration;
    return true;
  }
  auto& life = set->lifecycle;
  const uint64_t now = uint64_t(bridgeProjectionCycle) * length + currentPhase;
  const auto endOf = [length](const RuntimePlaybackView& view) {
    return uint64_t(view.cursorAnchor.projectionCycle) * length +
        view.coverage.startPhase + view.coverage.lengthTicks;
  };
  if (life.ready < set->views.size() && set->views[life.ready].complete) {
    const auto& ready = set->views[life.ready];
    if (ready.cursorAnchor.projectionCycle == bridgeProjectionCycle && ready.coverage.contains(currentPhase)) {
      (void)set->handoffReadyCoverage();
      return true;
    }
    if (endOf(ready) <= now) {
      set->views[life.ready].resetMetadata();
      if (bridgeWindowBuildIndex == life.ready) {
        bridgeWindowBuild.discard(); bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
      }
      return true;
    }
  }
  if (bridgeWindowBuildIndex < set->views.size()) {
    auto& target = set->views[bridgeWindowBuildIndex];
    if (endOf(target) <= now) {
      bridgeWindowBuild.discard(); target.resetMetadata();
      bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
      return true;
    }
    const auto grain = bridgeWindowBuild.nextGrain();
    if (grain == RuntimePlaybackViewBuildGrain::Fill) bridgeWindowBuild.stepFill(quantum);
    else if (grain == RuntimePlaybackViewBuildGrain::Order) bridgeWindowBuild.stepOrder(quantum);
    else if (grain == RuntimePlaybackViewBuildGrain::Complete) {
      if (life.ready == RuntimePlaybackViewLifecycle::kNoView)
        (void)set->publishCompletedBuildAsReady();
      // Ready/build successors are complete but not yet published. Release
      // only the worker; clearing the destination rebuilds it forever and
      // starves snapshot-receipt cleanup (and therefore overdub reentry).
      bridgeWindowBuild.discard(&target);
      bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
    } else {
      bridgeWindowBuild.discard(); target.resetMetadata();
      bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
    }
    return true;
  }
  const uint8_t destination = life.ready < set->views.size() && !set->views[life.ready].complete
      ? life.ready : life.build;
  if (destination >= set->views.size() || set->views[destination].complete ||
      destination == life.current || destination == life.retained) return false;
  uint64_t start = uint64_t(bridgeProjectionCycle) * length + currentPhase / width * width;
  if (life.ready < set->views.size() && set->views[life.ready].complete)
    start = endOf(set->views[life.ready]);
  else if (publishedBridgeWindow(bridge.source, currentPhase))
    start = endOf(set->views[life.current]);
  const uint32_t phase = start % length;
  const uint32_t cycle = start / length;
  const uint32_t completed = cycle == bridge.cursorAnchor.projectionCycle && bridge.preparedAdmissionPhase
      ? bridge.preparedAdmissionPhase - 1 : UINT32_MAX;
  if (!bridgeWindowSource.selectAfterPhase(*base, bridge, cycle, completed)) return false;
  const PlaybackCoverageInterval coverage{phase, std::min(width, length - phase), length};
  const EmissionCursor anchor{cycle, phase, 0, 0, bridge.source.sourceGeneration};
  if (!bridgeWindowBuild.begin(set->views[destination], bridgeWindowSource.source(), bridge.source,
      coverage, anchor, bridge.ledgerBoundary, RuntimePlaybackViewFillScope::PlaybackCoverage)) return false;
  bridgeWindowBuildIndex = destination;
  return true;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginCoverageBuild(
    const CoverageServiceContract& contract, uint32_t currentPhase, uint32_t handoffPhase,
    const MidiEvent* source, size_t sourceCount, const PlaybackViewSourceIdentity& sourceIdentity,
    const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
    const LedgerBoundarySummary& ledgerBoundary) {
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || viewBuild.active() || set->lifecycle.build == RuntimePlaybackViewLifecycle::kNoView ||
      !contract.canCompleteBeforeHandoff(currentPhase, handoffPhase, coverage.loopLengthTicks)) {
    return false;
  }
  RuntimePlaybackView& destination = set->views[set->lifecycle.build];
  return viewBuild.begin(destination, source, sourceCount, sourceIdentity, coverage, cursorAnchor,
                         ledgerBoundary, RuntimePlaybackViewFillScope::PlaybackCoverage);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::prepareFirstRecordingBar(
    uint32_t ticksPerBar) {
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

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::releaseFirstRecordingBarReturnWindow() {
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
    // The bundle includes its cursor. The launch phase is where the live take
    // stopped, so the notes below it were already heard: select the first event
    // after it instead of letting the emission replay the window as a loop-start
    // catch-up (which dumped the already-played prefix in bursts). This mirrors
    // `reanchorPlaybackIndex` for the merged path at the same boundary.
    size_t cursor = 0;
    while (cursor < window.storage.eventCount) {
      const MidiEvent& event = window.storage.events[window.storage.order[cursor]];
      if (IntervalProjection::playbackEventPhase(event.tick, loopLengthTicks) > phase) {
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
    uint32_t loopLengthTicks) {
  RuntimePlaybackViewSet* set = viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView ||
      set->lifecycle.retained >= set->views.size()) {
    return false;
  }
  RuntimePlaybackView& retained = set->views[set->lifecycle.retained];
  if (!retained.bindRawCoverage(loopLengthTicks)) return false;
  return set->lifecycle.handoffToRetained(set->views);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::serviceFirstRecordingBar(
    const Loop& loop, const PlaybackViewSourceIdentity& sourceIdentity,
    uint32_t ticksPerBar, size_t entryQuantum) {
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
  const RuntimePlaybackViewBuildGrain grain = viewBuild.nextGrain();
  if (grain == RuntimePlaybackViewBuildGrain::Fill) {
    if (viewBuild.stepFill(entryQuantum) == RuntimePlaybackViewBuildGrain::Denied) return false;
  } else if (grain == RuntimePlaybackViewBuildGrain::Order) {
    if (viewBuild.stepOrder(entryQuantum) == RuntimePlaybackViewBuildGrain::Denied) return false;
  }
  if (viewBuild.nextGrain() != RuntimePlaybackViewBuildGrain::Complete) return true;
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
  return viewBuild.stepFill(entryQuantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM RuntimePlaybackViewBuildGrain
LoopPlaybackRuntime::stepCoverageBuildOrder(size_t entryQuantum) {
  return viewBuild.stepOrder(entryQuantum);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void LoopPlaybackRuntime::discardCoverageBuild() {
  viewBuild.discard();
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

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::beginFrozenCaptureCanonicalOrder(
    uint32_t frozenGeneration) {
  CaptureAdmissionStaging* set = captureStagingIfPrewarmed();
  if (set == nullptr || !set->frozen ||
      set->frozenGeneration != frozenGeneration ||
      set->captureGeneration != frozenGeneration) return false;
  if (frozenCaptureCanonicalOrderBinding.bound) {
    return frozenCaptureCanonicalOrderBinding.matches(*set, frozenCaptureOrderStorageGeneration) &&
           (frozenCaptureCanonicalOrder.active() || frozenCaptureCanonicalOrder.complete());
  }
  if (set->openReservationCount() != 0) return false;
  ++frozenCaptureOrderStorageGeneration;
  if (!frozenCaptureCanonicalOrder.beginCanonicalCapture(set->storage)) return false;
  frozenCaptureCanonicalOrderBinding = {frozenGeneration, 0, set->storage.eventCount, true, frozenCaptureOrderStorageGeneration};
  return true;
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

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::canonicalCaptureOutputAdmitted() const {
  return canonicalCaptureOutput.store.empty() && canonicalCaptureOutput.store.reservedAppendReady() &&
      canonicalCaptureChunks.availableChunks() >= PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM bool LoopPlaybackRuntime::replenishCanonicalCaptureChunk() {
  const auto available = canonicalCaptureChunks.availableChunks();
  return available < PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS &&
      canonicalCaptureChunks.reserveChunks(available + 1);
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

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::resetAll(bool preserveLedger) {
  for (auto& rt : runtimeBySlot_) {
    if (!rt) {
      continue;
    }
    rt->reset(preserveLedger);
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::clearAllLedgers() {
  for (auto& rt : runtimeBySlot_) {
    if (!rt) {
      continue;
    }
    rt->ledger.clear();
  }
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
