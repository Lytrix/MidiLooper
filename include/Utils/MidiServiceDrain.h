//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <array>
#include "Utils/PlaybackPhysicalDeadline.h"
#include "Utils/WindowCoverage.h"

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

// Caller-owned checkpoint opportunity state, not a queue of domain jobs.
struct InputCheckpointCursor {
  DeferredIdleOwner owedNormal = DeferredIdleOwner::Snapshot;
  bool normalPending = false;
};
using CheckpointCostTable = std::array<PhysicalCheckpointCost, kDeferredIdleOwnerCount>;
struct InputCheckpointDecision {
  bool dispatchOwner = false;
  DeferredIdleOwner owner = DeferredIdleOwner::Snapshot; // Ignored unless dispatchOwner.
  bool insertedWindow = false;
  PhysicalCheckpointResult admission = PhysicalCheckpointResult::ReferenceUnavailable;
};

// Preview selection and the actual chosen owner's cost, then commit both cursors
// only if its single grain can finish before independent physical/logical bounds.
// This does not run a grain, grant readiness, or establish the supplied bounds.
inline InputCheckpointDecision selectInputCheckpointOwner(
    DeferredIdleOwnerCursor& normal, InputCheckpointCursor& checkpoint,
    bool urgentWindowCanProgress, bool snapshotBacklogPending, bool sessionExitDrainPending,
    const PlaybackPhysicalDeadline& deadline, PlaybackDeadlineClock clock,
    PhysicalClockStamp now, const CheckpointCostTable& costs) {
  auto proposedNormal = normal;
  auto proposedCheckpoint = checkpoint;
  InputCheckpointDecision decision;
  bool proposedInsertion = false;
  if (proposedCheckpoint.normalPending) {
    decision.owner = proposedCheckpoint.owedNormal;
    proposedCheckpoint.normalPending = false;
  } else {
    const auto selected = selectDeferredIdleOwner(proposedNormal, snapshotBacklogPending, sessionExitDrainPending);
    if (urgentWindowCanProgress && selected != DeferredIdleOwner::PlaybackViewMaintenance) {
      proposedCheckpoint.owedNormal = selected;
      proposedCheckpoint.normalPending = true;
      decision.owner = DeferredIdleOwner::PlaybackViewMaintenance;
      proposedInsertion = true;
    } else decision.owner = selected;
  }
  const auto index = static_cast<uint8_t>(decision.owner);
  if (index >= costs.size()) {
    decision.admission = PhysicalCheckpointResult::CostUnavailable;
    return decision;
  }
  decision.admission = deadline.admit(clock, now, costs[index]);
  if (decision.admission != PhysicalCheckpointResult::Allowed) return decision;
  normal = proposedNormal;
  checkpoint = proposedCheckpoint;
  decision.dispatchOwner = true;
  decision.insertedWindow = proposedInsertion;
  return decision;
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
  /// How long the holder has before its interval is needed, **in ticks along its own loop**
  /// (`RuntimePlaybackViewBuild.h`'s `handoffLeadTicks`) -- not a raw phase. A phase is meaningless across
  /// holders whose loops differ in length (phase 900 is late on a 1,000-tick loop and early on a 20,000-tick one),
  /// while the tick distance is the same quantity the admission already judges, so urgency is comparable.
  uint32_t handoffLeadTicks = 0;
  /// False means "no outstanding deadline" -- ordered after every holder that has one.
  bool hasDeadline = false;
  /// DEC-246 amendment 27 (owner ruling): the holder's owed work is a **rolling successor with an active handoff
  /// deadline** -- liveness-critical, so it may be denied at most `kDeadlineSuccessorMaxConsecutiveDenials`
  /// consecutive eligible opportunities (`selectPlaybackHolderBounded`). Every other holder keeps round-robin.
  bool deadlineSuccessor = false;
  /// C5.3b (owner ruling 2026-11-03): the deadline successor's **next grant is due** -- its remaining lead (to the
  /// handoff, less the guard) divided by its bounded remaining grants, counted from its last grant, has elapsed. A due
  /// successor is served at this opportunity; no competing holder or owner may withhold it. Slack that early grants
  /// earned lets a later gap be longer, and a successor falling behind tightens its own next due.
  bool grantDue = false;
};
/// The part of a successor's lead kept back from the grant budget for the handoff itself (one measured lane-turn gap
/// is 32-56 ticks on the device).
constexpr uint32_t kDeadlineSuccessorHandoffGuardTicks = 64;

