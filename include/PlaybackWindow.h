#pragma once

#include <cstdint>

#include "MidiEvent.h"

/// Scratch merge cache for one loop playback path.
struct PlaybackWindow {
  uint32_t builtFromRevision = 0;
  MidiEventVec mergedEvents;

  void clear() {
    builtFromRevision = 0;
    mergedEvents.clear();
  }

  bool empty() const { return mergedEvents.empty(); }
};
