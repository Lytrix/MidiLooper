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

/// 5-owner cursor plus the after-cycle extra Snapshot slot used while a
/// publication backlog is present. Caller-owned; no content-size input.
struct DeferredIdleOwnerCursor {
  DeferredIdleOwner owner = DeferredIdleOwner::Snapshot;
  bool extraSnapshotPending = false;
};

/// Ordinary 5-owner identity, or the extra Snapshot after a completed cycle
/// when `snapshotBacklogPending` is true. Boolean backlog-present only.
inline DeferredIdleOwner selectDeferredIdleOwner(DeferredIdleOwnerCursor& cursor,
                                                 bool snapshotBacklogPending) {
  if (cursor.extraSnapshotPending) {
    cursor.extraSnapshotPending = false;
    if (snapshotBacklogPending) {
      return DeferredIdleOwner::Snapshot;
    }
  }
  const DeferredIdleOwner selected = cursor.owner;
  cursor.owner = nextDeferredIdleOwner(cursor.owner);
  cursor.extraSnapshotPending =
      snapshotBacklogPending && selected == DeferredIdleOwner::LoadAndDisplay;
  return selected;
}

inline DeferredIdleOwner selectDeferredIdleOwner(DeferredIdleOwner owner,
                                                 bool snapshotBacklogPending) {
  DeferredIdleOwnerCursor cursor;
  cursor.owner = owner;
  return selectDeferredIdleOwner(cursor, snapshotBacklogPending);
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
/// an in-flight candidate unless a wrap journal is pending (`DEC-047`). After a
/// published root exists, PLAYING / OVERDUBBING may step pending bounded
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

/// STOPPED idle may keep the transport clock running. Snapshot is still
/// admitted when any slot still has pending publication work.
inline bool allowStoppedClockSnapshotStep(bool allTracksStopped, bool clockRunning,
                                          bool anyPendingSnapshotWork) {
  return !allTracksStopped || !clockRunning || anyPendingSnapshotWork;
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
