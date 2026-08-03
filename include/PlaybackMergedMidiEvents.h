#pragma once

#include <cstdint>

#include "MidiEvent.h"

/// Scratch merge cache for one loop playback path (not the musical PlaybackWindow domain object).
struct PlaybackMergedMidiEvents {
  uint32_t builtFromRevision = 0;
  /// For long loops: tick range covered by `mergedEvents` (windowed gather).
  uint32_t windowStartTick = 0;
  uint32_t windowLengthTicks = 0;
  SessionMidiEventVec mergedEvents;

  void clear() {
    builtFromRevision = 0;
    windowStartTick = 0;
    windowLengthTicks = 0;
    mergedEvents.clear();
  }

  bool empty() const { return mergedEvents.empty(); }
};
