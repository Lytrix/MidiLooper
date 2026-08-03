#pragma once

#include <cstddef>
#include <cstdint>

/// True if this slot must be Committed at boot for first Play / piano-roll focus.
///
/// While stopped, `loadTransportSlotIndices` remaps `active := selected`, so playback
/// uses the **selected** slot. Boot must therefore restore **union(selected, active)**
/// on every track when those slots have SD payload — not file-active alone, and not
/// only the focus track's selected slot.
inline bool isBootPlaybackSlot(uint8_t trackIndex, uint8_t slotIndex, uint8_t /*selectedTrackIdx*/,
                               const uint8_t* activeLoopIndex, size_t activeCount,
                               const uint8_t* selectedSlotIndex, size_t selectedCount) {
  const uint8_t activeSlot =
      trackIndex < activeCount ? activeLoopIndex[trackIndex] : static_cast<uint8_t>(0);
  if (slotIndex == activeSlot) {
    return true;
  }
  const uint8_t selectedSlot =
      trackIndex < selectedCount ? selectedSlotIndex[trackIndex] : activeSlot;
  return slotIndex == selectedSlot;
}

/// Deferred restore priority (lower = sooner). Used for boot playback ordering and
/// post-boot background fill of remaining HEADER_READY slots.
///
/// Focus track (selectedTrackIdx):
///   0 = selected slot
///   1 = left neighbor (wrap within track)
///   2 = right neighbor (wrap within track)
///   10 + slotIndex = other slots on focus track
/// Other tracks (forward cycle from focus):
///   100 + trackDistance * maxLoopsPerTrack + slotIndex
///     trackDistance = (track - selectedTrack + numTracks) % numTracks
inline uint16_t computeDeferredRestorePriority(uint8_t trackIndex, uint8_t slotIndex,
                                               uint8_t selectedTrackIdx,
                                               const uint8_t* selectedSlotIndex,
                                               size_t selectedCount, size_t numTracks,
                                               size_t maxLoopsPerTrack) {
  if (maxLoopsPerTrack == 0 || numTracks == 0) {
    return 0xFFFFu;
  }
  const uint8_t focusSelected =
      selectedTrackIdx < selectedCount ? selectedSlotIndex[selectedTrackIdx]
                                       : static_cast<uint8_t>(0);
  const uint8_t focusSlot =
      focusSelected < maxLoopsPerTrack ? focusSelected : static_cast<uint8_t>(0);

  if (trackIndex == selectedTrackIdx) {
    if (slotIndex == focusSlot) {
      return 0;
    }
    const uint8_t left =
        static_cast<uint8_t>((focusSlot + maxLoopsPerTrack - 1u) % maxLoopsPerTrack);
    if (slotIndex == left) {
      return 1;
    }
    const uint8_t right = static_cast<uint8_t>((focusSlot + 1u) % maxLoopsPerTrack);
    if (slotIndex == right) {
      return 2;
    }
    return static_cast<uint16_t>(10u + slotIndex);
  }

  const uint16_t trackDistance = static_cast<uint16_t>(
      (static_cast<uint16_t>(trackIndex) + static_cast<uint16_t>(numTracks) -
       static_cast<uint16_t>(selectedTrackIdx)) %
      static_cast<uint16_t>(numTracks));
  return static_cast<uint16_t>(100u + trackDistance * static_cast<uint16_t>(maxLoopsPerTrack) +
                               slotIndex);
}

/// Alias for boot playback-set sort (same priority model as runtime fill).
inline uint16_t computeBootRestorePriority(uint8_t trackIndex, uint8_t slotIndex,
                                           uint8_t selectedTrackIdx,
                                           const uint8_t* /*activeLoopIndex*/, size_t /*activeCount*/,
                                           const uint8_t* selectedSlotIndex, size_t selectedCount,
                                           size_t numTracks, size_t maxLoopsPerTrack) {
  return computeDeferredRestorePriority(trackIndex, slotIndex, selectedTrackIdx, selectedSlotIndex,
                                        selectedCount, numTracks, maxLoopsPerTrack);
}
