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
  /// S4b.7 (Rev 20.3/21.7): the off-clock owner of a **pending slot switch's inbound occupancy**. A switch
  /// may not take its runtime on the MIDI edge that requests it nor on the clock path that commits it, so
  /// this turn does both halves the contract names: the provisioning and the target's merged-window build.
  /// Its work is state-driven (`slotProvisionNeeded`), so a pending switch needs no separate request state.
  SlotProvision = 5,
  /// DEC-108 item 1 / `ExecutionBounds.md` rows 27-29: the **window-build/publication owner**
  /// (`Track::stepPlaybackViewMaintenance`). It serves **one declared grain** of an owed window per admitted
  /// turn, off-clock, and reaches no consumer in this slice. Its work is state-driven -- an owed window *is*
  /// the obligation -- so no separate request record is involved, exactly as a pending switch *is* the
  /// `SlotProvision` turn's work.
  PlaybackViewMaintenance = 6,
};

// Overdub paint dirtiness cannot prioritize reconstruction that is disabled
// during capture. Wrap append and source-span work still precede prework.
inline bool canonicalPreparationHasPriorityWork(bool overdubbing, bool visualDirty,
                                                bool wrapAppend, bool sourceSpansReady) {
  return (!overdubbing && visualDirty) || wrapAppend || (overdubbing && !sourceSpansReady);
}

// One continuous canonical child in VisualCache or an unused wrap-append turn. Existing
// stop/visual work has priority across all tracks, not just the selected track.
constexpr uint8_t kCanonicalPrefixInputQuantum = 32;
template <typename HasPriorityWork, typename StepPrefix>
bool serviceCanonicalPrefixTurn(DeferredIdleOwner owner, uint8_t trackCount,
                                uint8_t& nextTrack, HasPriorityWork hasPriorityWork,
                                StepPrefix stepPrefix) {
  if ((owner != DeferredIdleOwner::VisualCache && owner != DeferredIdleOwner::VisualCacheWrapAppend) ||
      trackCount == 0) return false;
  for (uint8_t i = 0; i < trackCount; ++i)
    if (hasPriorityWork(i)) return false;
  for (uint8_t offset = 0; offset < trackCount; ++offset) {
    const uint8_t index = static_cast<uint8_t>((nextTrack + offset) % trackCount);
    if (stepPrefix(index)) {
      nextTrack = static_cast<uint8_t>((index + 1) % trackCount);
      return true;
    }
  }
  return false;
}

/// The LoadAndDisplay owner may run only one expensive child while a track is
/// timing-critical. The unselected child remains pending for a later owner turn.
enum class ActiveLoadDisplayChild : uint8_t { None, Display, LoadJob };

inline ActiveLoadDisplayChild selectActiveLoadDisplayChild(bool displayDue,
                                                           bool loadJobPending) {
  if (displayDue) {
    return ActiveLoadDisplayChild::Display;
  }
  return loadJobPending ? ActiveLoadDisplayChild::LoadJob
                        : ActiveLoadDisplayChild::None;
}

/// An unbounded LoadLoopJob publish cannot enter an active-transport owner
/// turn. It remains pending for the STOPPED load/display policy.
inline bool admitLoadJobChild(bool timingCritical, bool commitPending) {
  return !timingCritical || !commitPending;
}

/// Fixed owner-local continuation for a timing-critical LoadAndDisplay turn.
/// A display frame retains its resolve/draw/transfer continuation. Load jobs
/// retain their progress in StorageManager and yield between bounded slices
/// when a display frame is due, including when queued loads cannot run yet.
/// A denied owner turn does not touch this state.
struct ActiveLoadDisplayContinuation {
  ActiveLoadDisplayChild pendingChild = ActiveLoadDisplayChild::None;

  ActiveLoadDisplayChild select(bool timingCritical, bool displayPending,
                                bool loadJobPending) {
    if (!timingCritical) {
      return ActiveLoadDisplayChild::None;
    }
    if (displayPending) {
      pendingChild = ActiveLoadDisplayChild::Display;
      return pendingChild;
    }
    if (pendingChild == ActiveLoadDisplayChild::LoadJob && loadJobPending) {
      return pendingChild;
    }
    pendingChild = selectActiveLoadDisplayChild(displayPending, loadJobPending);
    return pendingChild;
  }

