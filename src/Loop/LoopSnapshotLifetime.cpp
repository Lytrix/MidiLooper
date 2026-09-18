//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"

#include <utility>

namespace {
Loop::SessionExitPublishedUndoAttachFn s_sessionExitPublishedUndoAttachFn = nullptr;
}

LOOP_COLD_MEM Loop::Loop() {}

LOOP_COLD_MEM Loop::~Loop() {}

/// Defined beside the destructor: every binary that instantiates ~Loop() must
/// resolve the receipt-ring deleter, and this is the one TU that always carries
/// both the destructor and the box definition.
void Loop::SessionExitReceiptsDeleter::operator()(SessionExitSnapshotHandoff* receipts) const {
  if (!receipts) return;
  for (size_t i = 0; i < kSessionExitReceiptCapacity; ++i) {
    receipts[i].~SessionExitSnapshotHandoff();
  }
  ExternalMemoryFirstAllocator<SessionExitSnapshotHandoff>{}.deallocate(
      receipts, kSessionExitReceiptCapacity);
}

LOOP_COLD_MEM void Loop::setSessionExitPublishedUndoAttachFn(
    SessionExitPublishedUndoAttachFn fn) {
  s_sessionExitPublishedUndoAttachFn = fn;
}

LOOP_COLD_MEM void Loop::notifySessionExitPublishedUndoAttach(
    ImmutableLoopSnapshotRef published, uint32_t entryId) {
  if (entryId == 0 || !published || s_sessionExitPublishedUndoAttachFn == nullptr) {
    return;
  }
  s_sessionExitPublishedUndoAttachFn(*this, std::move(published), entryId);
}

LOOP_COLD_MEM ImmutableLoopSnapshotRef Loop::publishedSnapshot() const {
  return publishedSnapshotBox_ ? publishedSnapshotBox_->snapshot
                               : ImmutableLoopSnapshotRef{};
}

LOOP_COLD_MEM bool Loop::publishedSnapshotIsCurrent() const {
  if (!publishedSnapshotBox_ || !publishedSnapshotBox_->snapshot) {
    return false;
  }
  // DEC-073: coverage, not the revision stamp, decides currency. A root is current only when its
  // content was enumerated from the committed-pass state that is live now. A later request cannot
  // invalidate it without moving that state (which advances the committed-pass revision), so no
  // separate pending check is needed here; the persistence gate (RC2) additionally requires the
  // publisher to be settled before a root is admitted to a save.
  return publishedSnapshotBox_->coveredPassRevision == committedPassRevision();
}

LOOP_COLD_MEM uint32_t Loop::committedPassRevision() const {
  // Every enumerable committed-pass/companion inventory change bumps `playbackRevision` (pass
  // commit, wrap commit, edit-pass save/delete, pending-change seal, pass disable/reclaim), so the
  // inventory revision is the playback revision today. Kept as a named contract so the coverage
  // comparison reads as "does this root cover the committed passes?" rather than as a raw stamp.
  return playbackRevision;
}

LOOP_COLD_MEM uint32_t Loop::publishedSnapshotCoveredRevision() const {
  return publishedSnapshotBox_ ? publishedSnapshotBox_->coveredPassRevision
                               : kSnapshotCoverageUnset;
}

LOOP_COLD_MEM const void* Loop::publishedSnapshotIdentity() const {
  return publishedSnapshotIsCurrent() ? publishedSnapshotBox_->snapshot.get() : nullptr;
}

LOOP_COLD_MEM uint32_t Loop::publishedSnapshotRevision() const {
  return publishedSnapshotBox_ && publishedSnapshotBox_->snapshot
             ? publishedSnapshotBox_->revision
             : 0;
}

LOOP_COLD_MEM void Loop::clearPublishedSnapshot() {
  publishedSnapshotBox_.reset();
  for (size_t i = 0; i < sessionExitReceiptCount_; ++i) {
    const size_t slot = (sessionExitReceiptHead_ + i) % kSessionExitReceiptCapacity;
    if (sessionExitReceipts_) sessionExitReceipts_[slot] = {};
  }
  sessionExitReceiptHead_ = 0;
  sessionExitReceiptCount_ = 0;
  clearPendingSnapshotPublish();
}

LOOP_COLD_MEM void Loop::clearPendingSnapshotPublish() {
  ++pendingSnapshotCompanionGeneration_;
  if (pendingSnapshotPublishBox_ && preparedCompanionHistory_.companionCapacity != 0) {
    // Publication cleanup must retain the pre-capture companion destination.
    // Losing its capacity leaves the allocation-free stop worker unable to start.
    auto reservedIds = std::move(pendingSnapshotPublishBox_->companionIds);
    reservedIds.clear();
    *pendingSnapshotPublishBox_ = PendingSnapshotPublishBox{};
    pendingSnapshotPublishBox_->companionIds = std::move(reservedIds);
    return;
  }
  pendingSnapshotPublishBox_.reset();
}

LOOP_COLD_MEM void Loop::storePublishedSnapshot(ImmutableLoopSnapshotRef snapshot,
                                                uint32_t revision,
                                                uint32_t coveredPassRevision) {
  if (!snapshot) {
    publishedSnapshotBox_.reset();
    return;
  }
  if (!publishedSnapshotBox_) {
    publishedSnapshotBox_ = std::make_unique<PublishedSnapshotBox>();
  }
  publishedSnapshotBox_->snapshot = std::move(snapshot);
  publishedSnapshotBox_->revision = revision != 0 ? revision : playbackRevision;
  // DEC-073: coverage is evidence about the content, so it is never inferred from the stamp. The
  // "current" sentinel means the caller derived this root from the live state in this same turn.
  publishedSnapshotBox_->coveredPassRevision =
      coveredPassRevision == kSnapshotCoverageCurrent ? committedPassRevision()
                                                      : coveredPassRevision;
}

LOOP_COLD_MEM void Loop::setPendingSessionExitUndoAttachEntryId(uint32_t entryId) {
  if (!pendingSnapshotPublishBox_ || !pendingSnapshotPublishBox_->sessionExitQueued) {
    return;
  }
  pendingSnapshotPublishBox_->sessionExitUndoAttachEntryId = entryId;
}

LOOP_COLD_MEM uint32_t Loop::pendingSessionExitUndoAttachEntryId() const {
  if (!pendingSnapshotPublishBox_ || !pendingSnapshotPublishBox_->sessionExitQueued) {
    return 0;
  }
  return pendingSnapshotPublishBox_->sessionExitUndoAttachEntryId;
}
