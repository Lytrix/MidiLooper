#pragma once

#include <cstddef>
#include <cstdint>

/// True if this slot is priority-0 for boot restore drain order:
/// - every track's active slot, and
/// - selected-track selected slot when it differs from active (focus piano roll).
inline bool isAudibleBootSlot(uint8_t trackIndex, uint8_t slotIndex, uint8_t selectedTrackIdx,
                              const uint8_t* activeLoopIndex, size_t activeCount,
                              const uint8_t* selectedSlotIndex, size_t selectedCount) {
  const uint8_t activeSlot =
      trackIndex < activeCount ? activeLoopIndex[trackIndex] : static_cast<uint8_t>(0);
  if (slotIndex == activeSlot) {
    return true;
  }
  if (trackIndex != selectedTrackIdx) {
    return false;
  }
  const uint8_t selectedSlot =
      trackIndex < selectedCount ? selectedSlotIndex[trackIndex] : activeSlot;
  return slotIndex == selectedSlot;
}

/// Priority for deferred loop-slot restore at boot (lower = sooner).
/// Boot enqueue is **audible-only** (`isAudibleBootSlot`); this priority orders that set.
/// Non-audible SD payloads stay HEADER_READY until explicitly requested at runtime.
/// 0 = active slots all tracks + focus selected if split
/// 1 = selected slot on other tracks (not used at boot enqueue while audible-only)
/// 2 = all other slots (not used at boot enqueue while audible-only)
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