  void finish(ActiveLoadDisplayChild child, bool remainsPending) {
    if (child != pendingChild) {
      return;
    }
    if (!remainsPending) {
      pendingChild = ActiveLoadDisplayChild::None;
    }
  }

  bool hasPendingChild() const { return pendingChild != ActiveLoadDisplayChild::None; }
};
constexpr uint8_t kDeferredIdleOwnerCount = 7;

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

/// 6-owner cursor plus the after-cycle extra Snapshot slot used while a
/// publication backlog is present. Caller-owned; no content-size input.
struct DeferredIdleOwnerCursor {
  DeferredIdleOwner owner = DeferredIdleOwner::Snapshot;
  bool extraSnapshotPending = false;
};

/// Ordinary 6-owner identity, or the extra Snapshot after a completed cycle
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

/// S4b.7 (Rev 20.3/21.7): does the `SlotProvision` turn have work? A **pending switch whose inbound
/// occupancy has not been taken yet**. State-driven on purpose: the pending switch *is* the eligibility, so
/// no separate request state is needed, and the occupancy answer belongs to the caller that owns the
/// runtime. A switch that is already provisioned is the commit path's business, not this turn's.
inline bool slotProvisionNeeded(bool hasPendingSlotSwitch, bool inboundOccupancyPresent) {
  return hasPendingSlotSwitch && !inboundOccupancyPresent;
}


/// DEC-108 item 1: does the `PlaybackViewMaintenance` turn have work? An **owed window**: a build that is
/// begun and still unfinished. State-driven on purpose, the `slotProvisionNeeded` shape -- the unfinished
/// build *is* the obligation, so nothing else has to be tracked and a slot with no build owes nothing. The
/// runtime's presence is the caller's answer, because the runtime's owner is the caller that owns it.
inline bool playbackViewMaintenanceNeeded(bool runtimePresent, bool buildUnfinished) {
  return runtimePresent && buildUnfinished;
}

/// DEC-108 §13b / task 3.10: a **holder** is one item of playback-maintenance work inside the
/// `PlaybackViewMaintenance` opportunity -- one slot's owed window build. The owner rotation above decides *that*
/// the playback owner gets a turn; this decides *which holder* that turn serves, which is where the design's two
/// remaining properties live: holders rotate, and within the eligible set the **earliest outstanding playback
/// deadline** is served first. Nothing here is state the caller cannot answer: eligibility is the same
/// "unfinished build" answer, and the deadline is the phase at which the playhead next enters the interval the
/// build serves (the figure `Track::requestPlaybackWindow` already passes as `handoffPhase`).
inline constexpr uint8_t kNoPlaybackHolder = 0xFFu;

struct PlaybackHolder {
  /// Owes a grain now (an unfinished build: `playbackViewMaintenanceNeeded`'s shape).
  bool eligible = false;
  /// False while the holder is owed a grain it **cannot** serve this turn (admission denied, contract margin not
  /// met, source not current): the holder stays eligible, so it is not lost -- only skipped over.
  bool canProgress = true;
  /// The phase at which the playhead next enters this holder's interval: its outstanding playback deadline.
  uint32_t deadlinePhase = 0;
  /// False means "no outstanding deadline" -- ordered after every holder that has one.
  bool hasDeadline = false;
};

/// An earlier outstanding deadline wins. A holder that *has* a deadline beats one that does not; two holders with
/// none keep the rotation's order, so fairness survives where urgency cannot distinguish them.
inline bool playbackHolderDeadlineIsEarlier(const PlaybackHolder& candidate,
                                            const PlaybackHolder& incumbent) {
  if (candidate.hasDeadline != incumbent.hasDeadline) {
    return candidate.hasDeadline;
  }
  if (!candidate.hasDeadline) {
    return false;
  }
  return candidate.deadlinePhase < incumbent.deadlinePhase;
}

