//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

void Loop::reclaimUnreferencedDisabledPasses(const SlotPassReferences& refs) {
  reclaimUnreferencedDisabledCapturePasses(refs);
  reclaimUnreferencedDisabledEditPasses(refs);
}

void Loop::markPassDerivedStale() {
  passesMaterializedStoreStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

NoteId Loop::allocateNoteId() {
  return nextNoteId_++;
}

void Loop::resetPassTimeline() {
  discardPendingCapturePass();
  if (passes.hasRecordPass()) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass = RecordPass{};
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
  }
  passes.overdubPasses.clear();
  passes.editPasses.clear();
  nextPassId_ = 1;
  nextNoteId_ = 1;
  nextMergeSequence_ = 0;
  lastCommittedPassId_ = kInvalidPassId;
  playbackRevision = 0;
  editStateDirty_ = false;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardEventsCache();
  passesMaterializedStoreStale_ = true;
  clearOverdubSourceView();
  clearPendingNoteChanges();
}

void Loop::invalidateCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
  visualCacheDirty = true;
  playbackOrderDirty = true;
  passesMaterializedStoreStale_ = true;
}

void Loop::invalidatePlaybackCaches() {
  playbackOrderDirty = true;
  passesMaterializedStore_.discardEventsCache();
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
}
