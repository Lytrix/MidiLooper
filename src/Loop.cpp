//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
#include "Utils/LoopMem.h"

void Loop::reclaimUnreferencedDisabledPasses(const SlotPassReferences& refs) {
  reclaimUnreferencedDisabledCapturePasses(refs);
  reclaimUnreferencedDisabledEditPasses(refs);
}

void Loop::notifyCommittedContentChanged() {
  // The committed source generation moved. Touched coverage is session state describing *what this
  // overdub touched*, so it survives; prepared coverage for the old generation is no longer
  // authoritative, so every required tile becomes owed again (task 4.4).
  overdubCoverage_.changeGeneration(playbackRevision);
  // Touched-coverage producer (task 4.8): the cache holds the old generation's tile content, so it is not
  // a source for the new one. The worker republishes the required tiles, content and bit together. The
  // view is a read-only cut of the cache and goes with it; the next session/capture edge recuts it.
  overdubSourceSpanCacheNotes_.clear();
  overdubSourceSpanCacheValid_ = false;
  overdubSourceSpanCacheFullLoop_ = false;
  overdubSourceSpanCacheCoverageStart_ = 0;
  overdubSourceSpanCacheCoverageLength_ = 0;
  overdubSourceSpanCachePublishedRevision_ = 0;
  overdubSourceSpanCachePublishedIdentity_ = nullptr;
  overdubSourceViewNotes_.clear();
  overdubSourceViewEvents_.clear();
  // The view stays **established** (an empty prepared source is a legitimate state, and the session is
  // still live): a note-off then defers by name instead of silently returning "no source".
  overdubSourceViewLoopLengthTicks_ = loopLengthTicks;
  // A completion owed for the old generation is re-owed by `changeGeneration` (its tiles are unprepared
  // again); the retained parameters stay valid because the note's span did not move.
  // CONTRACTS.md C3.5: the committed revision moved, so a private-predecessor scan that is running
  // (or finished) is stale. Drop the scan and the rows it staged; a private prefix is never
  // completed by -- or mixed with -- committed content. Retained hold intents survive: the
  // committed origin now answers for them.
  discardPrivateSourceScan();
  markPassDerivedStale();
}

void Loop::markPassDerivedStale() {
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
  // Unlike markDisplayCachesStale this leaves dirtyBars as the previous rebuild left it, so the
  // emitted dsz/dcnt say which bars the following idle slices are allowed to revisit.
  // After slice_clean, dirtyBars is empty while notes remain — do not emit a zero-size dirty set.
  if (visualCache.notes.empty() || !visualCache.dirtyBars.empty()) {
    emitVisualCacheState("stale", -1);
  }
}

NoteId Loop::allocateNoteId() {
  return nextNoteId_++;
}

LOOP_COLD_MEM void Loop::resetPassTimeline() {
  ++canonicalStorageGeneration; // Before releasing refs or resetting playbackRevision.
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
  passes.loopGeometries.clear();
  nextPassId_ = 1;
  nextNoteId_ = 1;
  nextMergeSequence_ = 0;
  nextOverdubSessionIndex_ = 1;
  currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
  lastCommittedPassId_ = kInvalidPassId;
  clearLastOverdubSeal();
  clearPublishedSnapshot();
  playbackRevision = 0;
  editStateDirty_ = false;
  visualCache.clear();
  capturePreview.clear();
  // playbackRevision restarts at 0 here, so a retained sealed revision could never be proven
  // covered again and would paint content these passes no longer hold.
  clearSealedCapturePreview();
  clearRetiredSealedDisplayNotes();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  clearOverdubSourceView();
  clearCommittedPlaybackNoteOnIdentities();
  clearPendingNoteChanges();
  closeOverdubSession();
}

void Loop::invalidateCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
  visualCacheDirty = true;
  playbackOrderDirty = true;
}

void Loop::invalidatePlaybackCaches() {
  playbackOrderDirty = true;
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
}
