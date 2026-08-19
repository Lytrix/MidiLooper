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

/// Complete committed playback window only. A partial gather must not invalidate
/// ledger identities.
inline bool isFullLoopMergedPlaybackWindow(const PlaybackMergedMidiEvents& merged,
                                           uint32_t loopLengthTicks) {
  return loopLengthTicks > 0 && merged.windowStartTick == 0 &&
         merged.windowLengthTicks == loopLengthTicks;
}
