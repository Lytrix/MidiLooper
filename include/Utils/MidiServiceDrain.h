//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Gated MIDI polls around deferred remainder. Not a scheduler and not all-PLAYING drain.
/// Capture uses the existing after-display poll (RC-C C). Post-overdub PLAYING adds polls
/// around idle maintenance so that work cannot stack with load_frame and persist.
namespace MidiServiceDrain {

enum class DeferredIdleOwner : uint8_t {
  Snapshot = 0,
  VisualCache = 1,
  VisualCacheWrapAppend = 2,
  Persistence = 3,
  LoadAndDisplay = 4,
};

constexpr uint8_t kDeferredIdleOwnerCount = 5;

inline DeferredIdleOwner nextDeferredIdleOwner(DeferredIdleOwner owner) {
  const uint8_t next =
      static_cast<uint8_t>(owner) + static_cast<uint8_t>(1);
  return static_cast<DeferredIdleOwner>(next % kDeferredIdleOwnerCount);
}

inline bool deferredIdleOwnerDependsOnVisualCacheWrap(
    DeferredIdleOwner owner) {
  return owner == DeferredIdleOwner::VisualCache ||
         owner == DeferredIdleOwner::LoadAndDisplay;
}

inline bool allowDeferredIdleOwner(DeferredIdleOwner owner,
                                   bool wrapAppendPending) {
  return !wrapAppendPending ||
         !deferredIdleOwnerDependsOnVisualCacheWrap(owner);
}

inline bool aroundIdleMaintenance(bool postOverdubPlayingDrain) {
  return postOverdubPlayingDrain;
}

inline bool afterDeferredDisplay(bool captureActive, bool postOverdubPlayingDrain) {
  return captureActive || postOverdubPlayingDrain;
}

inline bool aroundDeferredLoadFrame(bool postOverdubPlayingDrain) {
  return postOverdubPlayingDrain;
}

inline bool allowDeferredSave(bool postOverdubPlayingDrain, bool deferredSavePending) {
  // During the post-overdub drain window, do not stack pending save work into the same
  // PLAYING turn that is already draining idle maintenance debt.
  return !postOverdubPlayingDrain || !deferredSavePending;
}

inline bool allowDeferredSaveAfterLoadFrame(bool timingCriticalTrackActive,
                                            uint32_t loadFrameDurationUs) {
  // In active timing windows, defer save when the same-turn load_frame slice is already
  // heavy. This keeps save work resumable and avoids avoidable load+save stacking gaps.
  constexpr uint32_t kActiveLoadFrameSaveDeferThresholdUs = 15000u;
  return !timingCriticalTrackActive || loadFrameDurationUs < kActiveLoadFrameSaveDeferThresholdUs;
}

/// First-record uses the overdub live-enqueue setup: start on RECORDING after
/// the playhead has left bar 1, and start on PLAYING. OVERDUBBING only advances
/// an in-flight candidate (`allowStart` is false there). After a published root
/// exists, PLAYING / OVERDUBBING / RECORDING do not step successors (DEC-045).
/// Empty dirty visualCache still wins on STOPPED / STOPPED_RECORDING (`231632`).
inline bool allowSnapshotIdleStep(bool isRecording, bool isPlaying, bool isOverdubbing,
                                  bool havePublishedSnapshot, bool hasInFlightConstruction,
                                  bool visualCacheNeedsFirstPaint) {
  if (havePublishedSnapshot && (isRecording || isPlaying || isOverdubbing)) {
    return false;
  }
  if (isOverdubbing) {
    return hasInFlightConstruction;
  }
  if (isRecording || isPlaying) {
    return true;
  }
  if (visualCacheNeedsFirstPaint) {
    return false;
  }
  return true;
}

/// First-record live construction waits until `currentTick` has left bar 1.
/// `loopLengthTicks` can already be one bar while that bar is still being
/// written (session_20260826_114012).
inline bool allowLiveRecordSnapshotStart(bool isRecording, uint32_t recordElapsedTicks,
                                         uint32_t ticksPerBar) {
  if (!isRecording) {
    return true;
  }
  return recordElapsedTicks >= ticksPerBar;
}

/// Exclusive end of completed first-record bars. The bar still being written
/// is not included.
inline uint32_t liveRecordCompletedTicks(uint32_t recordElapsedTicks, uint32_t ticksPerBar) {
  if (ticksPerBar == 0 || recordElapsedTicks < ticksPerBar) {
    return 0;
  }
  return (recordElapsedTicks / ticksPerBar) * ticksPerBar;
}

}  // namespace MidiServiceDrain
