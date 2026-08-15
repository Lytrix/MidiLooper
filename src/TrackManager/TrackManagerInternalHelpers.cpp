//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManagerInternal.h"

#include "Globals.h"

TRACK_MANAGER_INTERNAL_MEM void replaceSingleEnabledSlotWithTarget(
    bool slotEnabled[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK],
    bool slotMuted[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK], uint8_t trackIndex,
    uint8_t targetSlot, bool replaceLayeredSet) {
  if (trackIndex >= Config::NUM_TRACKS || targetSlot >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  if (!replaceLayeredSet) {
    uint8_t enabledCount = 0;
    uint8_t enabledSlot = Config::INVALID_LOOP_SLOT;
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      if (slotEnabled[trackIndex][slot]) {
        enabledCount++;
        enabledSlot = slot;
        if (enabledCount > 1) {
          return;  // layered set already, keep as-is while playing
        }
      }
    }
    if (enabledCount != 1 || enabledSlot == Config::INVALID_LOOP_SLOT ||
        enabledSlot == targetSlot) {
      return;
    }
  }
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    slotEnabled[trackIndex][slot] = (slot == targetSlot);
    if (slot != targetSlot) {
      slotMuted[trackIndex][slot] = false;
    }
  }
}

TRACK_MANAGER_INTERNAL_MEM uint8_t reclaimTrackPriority(uint8_t trackIndex,
                                                        const TrackManager& manager,
                                                        const Track& track) {
  if (manager.isSelectedTrack(track)) {
    return 3;
  }
  if (track.isRecording() || track.isOverdubbing() || track.getState() == TRACK_ARMED) {
    return 2;
  }
  if (track.isPlaying()) {
    return 1;
  }
  (void)trackIndex;
  return 0;
}

TRACK_MANAGER_INTERNAL_MEM uint8_t resolveTrackIndex(const Track& track) {
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    if (&trackManager.getTrack(trackIndex) == &track) {
      return trackIndex;
    }
  }
  return trackManager.getSelectedTrackIndex();
}
