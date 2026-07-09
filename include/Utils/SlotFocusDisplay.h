#pragma once

#include <cstdint>

/// True when preview slot differs from playing slot while transport is active on the track.
inline bool isPreviewPlayheadPending(uint8_t previewSlot, uint8_t playingSlot, bool transportActive) {
  return transportActive && previewSlot != playingSlot;
}

/// Toggle interval for flashing preview playhead (~250 ms).
inline bool previewPlayheadFlashVisible(uint32_t millisNow) {
  return (millisNow / 250U) % 2U == 0U;
}