/// DEC-246 amendment 27 (owner ruling, measured on device at 300 BPM with the balanced tail: 3 holds 50/50 seams, 4-6
/// miss): the most consecutive eligible opportunities a deadline successor may be denied. The successor's admission
/// price may count on this bound because the scheduler enforces it.
constexpr uint8_t kDeadlineSuccessorMaxConsecutiveDenials = 3;

/// An earlier outstanding deadline wins, where "earlier" means **fewer ticks to go**, not a smaller phase. A
/// holder that has a deadline beats one that does not; two holders with none keep the rotation's order, so
/// fairness survives where urgency cannot distinguish them.
inline bool playbackHolderDeadlineIsEarlier(const PlaybackHolder& candidate,
                                            const PlaybackHolder& incumbent) {
  if (candidate.hasDeadline != incumbent.hasDeadline) {
    return candidate.hasDeadline;
  }
  if (!candidate.hasDeadline) {
    return false;
  }
  return candidate.handoffLeadTicks < incumbent.handoffLeadTicks;
}

/// The rotation's position. `next` is where the scan begins, never "the last holder": a holder selected once is
/// resumed *after*, which is what makes the fairness bound below hold without any per-holder state.
struct PlaybackHolderCursor {
  uint8_t next = 0;
  /// Consecutive eligible opportunities each holder's deadline successor was denied (by another holder's turn or a
  /// withheld grant); reset when it is served or stops being a deadline successor.
  uint8_t successorDenials[8] = {};
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
/// **The within-a-holder rule** -- earliest outstanding deadline first, and the cursor advances **even when the chosen
/// holder cannot progress**, so a blocked early holder can never monopolise service.
///
/// **Scope, corrected (2026-10-02).** This ordering is the *inside-one-holder* rule, and it must not be applied across
/// holders. The spec is explicit: "Service SHALL rotate round-robin across eligible holders, and **within a holder**
/// SHALL serve the earliest outstanding playback deadline first", with the scenario *"Earlier deadlines win within a
/// holder -- WHEN **a holder has more than one outstanding playback deadline** THEN its earliest outstanding deadline is
/// served first"*. The earlier version here applied earliest-deadline-first **globally** and called that "the tighter of
/// the two readings"; that reading is wrong and it starves: a holder whose deadline is permanently earlier than
/// another's is chosen **every** rotation, and the cursor cannot rescue the loser because the comparison ignores where
/// the scan starts. Across holders the rule is `selectPlaybackHolderRoundRobin` below.
///
/// The bound, as what a caller may rely on: at most `count` consecutive selections examine every holder once, so every
/// holder that can progress is served within `count` turns.
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

/// **The across-holders rule** -- plain round-robin, and the one the spec's fairness sentence names for holders: "Service
/// SHALL rotate round-robin across eligible holders". The cursor decides *who*; the holder's own urgency is not an
/// ordering key here, because it must not be: a holder whose deadline is permanently earlier than another's would then
/// be chosen on every rotation and starve the other, which is the defect this rule removes. Urgency is still used, one
/// level down -- within a holder (`selectPlaybackHolder` above) and by the per-holder offer that picks which of a
/// holder's own obligations is the urgent one.
///
/// A holder that cannot progress is skipped **without consuming the rotation's position for anyone else**: the scan
/// keeps walking to the next eligible holder, so a blocked holder costs at most its own turn. If no eligible holder can
/// progress, the cursor still advances and the outcome is named `AllBlocked` -- never a latch, and never a silent
/// "nothing owed".
///
/// The bound this buys, and the one the envelope is derived from: **every eligible holder that can progress is served
/// once per `count` turns** (one rotation), so the worst gap between two services of the same holder is `count` turns.
inline PlaybackHolderSelection selectPlaybackHolderRoundRobin(PlaybackHolderCursor& cursor,
                                                             const PlaybackHolder* holders, uint8_t count,
                                                             uint8_t& holderOut) {
  holderOut = kNoPlaybackHolder;
  if (holders == nullptr || count == 0) {
    return PlaybackHolderSelection::None;
  }
  bool sawEligible = false;
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
    cursor.next = static_cast<uint8_t>((index + 1u) % count);
    holderOut = index;
    return PlaybackHolderSelection::Service;
  }
  cursor.next = static_cast<uint8_t>((cursor.next + 1u) % count);
  return sawEligible ? PlaybackHolderSelection::AllBlocked : PlaybackHolderSelection::None;
}
/// **The bounded selection** (DEC-246 amendment 27): `selectPlaybackHolderRoundRobin`, except that an eligible deadline
/// successor that can progress and has already been denied `kDeadlineSuccessorMaxConsecutiveDenials` opportunities is
/// served first. It is not unconditional priority: below the bound the rotation decides. Every eligible deadline
/// successor passed over is counted as denied; a holder that is not one has its count reset. Flash-resident.
PlaybackHolderSelection selectPlaybackHolderBounded(PlaybackHolderCursor& cursor, const PlaybackHolder* holders,
                                                    uint8_t count, uint8_t& holderOut);
