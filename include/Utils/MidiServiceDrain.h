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

/// Snapshot idle admits two construction units per turn (DEC-053). Each unit
/// remains one row-21 step or one journal-capped fragment start.
constexpr uint8_t kSnapshotIdleConstructionUnitsPerTurn = 2;

/// Policy ceiling on construction-attributable work per Snapshot idle turn.
constexpr uint32_t kSnapshotIdleReserveCeilingUs = 10000u;

/// Working hypothesis only; DEC-053 leaves T_RT_safety_margin unfrozen.
constexpr uint32_t kSnapshotIdleSafetyMarginUs = 1000u;

/// Classified cost of one Snapshot construction unit. Not an observed freeze.
constexpr uint32_t kSnapshotIdleConstructionUnitCostUs = 4000u;

inline uint32_t snapshotIdleConstructionCostUs(uint8_t units) {
  return static_cast<uint32_t>(units) * kSnapshotIdleConstructionUnitCostUs;
}

inline uint32_t admittedSnapshotIdleUs(uint32_t remainingUs) {
  const uint32_t afterMargin = remainingUs > kSnapshotIdleSafetyMarginUs
                                   ? remainingUs - kSnapshotIdleSafetyMarginUs
                                   : 0u;
  return afterMargin < kSnapshotIdleReserveCeilingUs ? afterMargin
                                                     : kSnapshotIdleReserveCeilingUs;
}

/// Remaining-interval reject is parked (DEC-053 yield device proof).
/// `allowSnapshotIdleReserve` and capture `snapshot_idle,yield` stay compiled.
constexpr bool kSnapshotIdleReserveYieldEnabled = false;

/// Timing-critical Snapshot idle admits only when the classified N-unit cost
/// fits the remaining interval. STOPPED / non-critical turns always admit.
/// Callers honor this only when `kSnapshotIdleReserveYieldEnabled` is true.
inline bool allowSnapshotIdleReserve(uint32_t remainingUs, bool timingCritical,
                                     uint8_t units = kSnapshotIdleConstructionUnitsPerTurn) {
  if (!timingCritical) {
    return true;
  }
  return snapshotIdleConstructionCostUs(units) <= admittedSnapshotIdleUs(remainingUs);
}

inline DeferredIdleOwner nextDeferredIdleOwner(DeferredIdleOwner owner) {
  const uint8_t next =
      static_cast<uint8_t>(owner) + static_cast<uint8_t>(1);
  return static_cast<DeferredIdleOwner>(next % kDeferredIdleOwnerCount);
}

/// 5-owner cursor plus the after-cycle extra Snapshot slot used while a
/// publication backlog is present. Caller-owned; no content-size input.
struct DeferredIdleOwnerCursor {
  DeferredIdleOwner owner = DeferredIdleOwner::Snapshot;
  bool extraSnapshotPending = false;
};

/// Ordinary 5-owner identity, or the extra Snapshot after a completed cycle
/// when `snapshotBacklogPending` is true. Boolean backlog-present only.
inline DeferredIdleOwner selectDeferredIdleOwner(DeferredIdleOwnerCursor& cursor,
                                                 bool snapshotBacklogPending,
                                                 bool sessionExitDrainPending = false) {
  if (cursor.extraSnapshotPending) {
    cursor.extraSnapshotPending = false;
    if (snapshotBacklogPending || sessionExitDrainPending) {
      return DeferredIdleOwner::Snapshot;
    }
  }
  const DeferredIdleOwner selected = cursor.owner;
  cursor.owner = nextDeferredIdleOwner(cursor.owner);
  if (sessionExitDrainPending) {
    cursor.extraSnapshotPending = selected != DeferredIdleOwner::Snapshot;
  } else {
    cursor.extraSnapshotPending =
        snapshotBacklogPending && selected == DeferredIdleOwner::LoadAndDisplay;
  }
  return selected;
}

