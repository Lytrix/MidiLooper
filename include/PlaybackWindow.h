#pragma once

#include <cstdint>

#include "MidiEvent.h"

/// Scratch playback window data for one loop path.
struct PlaybackWindow {
  uint16_t windowStartBar = 0;
  uint8_t effectiveWindowBars = 0;
  uint32_t builtFromRevision = 0;
  MidiEventVec mergedEvents;

  void clear() {
    windowStartBar = 0;
    effectiveWindowBars = 0;
    builtFromRevision = 0;
    mergedEvents.clear();
  }

  bool empty() const { return mergedEvents.empty(); }
};