/// The rotation's position. `next` is where the scan begins, never "the last holder": a holder selected once is
/// resumed *after*, which is what makes the fairness bound below hold without any per-holder state.
struct PlaybackHolderCursor {
  uint8_t next = 0;
};

/// What the selection did, named -- so "nothing was owed", "a holder was served" and "every owed holder was
/// blocked" are distinguishable outcomes rather than a bare index.
enum class PlaybackHolderSelection : uint8_t {
  /// No holder owed anything: the opportunity serves nothing this turn.
  None,
  /// The named holder is owed a grain and can progress.
  Service,
  /// Every owed holder is blocked this turn. The rotation still advanced past them.
  AllBlocked,
};

/// Round-robin across holders, earliest outstanding deadline first within the eligible set, and the cursor
/// advances **even when the chosen holder cannot progress** (a blocked early holder can therefore never monopolise
/// service).
///
/// Earliest-deadline-first is applied **globally** over the eligible set, which is the tighter of the two readings
/// of the design's "within a holder": a global order cannot serve a later deadline first inside any one holder, so
/// the per-holder reading follows a fortiori, and the property is stated once rather than per holder.
///
/// The bound this buys, as what a caller may rely on: at most `count` consecutive selections examine every holder
/// once, so **every holder that can progress is served within `count` turns** -- the per-holder progress bound task
/// 3.10 asks to be asserted for *every* admitted holder.
inline PlaybackHolderSelection selectPlaybackHolder(PlaybackHolderCursor& cursor,
                                                    const PlaybackHolder* holders,
                                                    uint8_t count, uint8_t& holderOut) {
  holderOut = kNoPlaybackHolder;
  if (holders == nullptr || count == 0) {
    return PlaybackHolderSelection::None;
  }
  bool sawEligible = false;
  uint8_t best = kNoPlaybackHolder;
  for (uint8_t step = 0; step < count; ++step) {
    const uint8_t index = static_cast<uint8_t>((cursor.next + step) % count);
    const PlaybackHolder& holder = holders[index];
    if (!holder.eligible) {
      continue;
    }
    sawEligible = true;
    if (!holder.canProgress) {
      continue;
    }
    if (best == kNoPlaybackHolder || playbackHolderDeadlineIsEarlier(holder, holders[best])) {
      best = index;
    }
  }
  if (best != kNoPlaybackHolder) {
    cursor.next = static_cast<uint8_t>((best + 1u) % count);
    holderOut = best;
    return PlaybackHolderSelection::Service;
  }
  // Nothing can progress this turn. The rotation still moves on, so a holder that is blocked now never holds the
  // position while it is blocked -- and the caller's next turn begins somewhere else.
  cursor.next = static_cast<uint8_t>((cursor.next + 1u) % count);
  return sawEligible ? PlaybackHolderSelection::AllBlocked : PlaybackHolderSelection::None;
}
inline bool aroundIdleMaintenance(bool postOverdubPlayingDrain, bool overdubStopping = false) {
  return postOverdubPlayingDrain || overdubStopping;
}

inline bool afterDeferredDisplay(bool captureActive, bool postOverdubPlayingDrain) {
  return captureActive || postOverdubPlayingDrain;
}

inline bool aroundDeferredLoadFrame(bool postOverdubPlayingDrain, bool overdubStopping = false) {
  return postOverdubPlayingDrain || overdubStopping;
}

/// DEC-045 amendment: OVERDUBBING idle does not reconstruct `visualCache`.
/// Source-view refresh stays on its own overdub path. Slices resume on
/// PLAYING / STOPPED_RECORDING.
inline bool allowVisualCacheReconstructSlice(bool isOverdubbing) {
  return !isOverdubbing;
}

inline bool allowDeferredSave(bool postOverdubPlayingDrain, bool deferredSavePending,
                               bool overdubStopping = false) {
  // During the post-overdub drain window, do not stack pending save work into the same
  // PLAYING turn that is already draining idle maintenance debt.
  return !(postOverdubPlayingDrain || overdubStopping) || !deferredSavePending;
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