/// True when the chosen holder's grant may not be withheld (its successor has used its whole denial allowance).
bool playbackHolderMustBeServed(const PlaybackHolderCursor& cursor, uint8_t index, const PlaybackHolder& holder);
/// The chosen holder was served: its deadline successor's count resets.
void notePlaybackHolderServed(PlaybackHolderCursor& cursor, uint8_t index);
/// The chosen holder's grant was withheld by a competing owner: one more denial for its deadline successor.
void notePlaybackHolderWithheld(PlaybackHolderCursor& cursor, uint8_t index, const PlaybackHolder& holder);

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

// ---------------------------------------------------------------------------------------------------
// Realtime idle-owner admission (`realtime-idle-owner-admission-bound`, DEC-043).
//
// While a track is timing-critical, the deferred idle-owner turn may dispatch only work whose per-turn
// cost is **declared and enforced**. This is the owner-boundary predicate for that rule; it is not a
// scheduler, a work class, or interval reservation (DEC-043 point 3 keeps `admit(WorkClass)` rejected).
//
// Two quantities that must not be conflated:
//
//   * `kIdleMaintBreachThresholdUs` (50 ms) is the **alarm** — the same number as
//     `kRuntimeTimingTelemetry::kLoopRemainderOneShotUs`, which is what makes every `loop_rem,idle_maint`
//     row a declared-bar breach. It is an acceptance criterion and a backstop, never the work budget.
//   * `kOwnerWorkBudgetUs` is the amount of **child work** a turn may admit. It is strictly smaller than
//     the alarm because the turn also carries its own scans (track walk, pending-snapshot scan, visual
//     wrap-append scan), the `handleMidiInput()` polls around it, telemetry and the breach record. Giving
//     the children the whole 50 ms guarantees that some turn lands at 50 ms + overhead and breaches.
//
// `kOwnerWorkBudgetUs` is derived from the **fastest supported MIDI clock interval**, not from the alarm
// and not from a measured 120 BPM sample. A child that runs longer than one clock interval guarantees a
// delayed clock edge, which is the tempo wobble this change exists to remove.
//
// The enforced playback tempo range is 1-300 BPM (`ClockManager::setBpmFloat`,
// `MidiConfig::TEMPO_MAX_BPM`), and MIDI clock is 24 PPQN, so the **minimum supported interval** is
// `60e6 / (300 * 24) = 8333 us`. The declared contract is therefore:
//
//   kOwnerWorkBudgetUs + worstCaseOwnerResidualUs <= kMinimumSupportedClockIntervalUs
//
// A 25 ms draft failed this (it exceeded even the 120 BPM interval); an 8 ms draft still leaves only
// ~333 us of residual at 300 BPM, which no measurement would defend. 4 ms leaves 4333 us, where
// `kOwnerWorkResidualUs` starts until the device measures it (task 5.4) **at the fastest supported
// tempo**, not at 120 BPM. A measured 120 BPM interval is not the safety proof.
//
// Do not size coverage tiles or worker grains against this number. A tile is a correctness/publication
// unit; a grain is an execution unit bounded by its own event/operation cursor.
//
// Admission **reserves a child's declared maximum before dispatch**: a child whose maximum does not fit
// the remaining budget is deferred by name rather than dispatched into a budget it can only overshoot.
// A child may take a smaller remaining deadline only when it declares a finer resumable grain.
constexpr uint32_t kIdleMaintBreachThresholdUs = 50000u;
/// Enforced maximum tempo 300 BPM at 24 PPQN -> the shortest interval a clock edge can arrive on.
constexpr uint32_t kMinimumSupportedClockIntervalUs = 8333u;
/// Provisional worst-case cost of a turn that is not admitted child work (owner scans, MIDI polls,
/// telemetry, the breach record). Measured on device at the fastest supported tempo in task 5.4.
constexpr uint32_t kOwnerWorkResidualUs = 4000u;
constexpr uint32_t kOwnerWorkBudgetUs = 4000u;

