//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "EditManager.h"
#include "Globals.h"
#include "TrackManager.h"
#include "Utils/CommittedMidiScratch.h"

extern TrackManager trackManager;

// FLASHMEM: legacy note-edit access, not a MIDI-edge path; moved out of ITCM to
// keep the S4f.19 onward-service call site inside the RAM1 floor.
TRACK_COLD_MEM MidiEventVec& Track::legacyMidiEventsFromCommitted() {
  // Edit-path boundary only (EditManager::editMidiEvents when session inactive).
  // OpenSpec 4.6 Stage 5 / 6.3.3: published-snapshot content + revision/identity freshness.
  (void)CommittedMidiScratch::refresh(committedMidiScratch_, committedMidiScratchRevision_,
                                      committedMidiScratchPublishedIdentity_, getActiveLoop());
  return committedMidiScratch_;
}

TRACK_COLD_MEM const MidiEventVec& Track::legacyMidiEventsFromCommitted() const {
  return const_cast<Track*>(this)->legacyMidiEventsFromCommitted();
}

MidiEventVec& Track::editAwareMidiEvents() {
  return editManager.editMidiEvents(*this);
}

const MidiEventVec& Track::editAwareMidiEvents() const {
  return editManager.editMidiEvents(*this);
}

void Track::invalidateLoopDerivedCachesForSlot(uint8_t slot) {
  CommittedMidiScratch::invalidate(committedMidiScratchRevision_,
                                   committedMidiScratchPublishedIdentity_);
  Loop& loop = getLoop(slot);
  loop.invalidateCaches();
  loop.playbackOrderDirty = true;
  if (!editManager.isNoteEditActive()) {
    return;
  }
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    if (&trackManager.getTrack(trackIndex) != this) {
      continue;
    }
    const uint8_t selectedSlot = trackManager.getSelectedSlotIndex(trackIndex);
    if (selectedSlot != slot) {
      getLoop(selectedSlot).invalidateCaches();
    }
    break;
  }
}

void Track::invalidateLoopDerivedCaches() {
  invalidateLoopDerivedCachesForSlot(activeLoopIndex);
}

TRACK_COLD_MEM void Track::invalidateCommittedContentForSlot(uint8_t slot,
                                                             bool queueRecordRevts) {
  if (!loopsAllocated() || slot >= Config::MAX_LOOPS_PER_TRACK) return;
  // S4f.20/S4f.21 parity. The deferred publication's atomic handoff already called
  // `Loop::notifyCommittedContentChanged`, which is `markPassDerivedStale` -- a superset of
  // `Loop::invalidateCaches()` for this loop (playbackOrderDirty, visualCacheDirty,
  // invalidatePlaybackCaches). What it does **not** cover, and the inline
  // `finalizeCommitSideEffects` did, is the Track-level committed-MIDI scratch, the note-edit
  // selected-slot invalidation, and the deferred REVT queue. Those are applied here against the
  // **job's** slot: the publication runs while a successor may own the active slot, so the
  // active-index form would invalidate (and queue against) the wrong interval.
  invalidateLoopDerivedCachesForSlot(slot);
  if (queueRecordRevts) queueDeferredRecordRevts(slot);
}

void Track::invalidateCaches(bool refreshPlaybackPreview) {
  if (editManager.isNoteEditActive()) {
    // Session-store overlay only — committed loop visual cache stays valid until bake.
    editManager.bumpSessionPreviewRevision();
    if (refreshPlaybackPreview) {
      if (isPlaying()) {
        editManager.scheduleDeferredNoteEditDisplayRefresh();
      } else {
        editManager.bumpSessionPlaybackPreviewRevision();
        getActiveLoop().playbackOrderDirty = true;
      }
    }
    return;
  }
  invalidateLoopDerivedCaches();
}
