//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The one owner of the slot-clear playback restore. See `include/SlotClearPlaybackRestore.h` for why it
// is a translation unit of its own (A3.4a's MIDI-dispatch fixtures link both the button routes and
// `TrackUndo.cpp`, which previously defined this helper twice).

#include "SlotClearPlaybackRestore.h"

#include "Globals.h"
#include "Track.h"
#include "TrackManager.h"

extern TrackManager trackManager;

TRACK_COLD_MEM void restorePlaybackAfterSlotClear(uint8_t trackIndex, Track& track, uint32_t now) {
  bool foundPlaybackSlot = false;
  uint8_t newActiveSlot = 0;
  for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (trackManager.isSlotEnabled(trackIndex, s) && !trackManager.isSlotMuted(trackIndex, s) &&
        track.hasDataInSlot(s)) {
      foundPlaybackSlot = true;
      newActiveSlot = s;
      break;
    }
  }
  if (foundPlaybackSlot) {
    trackManager.setActiveLoopIndex(trackIndex, newActiveSlot);
    for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (trackManager.isSlotEnabled(trackIndex, s) && !trackManager.isSlotMuted(trackIndex, s) &&
          track.hasDataInSlot(s)) {
        track.resetPlaybackStateForSlot(s, now);
      }
    }
    track.forceSetState(TRACK_STOPPED);
    track.startPlaying(now);
  } else {
    track.sendAllNotesOff();
    if (track.isPlaying()) {
      track.forceSetState(TRACK_STOPPED);
    }
  }
}
