#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "Globals.h"

/// Runtime playback cursor for one loop playback path.
struct PlaybackCursor {
  uint16_t windowStartBar = 0;
  uint8_t effectiveWindowBars = Config::PLAYBACK_WINDOW_MIN_BARS;
  uint32_t lastTickInLoop = std::numeric_limits<uint32_t>::max();
  size_t mergeCursorIndex = 0;
  uint32_t cachedLoopRevision = 0;
  uint32_t cachedTrackGeneration = 0;
  bool loopHeadReady = false;
  uint32_t loopHeadRevision = 0;

  void reset(bool preserveLedger = false) {
    windowStartBar = 0;
    effectiveWindowBars = Config::PLAYBACK_WINDOW_MIN_BARS;
    lastTickInLoop = std::numeric_limits<uint32_t>::max();
    mergeCursorIndex = 0;
    loopHeadReady = false;
    loopHeadRevision = 0;
    if (!preserveLedger) {
      cachedLoopRevision = 0;
      cachedTrackGeneration = 0;
    }
  }

  bool isStale(uint32_t loopRevision, uint32_t trackGeneration) const {
    return cachedLoopRevision != loopRevision || cachedTrackGeneration != trackGeneration;
  }

  void syncRevision(uint32_t loopRevision, uint32_t trackGeneration) {
    cachedLoopRevision = loopRevision;
    cachedTrackGeneration = trackGeneration;
  }
};
