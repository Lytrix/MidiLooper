#pragma once

#include <cstddef>
#include <cstdint>

/// True if this slot must be Committed at boot for first Play / piano-roll focus.
///
/// While stopped, `loadTransportSlotIndices` remaps `active := selected`, so playback
/// uses the **selected** slot. Boot must therefore restore **union(selected, active)**
/// on every track when those slots have SD payload — not file-active alone, and not
/// only the focus track's selected slot.
inline bool isAudibleBootSlot(uint8_t trackIndex, uint8_t slotIndex, uint8_t /*selectedTrackIdx*/,
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

/// Priority for deferred loop-slot restore at boot (lower = sooner).
/// Boot enqueue uses `isAudibleBootSlot` (union selected+active); this orders that set.
/// Other SD payloads stay HEADER_READY until explicitly requested at runtime.
/// 0 = selected and/or active (boot playback set)
/// 1 = unused at boot enqueue (reserved)
/// 2 = all other slots (not enqueued at boot)
inline uint8_t computeBootRestorePriority(uint8_t trackIndex, uint8_t slotIndex,
                                          uint8_t selectedTrackIdx,
                                          const uint8_t* activeLoopIndex, size_t activeCount,
                                          const uint8_t* selectedSlotIndex, size_t selectedCount) {
  if (isAudibleBootSlot(trackIndex, slotIndex, selectedTrackIdx, activeLoopIndex, activeCount,
                         selectedSlotIndex, selectedCount)) {
    return 0;
  }
  const uint8_t selectedSlot =
      trackIndex < selectedCount
          ? selectedSlotIndex[trackIndex]
          : (trackIndex < activeCount ? activeLoopIndex[trackIndex] : static_cast<uint8_t>(0));
  if (slotIndex == selectedSlot) {
    return 1;
  }
  return 2;
}