/// One dispatchable unit of work on the deferred idle-owner turn. A *child* is a named unit, not a
/// function: the registry below is closed over these names, and the enumeration test asserts every
/// reachable call site maps to exactly one of them (an unregistered child is a red suite, not a silent
/// overrun).
enum class IdleOwnerChild : uint8_t {
  OverdubSourceView = 0,   // windowed derive of the interval the view serves
  VisualCacheSlice,        // 1..N dirty bars
  PlaybackWindowGrain,     // one declared window-build grain
  CanonicalPrefixGrain,    // one normalisation child (Q = 32)
  SnapshotUnit,            // one fragment start or one construction step
  DeferredStopGrain,       // one stop-finalisation grain
  WrapAppend,              // one deferred visual wrap append
  SlotProvision,           // one pending slot's inbound occupancy
  OverdubSourceSpanPrewarm,// whole-loop span cache: unbounded, prewarm only
  WholeLoopDisplayResolve, // whole-pass `resolveDisplayNotes(passes, ...)` fallback
  StoredMidiVerification,  // capture-only effective-notes dump
  SevtDump,                // capture-only stopped-event dump
  DisplayBaseGrain,        // DEC-237: one bounded staged committed-display-base grain
  RevisionEditIndexGrain,  // DEC-246 C2b: one bounded `RevisionStore::stepEditIndex` grain (shadow store)
  RevisionShadowImportGrain,  // DEC-246 C3b: one bounded `RevisionStore::ImportJob` step (shadow store)
  RevisionShadowWindowGrain,  // DEC-246 C3d: one bounded shadow window-producer step
  Count
};
constexpr uint8_t kIdleOwnerChildCount = static_cast<uint8_t>(IdleOwnerChild::Count);

/// Declared bound for one child. `maxUs` is the device claim (the microsecond ceiling a turn may spend);
/// `maxUnits` is the deterministic work-unit claim the native fixtures assert, so a fixture never
/// depends on host timing. `fineGrainUs == 0` means "not resumable finer" -- such a child may never be
/// dispatched with a deadline smaller than `maxUs`.
struct IdleOwnerChildBound {
  const char* name;
  uint16_t maxUs;
  uint16_t fineGrainUs;
  bool unbounded;
};

/// The registry. `unbounded` children are never dispatched while timing-critical; they return a named
/// deferral. Every entry here must be reachable from exactly one call site, and every call site must
/// appear here -- `test_realtime_idle_owner_admission` pins both directions.
constexpr IdleOwnerChildBound kIdleOwnerChildBounds[kIdleOwnerChildCount] = {
    // Bounded children declare the quantum the code already enforces; this slice declares them, it does
    // not invent a bound for them.
    {"overdub_source_view", 4000, 1000, false},   // windowed derive, sub-window resumable
    {"visual_cache_slice", 3000, 0, false},       // 1..4 bars
    {"playback_window", 2000, 0, false},          // row 27 grain (measured 0.175-0.684 ms)
    {"canonical_prefix", 3000, 0, false},         // Q = 32
    {"snapshot_unit", 3000, 0, false},            // one fragment/step
    {"deferred_stop_grain", 4000, 0, false},      // DEC-178 grain (measured max 3.77 ms)
    {"wrap_append", 4000, 0, false},              // one append
    {"slot_provision", 4000, 0, false},           // one inbound occupancy
    // Unbounded children: whole-loop work with no declared, enforced slice.
    {"overdub_span_prewarm", 0, 0, true},
    {"whole_loop_resolve", 0, 0, true},
    // Task 7.1/7.2: the verification dump is one bar / one event slice per turn. The declared max is
    // the owner budget; the fine grain is the resumable one-bar derive. The device grain measurement
    // is still owed (section 7), so the max is a declared claim, not a measured one.
    {"stored_midi_verification", 4000, 1000, false},
    {"sevt_dump", 4000, 1000, false},
    // DEC-237 Slice 3: one bounded staged display-base grain (one tile or one boundary pass).
    {"display_base_grain", 4000, 1000, false},
    // DEC-246 C2b: a declared start of 2 ms whole call / 0.5 ms fine grain (256 hide rows measured at 1.12 ms on
    // device in B.5d); rebased only from device evidence.
    {"revision_edit_index", 2000, 500, false},
    // DEC-246 C3b: one bounded import step (64 rows declared start); rebased only from device evidence.
    {"revision_shadow_import", 2000, 500, false},
    // DEC-246 C3d: one bounded window-producer step (128 rows declared start).
    {"revision_shadow_window", 2000, 500, false},
};

