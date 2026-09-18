//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Utils/MemoryMonitor.h"
#include "Loop.h"
#include "RuntimeCaptureCompanions.h"
#include "RuntimeCanonicalPassInputs.h"
#include "RuntimeCompanionBridgeMap.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"

namespace {
LOOP_COLD_MEM void copyPreparedEditMetadata(EditPass& out, const EditPass& in) {
  out.id = in.id; out.passType = in.passType; out.editPassIndex = in.editPassIndex;
  out.actionType = in.actionType; out.propertyType = in.propertyType; out.state = in.state;
  out.targetNoteId = in.targetNoteId; out.startTick = in.startTick; out.endTick = in.endTick;
  out.pitch = in.pitch; out.velocity = in.velocity;
  out.addedEvents.clear(); // MidiEvent destruction is trivial; storage is retained.
}
}

LOOP_COLD_MEM uint64_t Loop::requiredPreparedCompanionHistoryGrains(
    size_t historyRows, size_t historyPayloadEvents, size_t companionCapacity, size_t quantum, size_t snapshotIds, size_t captureEvents, size_t captureChunks) {
  const size_t limit = RuntimePlaybackViewConfig::kRecordsPerBridge;
  if (!quantum || historyRows > limit || companionCapacity > limit - historyRows ||
      historyPayloadEvents > RuntimePlaybackViewConfig::kEventsPerView ||
      snapshotIds > limit - companionCapacity ||
      captureEvents > RuntimePlaybackViewConfig::kEventsPerView ||
      captureChunks > PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS) return UINT64_MAX;
  const uint64_t units = historyRows + historyPayloadEvents + companionCapacity + snapshotIds + captureEvents + captureChunks + 2;
  return 1 + (units - 1) / quantum;
}

LOOP_COLD_MEM size_t Loop::companionRowHorizonBound() const {
  // `upsertSourceTransform` keeps at most one Shorten/Hide (sealed as Delete/Length) row
  // per source note identity, and one row is produced per eligible change, so the
  // producible count is bounded by the number of distinct source note identities the
  // interval can occupy. Every canonical source note contributes at least one payload
  // event, so payload event count is a sound (generous) proxy; the reservation capacity
  // covers identities whose payloads were synthesized rather than stored.
  size_t payloadEvents = 0;
  for (const EditPass& row : passes.editPasses) {
    payloadEvents += row.addedEvents.size();
  }
  const size_t bounded = payloadEvents + RuntimePlaybackViewConfig::kMaxCaptureNoteReservations;
  return bounded > RuntimePlaybackViewConfig::kRecordsPerBridge
             ? RuntimePlaybackViewConfig::kRecordsPerBridge
             : bounded;
}