inline DeferredIdleOwner selectDeferredIdleOwner(DeferredIdleOwner owner,
                                                 bool snapshotBacklogPending,
                                                 bool sessionExitDrainPending = false) {
  DeferredIdleOwnerCursor cursor;
  cursor.owner = owner;
  return selectDeferredIdleOwner(cursor, snapshotBacklogPending, sessionExitDrainPending);
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

/// DEC-045 amendment: OVERDUBBING idle does not reconstruct `visualCache`.
/// Source-view refresh stays on its own overdub path. Slices resume on
/// PLAYING / STOPPED_RECORDING.
inline bool allowVisualCacheReconstructSlice(bool isOverdubbing) {
  return !isOverdubbing;
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
/// the playhead has left bar 1, and start on PLAYING. OVERDUBBING advances an
/// in-flight candidate and may start the next bounded fragment only while no
/// published root exists and session-exit is queued (`DEC-045` 2026-09-12).
/// After a published root exists, PLAYING / OVERDUBBING may step pending bounded
/// successors, including wrap-journal and session-exit pass work.
/// Empty dirty visualCache still wins on STOPPED / STOPPED_RECORDING (`231632`)
/// when no snapshot work is pending. Pending session-exit, in-flight, or
/// wrap-journal work wins over that first-paint predicate: STOPPED ends
/// active MIDI ownership; it does not mean pending snapshot work is complete.
inline bool allowSnapshotIdleStep(bool isRecording, bool isPlaying, bool isOverdubbing,
                                  bool havePublishedSnapshot, bool hasInFlightConstruction,
                                  bool visualCacheNeedsFirstPaint,
                                  bool hasSuccessorJournalWork = false,
                                  bool hasPendingSnapshotWork = false) {
  if (havePublishedSnapshot && (isRecording || isPlaying || isOverdubbing)) {
    return hasInFlightConstruction || hasSuccessorJournalWork || hasPendingSnapshotWork;
  }
  if (isOverdubbing) {
    return hasInFlightConstruction || hasSuccessorJournalWork || hasPendingSnapshotWork;
  }
  if (isRecording || isPlaying) {
    return true;
  }
  if (visualCacheNeedsFirstPaint) {
    return hasInFlightConstruction || hasSuccessorJournalWork || hasPendingSnapshotWork;
  }
  return true;
}

/// Whether `stepPendingSnapshotPublish` may call `startNextPendingConstruction`.
/// OVERDUBBING normally steps in-flight work only; before the first published
/// root, session-exit publication must still start the next bounded fragment.
inline bool allowSnapshotConstructionStart(bool isOverdubbing, bool liveRecordPastFirstBar,
                                           bool hasWrapJournal, bool havePublishedSnapshot,
                                           bool sessionExitQueued) {
  if ((!isOverdubbing && liveRecordPastFirstBar) || hasWrapJournal) {
    return true;
  }
  return !havePublishedSnapshot && sessionExitQueued;
}

/// STOPPED idle may keep the transport clock running. Snapshot is still
/// admitted when any slot still has pending publication work.
inline bool allowStoppedClockSnapshotStep(bool allTracksStopped, bool clockRunning,
                                          bool anyPendingSnapshotWork) {
  return !allTracksStopped || !clockRunning || anyPendingSnapshotWork;
}

/// A STOPPED track must not construct snapshot work on another track's live
/// MIDI interval. session_20260908_003555 rebooted when T6 loaded
/// session-exit construction continued during T0 RECORDING.
inline bool allowStoppedTrackSnapshotDuringRealtime(bool trackIsRealtime,
                                                    bool anyRealtimeTrackActive) {
  return !anyRealtimeTrackActive || trackIsRealtime;
}

/// Title drain keeps LoadLoopJob on the load-frame owner. Loaded snapshot
/// construction starts after title exit (`020111` stayed on title for 112 s
/// while focus T0/S5 climbed to 36k snapshot nodes).
inline bool allowSnapshotIdleDuringBootTitleDrain(bool bootTitleLoadDrain,
                                                  bool bootRestoreComplete = false) {
  (void)bootRestoreComplete;
  return !bootTitleLoadDrain;
}

/// Automatic loaded snapshot construction is off (`023222` reboot on loaded
/// `start`). Session-exit publication is unchanged. Explicit selected-slot
/// hydration is a later slice.
inline bool allowLoadedSnapshotHydration(bool anyRealtimeTrackActive,
                                         bool bootTitleLoadDrain,
                                         bool bootRestoreComplete) {
  (void)anyRealtimeTrackActive;
  (void)bootTitleLoadDrain;
  (void)bootRestoreComplete;
  return false;
}

/// Prefer a track with pending snapshot work on a STOPPED Snapshot turn.
/// Walks from `cursor`. Returns `cursor` when no pending flag is set.
inline uint8_t selectStoppedSnapshotTrack(uint8_t cursor, uint8_t trackCount,
                                          const bool* pendingByTrack) {
  if (trackCount == 0 || pendingByTrack == nullptr) {
    return 0;
  }
  if (cursor >= trackCount) {
    cursor = 0;
  }
  for (uint8_t offset = 0; offset < trackCount; ++offset) {
    const uint8_t index = static_cast<uint8_t>((cursor + offset) % trackCount);
    if (pendingByTrack[index]) {
      return index;
    }
  }
  return cursor;
}

/// Title drain constructs only the focus loop. Neighbor loaded publication
/// (66-bar T6 in session_20260908_015331) resets the device before boot_commit.
inline uint8_t selectBootTitleSnapshotTrack(bool bootTitleLoadDrain, uint8_t focusTrack,
                                            uint8_t cursor, uint8_t trackCount,
                                            const bool* pendingByTrack) {
  if (bootTitleLoadDrain) {
    return focusTrack;
  }
  return selectStoppedSnapshotTrack(cursor, trackCount, pendingByTrack);
}

/// After title: hydrate only the selected track. `022447` published the new
/// record loop, then a neighbor loaded construction (`ld=1`, `n=8337`)
/// reconnects. `pendingByTrack` / `inFlightByTrack` stay for the caller scan.
inline uint8_t selectStoppedLoadedSnapshotTrack(uint8_t focusTrack, uint8_t cursor,
                                               uint8_t trackCount,
                                               const bool* pendingByTrack,
                                               const bool* inFlightByTrack) {
  (void)cursor;
  (void)pendingByTrack;
  (void)inFlightByTrack;
  if (trackCount == 0) {
    return 0;
  }
  if (focusTrack < trackCount) {
    return focusTrack;
  }
  return 0;
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
