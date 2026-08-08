//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "EditManager.h"
#include "Globals.h"
#include "TrackManager.h"

extern TrackManager trackManager;

MidiEventVec& Track::legacyMidiEventsFromCommitted() {
  // Edit-path boundary only (EditManager::editMidiEvents when session inactive).
  // Display and playback use SessionMidiEventVec via getMidiEvents().
  Loop& loop = getActiveLoop();
  const uint32_t revision = loop.playbackRevision;
  if (committedMidiScratchRevision_ != revision) {
    loop.materializeEditViewFromPasses();
    const SessionMidiEventVec& committedLoopMidi = loop.midiEvents();
    committedMidiScratch_.assign(committedLoopMidi.begin(), committedLoopMidi.end());
    committedMidiScratchRevision_ = revision;
  }
  return committedMidiScratch_;
}

const MidiEventVec& Track::legacyMidiEventsFromCommitted() const {
  return const_cast<Track*>(this)->legacyMidiEventsFromCommitted();
}

MidiEventVec& Track::editAwareMidiEvents() {
  return editManager.editMidiEvents(*this);
}

const MidiEventVec& Track::editAwareMidiEvents() const {
  return editManager.editMidiEvents(*this);
}

void Track::invalidateLoopDerivedCaches() {
  committedMidiScratchRevision_ = UINT32_MAX;
  Loop& activeLoop = getActiveLoop();
  activeLoop.invalidateCaches();
  activeLoop.playbackOrderDirty = true;
  if (!editManager.isNoteEditActive()) {
    return;
  }
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    if (&trackManager.getTrack(trackIndex) != this) {
      continue;
    }
    const uint8_t selectedSlot = trackManager.getSelectedSlotIndex(trackIndex);
    if (selectedSlot != activeLoopIndex) {
      getLoop(selectedSlot).invalidateCaches();
    }
    break;
  }
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