LOOP_COLD_MEM bool Loop::prewarmPreparedCompanionHistory(size_t companionCapacity) {
  constexpr size_t limit = RuntimePlaybackViewConfig::kRecordsPerBridge;
  if (passes.editPasses.size() > limit || companionCapacity > limit - passes.editPasses.size()) return false;
  size_t payloads = 0;
  for (const auto& row : passes.editPasses) {
    if (row.addedEvents.size() > RuntimePlaybackViewConfig::kEventsPerView - payloads) return false;
    payloads += row.addedEvents.size();
  }
  const size_t snapshotCount = pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->companionIds.size() : 0;
  if (snapshotCount > limit - companionCapacity) return false;
  ensurePendingSnapshotPublishBox();
  auto& work = preparedCompanionHistory_;
  work.rowLimit = passes.editPasses.size() + companionCapacity;
  ++work.preparationGeneration;
  work.snapshotLimit = snapshotCount + companionCapacity;
  // Both vectors will exchange roles. Reserve the canonical side while STOPPED
  // as well, and invalidate borrowed history before a possible relocation.
  // Point-of-use reserves: the probe is taken immediately before the allocation, so its
  // verdict is predictive, and a denied reserve returns the `false` this function's
  // contract already allows instead of reaching `InternalHeapFirstAllocator::abort()`.
  // The callers ignore the result, so the only behaviour change is not dying.
  if (passes.editPasses.capacity() < work.rowLimit) {
    if (!MemoryMonitor::admitOperationMemory(
            MemoryMonitor::OperationKind::CompanionHistoryReserve,
            static_cast<uint32_t>(work.rowLimit * sizeof(EditPass)))) {
      return false;
    }
    ++canonicalStorageGeneration;
    passes.editPasses.reserve(work.rowLimit);
  }
  pendingSnapshotPublishBox_->companionIds.reserve(work.snapshotLimit);
  work.companionIds.clear(); work.companionIds.reserve(companionCapacity);
  work.snapshotIds.clear(); work.snapshotIds.reserve(snapshotCount + companionCapacity);
  // Caller is STOPPED; resizing/releasing old payload storage belongs here.
  const size_t rowCount = passes.editPasses.size() + companionCapacity;
  if (work.rows.capacity() < rowCount &&
      !MemoryMonitor::admitOperationMemory(
          MemoryMonitor::OperationKind::CompanionHistoryReserve,
          static_cast<uint32_t>(rowCount * sizeof(EditPass)))) {
    return false;
  }
  work.rows.clear(); work.rows.resize(rowCount);
  for (size_t i = 0; i < passes.editPasses.size(); ++i)
    work.rows[i].addedEvents.reserve(passes.editPasses[i].addedEvents.size());
  work.historyCount = passes.editPasses.size(); work.companionCapacity = companionCapacity;
  work.storageGeneration = canonicalStorageGeneration;
  work.source = nullptr; work.capture = nullptr; work.captureInputs = nullptr; work.capturePass = nullptr;
  work.owner = 0;
  work.phase = PreparedCompanionHistory::Phase::Prewarmed;
  return true;
}

