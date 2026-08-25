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

}  // namespace MidiServiceDrain
