#pragma once

#include <cstddef>
#include <cstdint>

/// Priority for deferred loop-slot restore at boot (lower = sooner).
/// 0 = selected track's selected (UI focus) slot
/// 1 = selected track's active slot when it differs from selected
/// 2 = other slots on the selected track
/// 3 = all other tracks
inline uint8_t computeBootRestorePriority(uint8_t trackIndex, uint8_t slotIndex,
                                          uint8_t selectedTrackIdx,
                                          const uint8_t* activeLoopIndex, size_t activeCount,
                                          const uint8_t* selectedSlotIndex, size_t selectedCount) {
  if (trackIndex == selectedTrackIdx) {
    const uint8_t activeSlot =
        trackIndex < activeCount ? activeLoopIndex[trackIndex] : static_cast<uint8_t>(0);
    const uint8_t selectedSlot = trackIndex < selectedCount ? selectedSlotIndex[trackIndex]
                                                           : activeSlot;
    if (slotIndex == selectedSlot) {
      return 0;
    }
    if (slotIndex == activeSlot) {
      return 1;
    }
    return 2;
  }
  return 3;
}