enum class IdleOwnerChildDecision : uint8_t {
  /// Owed and admitted for this turn.
  Run,
  /// Owed, not admitted. `reason` names the child and why.
  Defer,
  /// Nothing owed; the turn costs nothing for this child.
  NotOwed,
};

struct IdleOwnerAdmission {
  IdleOwnerChildDecision decision = IdleOwnerChildDecision::NotOwed;
  /// Microseconds this child may spend. `0` unless `decision == Run`.
  uint32_t grantedUs = 0;
  /// Stable name of the registry entry, for the deferral row.
  const char* name = "";
};

inline const IdleOwnerChildBound& idleOwnerChildBound(IdleOwnerChild child) {
  const uint8_t index = static_cast<uint8_t>(child);
  return kIdleOwnerChildBounds[index < kIdleOwnerChildCount ? index : 0];
}

/// The one admission decision. Reserve-before-dispatch: a child is admitted only when its declared
/// maximum fits the remaining owner work budget, or when it can resume at a finer grain that does.
/// While no track is timing-critical the budget does not apply -- the stopped lane keeps today's
/// admission exactly (`grantedUs` is unbounded there).
inline IdleOwnerAdmission admitIdleOwnerChild(IdleOwnerChild child, bool owed, bool timingCritical,
                                              uint32_t remainingOwnerWorkBudgetUs) {
  IdleOwnerAdmission out{};
  out.name = idleOwnerChildBound(child).name;
  if (!owed) {
    out.decision = IdleOwnerChildDecision::NotOwed;
    return out;
  }
  if (!timingCritical) {
    out.decision = IdleOwnerChildDecision::Run;
    out.grantedUs = 0xFFFFFFFFu;
    return out;
  }
  const IdleOwnerChildBound& bound = idleOwnerChildBound(child);
  if (bound.unbounded) {
    // No declared, enforceable per-turn bound: never dispatched on a timing-critical turn. The child
    // keeps its own cursor/request and is owed again on a non-timing-critical turn.
    out.decision = IdleOwnerChildDecision::Defer;
    return out;
  }
  if (static_cast<uint32_t>(bound.maxUs) <= remainingOwnerWorkBudgetUs) {
    out.decision = IdleOwnerChildDecision::Run;
    out.grantedUs = bound.maxUs;
    return out;
  }
  // Does not fit whole. A finer resumable grain may take the remaining deadline; anything else is
  // deferred rather than dispatched into a budget it can only overshoot.
  if (bound.fineGrainUs != 0 && static_cast<uint32_t>(bound.fineGrainUs) <= remainingOwnerWorkBudgetUs) {
    out.decision = IdleOwnerChildDecision::Run;
    out.grantedUs = remainingOwnerWorkBudgetUs;
    return out;
  }
  out.decision = IdleOwnerChildDecision::Defer;
  return out;
}

