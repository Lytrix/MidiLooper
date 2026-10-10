// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Loop.h"
#include "LoopSnapshotBoxes.h"
#include "RuntimePlaybackView.h"
#include "Utils/LoopMem.h"

LOOP_COLD_MEM Loop::PendingSnapshotInventoryDestinationResult
Loop::pendingSnapshotInventoryDestinationResult() const {
  using Result = PendingSnapshotInventoryDestinationResult;
  using Phase = PreparedCompanionHistory::Phase;
  const auto& work = preparedCompanionHistory_;
  if (capture.phase != CapturePhase::None || !capture.store.empty() ||
      hasPendingCapturePass_ || hasOverdubSession() || borrowedSealedCapture_ ||
      borrowedSealedCaptureOwner_ || borrowedPendingChanges_ || borrowedPendingChangesOwner_ ||
      borrowedPreview_ || borrowedPreviewOwner_ || sessionExitReceiptCount_ ||
      (work.phase != Phase::Cold && work.phase != Phase::Prewarmed) || work.owner ||
      work.source || work.capture || work.captureInputs || work.capturePass || work.captureChunks)
    return Result::Busy;
  if (!pendingSnapshotPublishBox_) return Result::NeedsHeader;
  return pendingSnapshotPublishBox_->freshForInventoryAdoption() ? Result::Ready : Result::Busy;
}

LOOP_COLD_MEM Loop::PendingSnapshotInventoryDestinationResult
Loop::prepareFreshPendingSnapshotInventoryDestination() {
  const auto result = pendingSnapshotInventoryDestinationResult();
  if (result != PendingSnapshotInventoryDestinationResult::NeedsHeader) return result;
  // Only a default header; allocation cost/failure follows existing make_unique convention.
  pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  return PendingSnapshotInventoryDestinationResult::Ready;
}

LOOP_COLD_MEM Loop::CommittedSnapshotInventoryAdoptionResult
Loop::adoptCommittedSnapshotInventory(CommittedSnapshotInventoryPreparation& cursor,
    std::vector<PassId>& captures, EditPassIdList& companions,
    CommittedSnapshotInventoryAdoptionMode mode) {
  using Result = CommittedSnapshotInventoryAdoptionResult;
  using Source = CommittedSnapshotInventoryPreparationResult;
  const auto source = committedSnapshotInventoryPreparationResult(cursor, captures, companions);
  if (source != Source::Complete) {
    if (source == Source::Fault) return Result::SourceFault;
    if (source == Source::Superseded) return Result::SourceSuperseded;
    return Result::SourceNotReady;
  }
  if (mode != CommittedSnapshotInventoryAdoptionMode::Loaded &&
      mode != CommittedSnapshotInventoryAdoptionMode::Republish) return Result::InvalidMode;
  if (captures.empty() && companions.empty()) return Result::EmptyInventory;
  if (!loopLengthTicks) return Result::InvalidGeometry;
  if (mode == CommittedSnapshotInventoryAdoptionMode::Loaded && publishedSnapshotBox_)
    return Result::DestinationBusy;
  const auto destination = pendingSnapshotInventoryDestinationResult();
  if (destination == PendingSnapshotInventoryDestinationResult::NeedsHeader) return Result::NeedsHeader;
  if (destination != PendingSnapshotInventoryDestinationResult::Ready) return Result::DestinationBusy;
  auto& box = *pendingSnapshotPublishBox_;
  const auto& work = preparedCompanionHistory_;
  if (companions.capacity() < box.companionIds.capacity()) return Result::NeedsCompanionCapacity;
  const size_t c = work.companionCapacity;
  if (c) {
    constexpr size_t ceiling = RuntimePlaybackViewConfig::kRecordsPerBridge;
    const size_t h = passes.editPasses.size(), s = companions.size();
    if (c > ceiling || s > ceiling - c || s > work.snapshotLimit ||
        c > work.snapshotLimit - s || companions.capacity() < work.snapshotLimit ||
        work.snapshotIds.capacity() < work.snapshotLimit ||
        h > work.rowLimit || c > work.rowLimit - h ||
        passes.editPasses.capacity() < work.rowLimit || work.rows.capacity() < work.rowLimit ||
        work.companionIds.capacity() < c || work.storageGeneration != canonicalStorageGeneration ||
        work.historyCount != h) return Result::NeedsCompanionCapacity;
  }
  // Capture coverage before swaps invalidate the receipt's output-storage identity.
  const uint32_t coverage = cursor.coveredPassRevision();
  box.queuedPassIds.swap(captures);
  box.companionIds.swap(companions);
  ++pendingSnapshotCompanionGeneration_;
  box.sessionExitQueued = true;
  box.fromLoadedPasses = mode == CommittedSnapshotInventoryAdoptionMode::Loaded;
  box.loadedHydrationPhase = box.fromLoadedPasses ? LoadedSnapshotHydrationPhase::Queued
                                                 : LoadedSnapshotHydrationPhase::None;
  box.sessionExitReason = CommitReason::RecordStop;
  box.companionsApplied = box.companionIds.empty();
  box.coveredPassRevision = coverage;
  cursor.discard();
  return Result::Adopted;
}