LOOP_COLD_MEM bool Loop::beginPreparedCompanionHistory(
    const RuntimeCaptureCompanions& rows, const CaptureAdmissionStaging& capture,
    const RuntimeCanonicalPassInputs* captureInputs, uint64_t owner,
    PendingCapturePass* capturePass) {
  auto& work = preparedCompanionHistory_;
  const size_t historyCount = passes.editPasses.size();
  // Prewarm reserves storage, not a borrowed canonical-history version. Wrap
  // publication can append metadata before this worker begins. Bind the live
  // version below, after checking that the existing reservation still fits.
  // Once begun, preparedCompanionHistorySourceCurrent keeps the strict version
  // checks; never restart an in-flight worker by rebinding its inputs.
  if (work.phase != PreparedCompanionHistory::Phase::Prewarmed ||
      !preparedCompanionHistoryOwnedBy(owner) ||
      // Fit is a capacity question, never a current-size question: `Phase::Trim` (and
      // the recycle path) shrink the vector's *size* to the last interval's actual
      // demand, so a later, larger interval must still be admitted against the reserve.
      work.rows.capacity() < historyCount + rows.size() ||
      historyCount > work.rowLimit || rows.size() > work.rowLimit - historyCount ||
      rows.size() > work.companionCapacity || rows.size() > work.companionIds.capacity() ||
      passes.editPasses.capacity() < work.rowLimit ||
      !rows.ready(capture) ||
      nextPassId_ == kInvalidPassId || rows.size() > UINT32_MAX - nextPassId_) return false;
  const size_t snapshotCount = pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->companionIds.size() : 0;
  if (!pendingSnapshotPublishBox_ || snapshotCount > work.snapshotIds.capacity() ||
      rows.size() > work.snapshotIds.capacity() - snapshotCount ||
      snapshotCount > work.snapshotLimit || rows.size() > work.snapshotLimit - snapshotCount ||
      pendingSnapshotPublishBox_->companionIds.capacity() < work.snapshotLimit) return false;
  // OpenSpec 5.12 / D8: bind the interval's own staged pass by address. A deferred job passes its
  // job-owned receipt, so a successor interval's capture start can neither destroy nor rebind this
  // preparation's verification source. A caller that supplies no pass (`nullptr`, the default) is on
  // the live producer path -- or a fixture that staged through the Loop slot -- where the Loop's own
  // staged pass is the only receipt that exists; there is no other fallback, so a deferred job must
  // always name its own receipt (production passes `&job.pendingPass` unconditionally).
  PendingCapturePass* boundCapturePass = capturePass;
  if (boundCapturePass == nullptr && hasPendingCapturePass_) boundCapturePass = &pendingCapturePass_;
  if (captureInputs && (boundCapturePass == nullptr || boundCapturePass->id == kInvalidPassId ||
      boundCapturePass->committedChunkIds.empty() ||
      boundCapturePass->committedChunkIds.size() > PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS ||
      !captureInputs->includesFrozenOverdub() || captureInputs->frozenCapture() != &capture)) return false;
  work.historyCount = historyCount;
  work.storageGeneration = canonicalStorageGeneration;
  work.captureInputs = captureInputs;
  work.capturePass = boundCapturePass;
  ++work.preparationGeneration;
  work.captureInputsGeneration = captureInputs ? captureInputs->generation() : 0;
  work.captureChunks = boundCapturePass != nullptr ? boundCapturePass->committedChunkIds.data() : nullptr;
  work.captureChunkCount = boundCapturePass != nullptr ? boundCapturePass->committedChunkIds.size() : 0;
  work.captureChunk = work.captureEvent = work.comparedEvents = 0;
  work.snapshotCount = snapshotCount; work.snapshotIndex = 0;
  work.snapshotGeneration = pendingSnapshotCompanionGeneration_;
  work.source = &rows; work.capture = &capture; work.rowGeneration = rows.preparationGeneration();
  work.owner = owner;
  work.revision = playbackRevision; work.firstId = nextPassId_;
  work.capturePassId = boundCapturePass != nullptr ? boundCapturePass->id : kInvalidPassId;
  work.targetCount = work.historyCount + rows.size(); work.row = work.event = 0;
  // Size the working vector to *this interval's* actual demand. The grant (capacity)
  // still covers the whole horizon, so the resize allocates nothing; what it removes is
  // the `Phase::Trim` walk, which otherwise pops one unused row per quantum unit -
  // `grantedCompanionCapacity - actualCompanionRows` units, about 2,030 of pure trimming
  // for a full horizon with a sparse overdub. Rows below `historyCount` keep their
  // prewarmed payload capacity, which is what the copy phase relies on.
  work.rows.resize(work.targetCount);
  work.phase = snapshotCount ? PreparedCompanionHistory::Phase::CopySnapshot :
      (work.historyCount ? PreparedCompanionHistory::Phase::CopyRow : PreparedCompanionHistory::Phase::Append);
  return true;
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryOwnedBy(uint64_t owner) const {
  const auto& work = preparedCompanionHistory_;
  using Phase = PreparedCompanionHistory::Phase;
  // Cold/Prewarmed hold no receipt, so no interval owns the horizon and any
  // caller may begin (subject to the phase machine) or observe it.
  if (work.phase == Phase::Cold || work.phase == Phase::Prewarmed) return true;
  return work.owner == owner;
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistorySourceCurrent() const {
  const auto& work = preparedCompanionHistory_;
  // OpenSpec 5.12 / D8: an interval-owned preparation validates against the receipt it bound at
  // begin (the job's pass), never against the Loop producer slot -- a successor may own that slot.
  const PendingCapturePass* boundPass = work.capturePass;
  return pendingSnapshotPublishBox_ && work.snapshotGeneration == pendingSnapshotCompanionGeneration_ &&
      work.snapshotCount == pendingSnapshotPublishBox_->companionIds.size() && work.storageGeneration == canonicalStorageGeneration && work.revision == playbackRevision &&
      work.historyCount == passes.editPasses.size() && work.firstId == nextPassId_ &&
      work.source && work.capture && work.source->preparationGeneration() == work.rowGeneration &&
      work.source->ready(*work.capture) &&
      (!work.captureInputs || (boundPass != nullptr && boundPass->id == work.capturePassId &&
       boundPass->committedChunkIds.data() == work.captureChunks &&
       boundPass->committedChunkIds.size() == work.captureChunkCount &&
       work.captureInputs->generation() == work.captureInputsGeneration &&
       work.captureInputs->includesFrozenOverdub() && work.captureInputs->frozenCapture() == work.capture));
}

LOOP_COLD_MEM bool Loop::stepPreparedCompanionHistory(size_t quantum, uint64_t owner) {
  auto& work = preparedCompanionHistory_;
  using Phase = PreparedCompanionHistory::Phase;
  if (!quantum || work.phase == Phase::Cold || work.phase == Phase::Prewarmed ||
      work.phase >= Phase::Committed)
    return false;
  if (!preparedCompanionHistoryOwnedBy(owner)) return false;
  if (!preparedCompanionHistorySourceCurrent()) { work.phase = Phase::Denied; return false; }
  while (quantum-- && work.phase != Phase::Ready) {
    if (work.phase == Phase::CopySnapshot) {
      const auto id = pendingSnapshotPublishBox_->companionIds[work.snapshotIndex++];
      // Prospective IDs must be fresh, so no duplicate-search belongs in the handoff.
      if (id >= work.firstId) { work.phase = Phase::Denied; return false; }
      work.snapshotIds.push_back(id);
      if (work.snapshotIndex == work.snapshotCount)
        work.phase = work.historyCount ? Phase::CopyRow : Phase::Append;
    } else if (work.phase == Phase::CopyRow) {
      const auto& source = passes.editPasses[work.row];
      auto& destination = work.rows[work.row];
      if (source.addedEvents.size() > destination.addedEvents.capacity()) { work.phase = Phase::Denied; return false; }
      copyPreparedEditMetadata(destination, source); work.event = 0;
      if (source.addedEvents.empty()) {
        if (++work.row == work.historyCount) work.phase = Phase::Append;
      } else work.phase = Phase::CopyPayload;
    } else if (work.phase == Phase::CopyPayload) {
      const auto& source = passes.editPasses[work.row].addedEvents;
      work.rows[work.row].addedEvents.push_back(source[work.event++]);
      if (work.event == source.size()) {
        ++work.row; work.phase = work.row == work.historyCount ? Phase::Append : Phase::CopyRow;
      }
    } else if (work.phase == Phase::Append) {
      if (work.row == work.targetCount) { work.phase = Phase::Trim; continue; }
      const auto& source = work.source->row(work.row - work.historyCount);
      if (!source.addedEvents.empty() || source.passType != EditPassType::Note ||
          !(source.actionType == EditActionType::Delete ||
            (source.actionType == EditActionType::Update && source.propertyType == EditPropertyType::Length))) {
        work.phase = Phase::Denied; return false;
      }
      auto& destination = work.rows[work.row]; copyPreparedEditMetadata(destination, source);
      destination.id = work.firstId + work.row - work.historyCount;
      destination.editPassIndex = kOverdubCompanionEditPassIndex;
      destination.state = EditPassState::Active;
      work.companionIds.push_back(destination.id);
      work.snapshotIds.push_back(destination.id);
      ++work.row;
    } else if (work.phase == Phase::Trim) {
      // `begin` already sized `rows` to `targetCount` (see the resize comment above),
      // so this phase trims nothing; it exists only to advance once every demanded
      // row has been written by the fill phases, one unit per quantum.
      work.phase = work.captureInputs ? Phase::VerifyCapture : Phase::Ready;
    } else if (work.phase == Phase::VerifyCapture) {
      MidiEvent event; bool atEnd = false;
      if (!LoopEventStore::readSealedChunkEvent(work.captureChunks[work.captureChunk],
          work.captureEvent, event, atEnd)) { work.phase = Phase::Denied; return false; }
      if (atEnd) {
        work.captureEvent = 0;
        if (++work.captureChunk == work.captureChunkCount) {
          if (work.comparedEvents != work.captureInputs->frozenCaptureEventCount()) {
            work.phase = Phase::Denied; return false;
          }
          work.phase = Phase::Ready;
        }
      } else {
        const auto* expected = work.captureInputs->frozenCaptureEvent(work.comparedEvents);
        if (!expected || !RuntimeCompanionBridgeMap::inputEventsMatch(event, *expected)) {
          work.phase = Phase::Denied; return false;
        }
        ++work.captureEvent; ++work.comparedEvents;
      }
    }
  }
  return true; // Valid pending or complete progress; readiness is queried separately.
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryReady(uint64_t owner) const {
  return preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Ready &&
      preparedCompanionHistoryOwnedBy(owner) && preparedCompanionHistorySourceCurrent();
}
LOOP_COLD_MEM const EditPassVec* Loop::preparedCompanionHistory(uint64_t owner) const {
  return preparedCompanionHistoryReady(owner) ? &preparedCompanionHistory_.rows : nullptr;
}
LOOP_COLD_MEM bool Loop::restoreSupersededPreparedCompanionHistory(uint64_t owner) {
  auto& work = preparedCompanionHistory_;
  using Phase = PreparedCompanionHistory::Phase;
  if (!preparedCompanionHistoryOwnedBy(owner) || work.phase == Phase::Cold ||
      work.phase == Phase::Prewarmed) return false;
  // Allocation-free by construction: the horizon was reserved when this
  // preparation began, so the only work is dropping copied rows and re-syncing
  // the horizon to the current canonical history.
  if (work.historyCount > work.rowLimit || work.rows.capacity() < work.rowLimit ||
      work.snapshotIds.capacity() < work.snapshotLimit || !pendingSnapshotPublishBox_ ||
      pendingSnapshotPublishBox_->companionIds.capacity() < work.snapshotLimit) return false;
  const size_t historyCount = passes.editPasses.size();
  if (historyCount > work.rowLimit) return false;
  const size_t rowRemaining = work.rowLimit - historyCount;
  const size_t liveSnapshotCount = pendingSnapshotPublishBox_->companionIds.size();
  const size_t snapshotRemaining =
      work.snapshotLimit > liveSnapshotCount ? work.snapshotLimit - liveSnapshotCount : 0;
  work.companionIds.clear();
  work.snapshotIds.clear();
  for (auto& row : work.rows) row.addedEvents.clear();
  work.historyCount = historyCount;
  work.companionCapacity = rowRemaining < snapshotRemaining ? rowRemaining : snapshotRemaining;
  work.rows.resize(historyCount + work.companionCapacity);
  work.storageGeneration = canonicalStorageGeneration;
  work.revision = playbackRevision;
  work.firstId = nextPassId_;
  work.snapshotGeneration = pendingSnapshotCompanionGeneration_;
  work.snapshotCount = liveSnapshotCount;
  work.source = nullptr; work.capture = nullptr; work.captureInputs = nullptr; work.capturePass = nullptr;
  work.owner = 0;
  work.phase = Phase::Prewarmed;
  return true;
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryInFlight(uint64_t owner) const {
  const auto& work = preparedCompanionHistory_;
  return preparedCompanionHistoryOwnedBy(owner) && work.phase != PreparedCompanionHistory::Phase::Cold &&
      work.phase != PreparedCompanionHistory::Phase::Prewarmed;
}

LOOP_COLD_MEM void Loop::discardPreparedCompanionHistory(uint64_t owner) {
  auto& work = preparedCompanionHistory_;
  if (!preparedCompanionHistoryOwnedBy(owner)) return;
  ++work.preparationGeneration;
  work.source = nullptr; work.capture = nullptr; work.captureInputs = nullptr; work.capturePass = nullptr;
  work.owner = 0;
  work.phase = PreparedCompanionHistory::Phase::Cold;
}

LOOP_COLD_MEM const EditPassIdList* Loop::preparedCompanionHistoryIds(uint64_t owner) const {
  return preparedCompanionHistoryOwnedBy(owner) &&
      (preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Committed ||
       preparedCompanionHistoryReady(owner)) ? &preparedCompanionHistory_.companionIds : nullptr;
}

LOOP_COLD_MEM bool Loop::commitPreparedCompanionHistoryAtomic(uint64_t owner) {
  if (!preparedCompanionHistoryReady(owner)) return false;
  publishPreparedCompanionHistory();
  return true;
}

LOOP_COLD_MEM void Loop::publishPreparedCompanionHistory() {
  auto& work = preparedCompanionHistory_;
  if (!work.companionIds.empty()) {
    ++canonicalStorageGeneration; // Invalidate borrowed rows before exchanging their owner.
    passes.editPasses.swap(work.rows);
    pendingSnapshotPublishBox_->companionIds.swap(work.snapshotIds);
    ++pendingSnapshotCompanionGeneration_;
    pendingSnapshotPublishBox_->companionsApplied = false;
    nextPassId_ += work.companionIds.size();
    ++playbackRevision;
    editStateDirty_ = true;
    // The normal notification measures visual coverage in capture builds.
    // Publish only scalar invalidation here; defer diagnostic traversal.
    visualCacheDirty = true;
    invalidatePlaybackCaches();
  }
  work.source = nullptr; work.capture = nullptr; work.captureInputs = nullptr; work.capturePass = nullptr;
  work.storageGeneration = canonicalStorageGeneration;
  work.revision = playbackRevision; work.firstId = nextPassId_;
  work.historyCount = passes.editPasses.size();
  work.snapshotGeneration = pendingSnapshotCompanionGeneration_;
  work.snapshotCount = pendingSnapshotPublishBox_->companionIds.size();
  work.phase = PreparedCompanionHistory::Phase::Committed;
}

LOOP_COLD_MEM bool Loop::canCommitPreparedCaptureAndCompanions(uint64_t owner,
                                                             const PendingCapturePass* pass) const {
  // OpenSpec 5.12 / D8: resolve the staged receipt through the interval that owns it. A deferred
  // caller passes the job's pass; otherwise the pass this preparation bound at begin; and only
  // then the live producer slot (a preparation that carries no frozen capture inputs).
  const PendingCapturePass* staged = pass;
  if (staged == nullptr) staged = preparedCompanionHistory_.capturePass;
  if (staged == nullptr && hasPendingCapturePass_) staged = &pendingCapturePass_;
  return preparedCompanionHistoryReady(owner) && staged != nullptr &&
      canTransferCapturePassWithoutAllocation(*staged) &&
      preparedCompanionHistory_.capturePassId != kInvalidPassId &&
      preparedCompanionHistory_.capturePassId == staged->id &&
      staged->phase == CapturePassPhase::Overdub &&
      !staged->committedChunkIds.empty() &&
      (!staged->retiresLiveCapture ||
       (capture.phase == CapturePhase::Overdub && capture.store.empty()));
}

LOOP_COLD_MEM bool Loop::commitPreparedCaptureAndCompanionsAtomic(
    PreparedCapturePassTransfer& transferred, uint64_t owner, PendingCapturePass* pass) {
  PendingCapturePass* staged = pass;
  if (staged == nullptr) staged = preparedCompanionHistory_.capturePass;
  if (staged == nullptr && hasPendingCapturePass_) staged = &pendingCapturePass_;
  if (staged == nullptr || !canCommitPreparedCaptureAndCompanions(owner, staged)) return false;
  // The transfer's only failure is its capacity preflight, before any mutation. It advances
  // revision, so history must publish using the proof above rather than rechecking the
  // now-obsolete source identity midway through the commit.
  const bool consumedLoopSlot = staged == &pendingCapturePass_;
  if (consumedLoopSlot ? !transferPreparedCapturePass(transferred)
                       : !transferCapturePass(*staged, transferred)) {
    return false;
  }
  publishPreparedCompanionHistory();
  // Staging already detached the capture store. Retire scalar ownership only;
  // the legacy completion notification scans visual coverage in capture builds.
  if (transferred.retiresLiveCapture) {
    capture.phase = CapturePhase::None;
    captureNextEventIndex = 0;
    captureEventsSortDirty = false;
    captureHasLastAppendTick_ = false;
    captureLastAppendTick_ = 0;
  }
  visualCacheDirty = true;
  invalidatePlaybackCaches();
  return true;
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryMatches(
    const RuntimeCaptureCompanions& rows, const CaptureAdmissionStaging& capture,
    uint64_t owner) const {
  return preparedCompanionHistoryReady(owner) && preparedCompanionHistory_.source == &rows &&
      preparedCompanionHistory_.capture == &capture;
}

LOOP_COLD_MEM bool Loop::preparedCompanionCaptureVerified(
    const RuntimeCanonicalPassInputs& inputs, uint64_t owner) const {
  return preparedCompanionHistoryReady(owner) && preparedCompanionHistory_.captureInputs == &inputs &&
      preparedCompanionHistory_.captureChunk == preparedCompanionHistory_.captureChunkCount;
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryCanStep(const RuntimeCaptureCompanions& rows,
    const CaptureAdmissionStaging& capture, const RuntimeCanonicalPassInputs& inputs,
    uint64_t owner) const {
  const auto& work = preparedCompanionHistory_;
  using Phase = PreparedCompanionHistory::Phase;
  return work.phase != Phase::Cold && work.phase != Phase::Prewarmed &&
      work.phase < Phase::Committed &&
      preparedCompanionHistoryOwnedBy(owner) &&
      work.source == &rows && work.capture == &capture && work.captureInputs == &inputs &&
      preparedCompanionHistorySourceCurrent();
}

LOOP_COLD_MEM bool Loop::beginCollectOverdubStopUndoPasses(SessionUndoCollection& work,
    PassIdList& passIds, EditPassIdList& companionIds, PassId finalPassId, bool includeFinalPass,
    uint64_t owner, size_t sessionExitReceiptRing) {
  const auto* receipt = preparedCompanionHistoryIds(owner);
  if (!receipt || receipt->size() > RuntimePlaybackViewConfig::kRecordsPerBridge ||
      preparedCompanionHistory_.storageGeneration != canonicalStorageGeneration ||
      preparedCompanionHistory_.revision != playbackRevision || preparedCompanionHistory_.firstId != nextPassId_) {
    return false;
  }
  // DEC-068: this interval's wrap identities left the shared session at its stop admission
  // (`captureSessionExitTail` swaps them into its own session-exit receipt) so a successor opens a
  // clean session. The undo entry must therefore read that receipt: the live `overdubSessionPassIds_`
  // is empty here and belongs to whatever interval is live now, so a plain
  // `collectOverdubSessionUndoPasses` recorded only the final pass and a single undo removed just
  // the last committed pass instead of the whole overdub session.
  const SessionExitSnapshotHandoff* sessionReceipt =
      sessionExitReceiptRing == kInvalidSessionExitReceipt
          ? nullptr
          : sessionExitReceiptByRingIndex(sessionExitReceiptRing);
  if (!beginCollectOverdubSessionUndoPasses(work, passIds, companionIds, sessionReceipt)) return false;
  work.finalReceipt = true; work.finalPassId = finalPassId; work.includeFinalPass = includeFinalPass;
  work.receiptGeneration = preparedCompanionHistory_.preparationGeneration;
  work.receiptCount = receipt->size();
  return true;
}

LOOP_COLD_MEM uint64_t Loop::requiredSessionStopUndoCollectionGrains(
    size_t rows, size_t companions, size_t finalCompanions, size_t quantum) {
  constexpr size_t kLimit = RuntimePlaybackViewConfig::kRecordsPerBridge;
  if (!quantum || rows > kLimit || companions > kLimit || finalCompanions > kLimit - companions)
    return UINT64_MAX;
  return 1 + (uint64_t(rows) * 2 + companions + finalCompanions + 2) / quantum;
}

LOOP_COLD_MEM uint64_t Loop::requiredPreparedCompanionRecycleGrains(size_t rowCapacity,
    size_t retiredPayloadEvents, size_t companionIds, size_t snapshotIds, size_t quantum) {
  const auto limit = RuntimePlaybackViewConfig::kRecordsPerBridge;
  if (!quantum || rowCapacity > limit || companionIds > rowCapacity || snapshotIds > limit ||
      retiredPayloadEvents > RuntimePlaybackViewConfig::kEventsPerView) return UINT64_MAX;
  const uint64_t units = rowCapacity + retiredPayloadEvents + companionIds + snapshotIds + 4;
  return 1 + (units - 1) / quantum;
}

LOOP_COLD_MEM bool Loop::preparedCompanionRecycleSourceCurrent() const {
  const auto& work = preparedCompanionHistory_;
  return work.storageGeneration == canonicalStorageGeneration && work.revision == playbackRevision &&
      work.firstId == nextPassId_ && work.historyCount == passes.editPasses.size() &&
      pendingSnapshotPublishBox_ && work.snapshotGeneration == pendingSnapshotCompanionGeneration_ &&
      work.snapshotCount == pendingSnapshotPublishBox_->companionIds.size();
}

LOOP_COLD_MEM bool Loop::beginRecyclePreparedCompanionHistory(uint64_t owner) {
  auto& work = preparedCompanionHistory_;
  if (work.phase != PreparedCompanionHistory::Phase::Committed ||
      !preparedCompanionHistoryOwnedBy(owner) ||
      !preparedCompanionRecycleSourceCurrent() || work.historyCount > work.rowLimit ||
      work.snapshotCount > work.snapshotLimit || work.rows.capacity() < work.rowLimit ||
      work.snapshotIds.capacity() < work.snapshotLimit) return false;
  const auto rowRemaining = work.rowLimit - work.historyCount;
  const auto snapshotRemaining = work.snapshotLimit - work.snapshotCount;
  work.companionCapacity = rowRemaining < snapshotRemaining ? rowRemaining : snapshotRemaining;
  work.row = 0;
  work.phase = PreparedCompanionHistory::Phase::RecycleIds;
  return true;
}

LOOP_COLD_MEM bool Loop::stepRecyclePreparedCompanionHistory(size_t quantum, uint64_t owner) {
  auto& work = preparedCompanionHistory_;
  using Phase = PreparedCompanionHistory::Phase;
  if (!quantum || work.phase < Phase::RecycleIds || work.phase > Phase::RecycleGrow) return false;
  if (!preparedCompanionHistoryOwnedBy(owner)) return false;
  if (!preparedCompanionRecycleSourceCurrent()) { work.phase = Phase::Denied; return false; }
  while (quantum-- && work.phase != Phase::Prewarmed) {
    if (work.phase == Phase::RecycleIds) {
      if (!work.companionIds.empty()) work.companionIds.pop_back();
      else work.phase = Phase::RecycleSnapshot;
    } else if (work.phase == Phase::RecycleSnapshot) {
      if (!work.snapshotIds.empty()) work.snapshotIds.pop_back();
      else work.phase = Phase::RecycleRows;
    } else if (work.phase == Phase::RecycleRows) {
      if (work.row < work.rows.size()) {
        auto& events = work.rows[work.row].addedEvents;
        if (!events.empty()) events.pop_back();
        else ++work.row;
      } else work.phase = Phase::RecycleGrow;
    } else if (work.phase == Phase::RecycleGrow) {
      // Size, unlike capacity, must match the usable horizon: extra rows would
      // add unreserved Trim units when snapshot slack is smaller than row slack.
      // Batched: every row here is inside the reserve, so one resize restores the
      // horizon without spending a quantum unit per row.
      work.rows.resize(work.historyCount + work.companionCapacity);
      // Receipt lifetime ends here; the horizon is owner-agnostic again.
      work.owner = 0;
      work.phase = Phase::Prewarmed;
    }
  }
  return true;
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryRecycleComplete(uint64_t owner) const {
  // Prewarmed holds no receipt, so completion is owner-agnostic by construction;
  // `owner` is accepted for call-site symmetry with the qualified accessors.
  (void)owner;
  return preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Prewarmed &&
      preparedCompanionRecycleSourceCurrent();
}

LOOP_COLD_MEM bool Loop::preparedCompanionHistoryPrewarmed(size_t requiredCompanions) const {
  const auto& work = preparedCompanionHistory_;
  return work.phase == PreparedCompanionHistory::Phase::Prewarmed &&
      work.storageGeneration == canonicalStorageGeneration && work.historyCount == passes.editPasses.size() &&
      requiredCompanions <= work.companionCapacity && pendingSnapshotPublishBox_ &&
      passes.editPasses.capacity() >= work.rowLimit && work.rows.capacity() >= work.rowLimit &&
      pendingSnapshotPublishBox_->companionIds.capacity() >= work.snapshotLimit &&
      requiredCompanions <= work.snapshotIds.capacity() &&
      pendingSnapshotPublishBox_->companionIds.size() <= work.snapshotIds.capacity() - requiredCompanions;
}