/// Production gate for an **unbounded** child (tasks 2.3/2.5). An owed unbounded child never runs on a
/// timing-critical turn; it keeps its request/cursor and is served later. This is the same rule as
/// `admitIdleOwnerChild`'s unbounded branch, expressed without ODR-using the registry array: a new
/// production caller's copy of that array is `.data` and crossed the DTCM align edge (+4 KB RAM1
/// variables). The full reserve-before-dispatch contract remains `admitIdleOwnerChild` for the bounded
/// children; the registry/enumeration test still pins the unbounded set.
inline bool unboundedIdleOwnerChildMayRun(bool owed, bool timingCritical) {
  return !owed || !timingCritical;
}

/// Task 7.1/7.2: the bounded production gate for the stored-MIDI verification/SEVT dump. Header-only
/// for the same reason as `unboundedIdleOwnerChildMayRun`: reaching the registry array from the hot
/// Track TU ODR-used it into `.data` and snapped DTCM by 4 KB. The constants mirror the registry and
/// are pinned by `test_realtime_idle_owner_admission`.
constexpr uint32_t kStoredVerificationMaxUs = 4000;
constexpr uint32_t kStoredVerificationFineGrainUs = 1000;
/// DEC-246 C2b gate constants for `RevisionEditIndexGrain`, mirrored from the registry for the same ODR reason.
constexpr uint32_t kRevisionEditIndexMaxUs = 2000;
constexpr uint32_t kRevisionEditIndexFineGrainUs = 500;
constexpr uint32_t kRevisionShadowImportMaxUs = 2000;
constexpr uint32_t kRevisionShadowImportFineGrainUs = 500;
constexpr uint32_t kRevisionShadowWindowMaxUs = 2000;
constexpr uint32_t kRevisionShadowWindowFineGrainUs = 500;
inline bool boundedIdleOwnerChildMayRun(bool owed, bool timingCritical, uint32_t declaredMaxUs,
                                        uint32_t remainingBudgetUs, uint32_t fineGrainUs = 0) {
  if (!owed) {
    return false;
  }
  if (!timingCritical) {
    return true;
  }
  if (declaredMaxUs != 0 && declaredMaxUs <= remainingBudgetUs) {
    return true;
  }
  return fineGrainUs != 0 && fineGrainUs <= remainingBudgetUs;
}

/// DEC-237 3c1 — the coverage-vs-display owner-turn choice. Coverage wins only when an owed completion's
/// consumer due tick is within one owner opportunity (`kCoverageLeadTicks`); merely being owed is not
/// urgency, or continuous coverage would starve the staged display exactly as C6.4 describes. The due
/// tick comes from `DeferredOverdubCompletion.endTick`, not from a scheduler-invented age.
constexpr uint32_t kCoverageLeadTicks = 8;
enum class CoverageDisplayChoice : uint8_t { Coverage, Display, Nothing };
inline CoverageDisplayChoice chooseCoverageOrDisplay(bool coverageRequiredNow, bool displayOwed) {
  if (coverageRequiredNow) {
    return CoverageDisplayChoice::Coverage;
  }
  if (displayOwed) {
    return CoverageDisplayChoice::Display;
  }
  return CoverageDisplayChoice::Nothing;
}

/// **Aggregate turn arithmetic** (ExecutionBounds row 19). `kOwnerWorkBudgetUs` bounds one *child*; it never
/// bounded the sum of the tracks one turn visits, because the realtime walk dispatches
/// `Track::processDeferredIdleMaintenance` for every eligible track in the same turn. Measured on device
/// (`session_20261030_114009.log`): one turn spent 283,622 us and the next 285,832 us -- 71x the declared
/// child budget and 5.7x the 50 ms alarm -- while the playback note lane went 294-326 ms late. The walk
/// therefore asks this before **starting another track unit**: once the turn has spent its budget it
/// yields to the MIDI/display lanes and the next turn resumes at the following track (the caller keeps the
/// rotating cursor). One unit always runs, so a single expensive unit cannot starve the rotation; it is
/// named instead (the caller reports any unit above this bound).
inline bool idleOwnerTurnMayStartAnotherUnit(uint32_t elapsedTurnUs) {
  return elapsedTurnUs < kOwnerWorkBudgetUs;
}

/// Budget arithmetic for one turn, in dispatch order. `remaining` starts at `kOwnerWorkBudgetUs` and is
/// reduced by each admitted child's *declared maximum* (not its actual cost), so a later child cannot be
/// admitted on the strength of an earlier child's early exit.
struct IdleOwnerTurnBudget {
  uint32_t remainingUs = kOwnerWorkBudgetUs;
  uint8_t admitted = 0;

