//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

/// Gated MIDI polls around deferred remainder. Not a scheduler and not all-PLAYING drain.
/// Capture uses the existing after-display poll (RC-C C). Post-overdub PLAYING adds polls
/// around idle maintenance so that work cannot stack with load_frame and persist.
namespace MidiServiceDrain {

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

/// While PLAYING or OVERDUBBING and both persist and first-record snapshot are pending, they take turns.
/// When only one is pending, that owner always runs.
inline bool shouldRunSnapshotOnPostStopLane(bool snapshotPending, bool persistPending,
                                            bool persistLane) {
  if (!snapshotPending) {
    return false;
  }
  if (!persistPending) {
    return true;
  }
  return !persistLane;
}

inline bool shouldRunPersistOnPostStopLane(bool snapshotPending, bool persistPending,
                                           bool persistLane) {
  if (!persistPending) {
    return false;
  }
  if (!snapshotPending) {
    return true;
  }
  return persistLane;
}

/// During unpublished first-root construction, PLAYING/OVERDUBBING alternate snapshot
/// idle turns so one owner cannot monopolize consecutive timing-critical turns.
inline bool shouldRoundRobinSnapshotIdle(bool isPlaying, bool isOverdubbing,
                                         bool havePublishedSnapshot,
                                         bool hasInFlightConstruction,
                                         bool snapshotPending) {
  return (isPlaying || isOverdubbing) && !havePublishedSnapshot &&
         hasInFlightConstruction && snapshotPending;
}

/// When unpublished snapshot work is pending while PLAYING/OVERDUBBING, keep
/// visual fallback constrained so compatibility rebuild does not run an
/// unbounded neighborhood/search path on the same realtime window.
inline bool shouldConstrainVisualIdleForUnpublishedSnapshot(bool isPlaying,
                                                            bool isOverdubbing,
                                                            bool havePublishedSnapshot,
                                                            bool /*hasInFlightConstruction*/,
                                                            bool snapshotPending) {
  return (isPlaying || isOverdubbing) && !havePublishedSnapshot &&
         snapshotPending;
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
