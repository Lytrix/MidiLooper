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
  return publishedSnapshotBox_ && publishedSnapshotBox_->snapshot &&
         publishedSnapshotBox_->revision == playbackRevision;
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
  clearPendingSnapshotPublish();
}

LOOP_COLD_MEM void Loop::clearPendingSnapshotPublish() {
  pendingSnapshotPublishBox_.reset();
}

LOOP_COLD_MEM void Loop::storePublishedSnapshot(ImmutableLoopSnapshotRef snapshot,
                                                uint32_t revision) {
  if (!snapshot) {
    publishedSnapshotBox_.reset();
    return;
  }
  if (!publishedSnapshotBox_) {
    publishedSnapshotBox_ = std::make_unique<PublishedSnapshotBox>();
  }
  publishedSnapshotBox_->snapshot = std::move(snapshot);
  publishedSnapshotBox_->revision = revision != 0 ? revision : playbackRevision;
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