  IdleOwnerAdmission admit(IdleOwnerChild child, bool owed, bool timingCritical) {
    const IdleOwnerAdmission admission = admitIdleOwnerChild(child, owed, timingCritical, remainingUs);
    if (admission.decision == IdleOwnerChildDecision::Run && timingCritical) {
      const IdleOwnerChildBound& bound = idleOwnerChildBound(child);
      // Charge what was granted. A whole-maximum admission charges the maximum; a finer-grain child that
      // took the remaining deadline charges that remainder, so the plan can never over-charge past the
      // budget it was judged against.
      const uint32_t charged = bound.maxUs != 0 && admission.grantedUs >= bound.maxUs
                                   ? bound.maxUs
                                   : admission.grantedUs;
      remainingUs = remainingUs > charged ? remainingUs - charged : 0;
      ++admitted;
    }
    return admission;
  }
};

/// **Coverage currency** (design D3). The `overdubSourceView` is load-bearing for overlap correctness, so
/// its refresh is the one child that may not simply be deferred: an established view is authoritative
/// only while its coverage still contains the interval the overlap path will evaluate. Outside that
/// coverage the bounded refresh must run (at a finer grain if necessary) before the interval is
/// consumed -- stale geometry is never served as though it were current.
///
/// `coveredStartTick`/`coveredLengthTicks` describe what the established view holds; the required pair
/// is the interval now owed. Wrap is handled implicitly: the offset is taken modulo the loop, so a
/// window that straddles the loop end is covered by a window that straddles it too.
inline bool idleOwnerSourceViewCovers(uint32_t coveredStartTick, uint32_t coveredLengthTicks,
                                      uint32_t requiredStartTick, uint32_t requiredLengthTicks,
                                      uint32_t loopLengthTicks) {
  return WindowCoverage::windowCoversWindow(coveredStartTick, coveredLengthTicks, requiredStartTick,
                                            requiredLengthTicks, loopLengthTicks);
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

/// Main's persistence admission boundary. Storage owns requests and worker state;
/// this function only offers the existing guarded owner turn (no new scheduler).
/// The template lets native SD/clock fixtures call the same production boundary.
template <typename Storage, typename State>
bool serviceDeferredPersistenceTurn(const State& state, DeferredIdleOwner owner,
                                    bool ownerAllowed, bool timingCriticalTrackActive,
                                    uint32_t loadFrameUs, bool postOverdubPlayingDrain,
                                    bool overdubStopping) {
  const bool persistencePending = Storage::hasDeferredSaveWork() ||
      Storage::hasRevisionCommitWork() || Storage::hasRevisionLoadWork();
  const bool allowPrioritizedWorkspaceSaveDrain =
      Storage::shouldPrioritizeWorkspaceSaveDrain() && !timingCriticalTrackActive;
  const bool allowAfterLoadFrame = allowPrioritizedWorkspaceSaveDrain ||
      allowDeferredSaveAfterLoadFrame(timingCriticalTrackActive, loadFrameUs);
  const bool queuedCaptureChunks = Storage::hasQueuedCaptureChunksToPersist();
  const bool admitted =
      (persistencePending &&
       (allowPrioritizedWorkspaceSaveDrain ||
        (owner == DeferredIdleOwner::Persistence && ownerAllowed && allowAfterLoadFrame &&
         allowDeferredSave(postOverdubPlayingDrain, persistencePending, overdubStopping)))) ||
      queuedCaptureChunks;
  if (!admitted) return false;
  Storage::processDeferredSaveState(state);
  return true;
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
/// root, an overdub session-exit may still start its bounded fragment. A Record
/// session-exit may continue against a stale published root so the committed
/// Record pass can become current without waiting for transport to stop.
inline bool allowSnapshotConstructionStart(bool isOverdubbing, bool liveRecordPastFirstBar,
                                           bool hasWrapJournal, bool havePublishedSnapshot,
                                           bool sessionExitQueued) {
  if ((!isOverdubbing && liveRecordPastFirstBar) || hasWrapJournal) {
    return true;
  }
  return sessionExitQueued && (!isOverdubbing || !havePublishedSnapshot);
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
