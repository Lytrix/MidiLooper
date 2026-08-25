# Runtime architecture — execution bounds

**Authority:** bound classification for owners that execute on the MIDI interval path. Subordinate to [ARCHITECTURE_RULES.md](../ARCHITECTURE_RULES.md). **Decision:** [DEC-043](../../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property).

**Contract:** invariants **N** (bound classification) and **O** (owner boundary) in [runtime_scheduling_admission_model_architecture.md](../../Plans/runtime_scheduling_admission_model_architecture.md). This file is the **maintained inventory**. Measurements and leverage order stay in the scheduling contract and [runtime_scheduling_owner_boundary_admission_refinement.md](../../Plans/runtime_scheduling_owner_boundary_admission_refinement.md).

This document does **not** authorize interval reservation, extra `handleMidiInput()` call sites, a new scheduler, or `RuntimeWorkBudget::admit(WorkClass)` (rejected).

---

## Interval path

The **interval path** is work that executes between consecutive `handleMidiInput()` entries (the MIDI Input Gap). Clock dispatch inside `handleMidiInput()` and `ClockManager::updateInternalClock` → `TrackManager::updateAllTracks` also consume that interval.

`main.cpp::loop()` owns service order. Domain owners own the work they step.

---

## Bound classes

Wording is the contract §3.3 classification. Do not invent a fourth class.

| Class | Meaning | May be reserved as a timing guarantee? |
|-------|---------|----------------------------------------|
| **Structurally bounded** | Code enforces a fixed maximum amount of work and storage. | Yes. |
| **Empirically bounded** | Measurements provide an engineering limit, but code does not enforce it. | Only with an explicit overrun guard and failure policy. |
| **Unbounded** (or unknown) | Scans, drains, allocates, sorts, or loops on a content-dependent condition without an enforced cap. | No. |

Structural bounds are guarantees. Measurements alone are tuning evidence.

A local µs budget checked *between* steps does not make a step structurally bounded. An indivisible step larger than the budget remains empirically bounded or unbounded.

---

## Large-work handling

“Off-clock”, “deferred”, and cold/flash placement are not execution-bound classes.
They describe when or where code is placed, not how much work one indivisible
operation can perform.

An operation is still timing-critical when it runs from `handleMidiInput()`, a
button or transport action dispatched by MIDI input, or clock dispatch through
`TrackManager::updateAllTracks`. An existing commit boundary does not make
content-sized candidate construction safe. The caller's bound does not transfer
to a nested operation.

Before a large operation is introduced or extended on a runtime path, its owner
must define:

- the input and storage cap for one work unit;
- the maximum indivisible step within that unit;
- persistent progress and completion state;
- the structural bound, or the measured bound plus an explicit overrun guard
  and failure policy; and
- the behavior when the unit does not fit or overruns, without losing pending
  work or publishing partial state.

If a complete result is required, construction may be incremental, but the
active result remains selected until construction completes. Publication is a
single atomic operation over the complete result. A local timer checked only
between steps is not a substitute for the indivisible-step bound.

For any new runtime owner or extended owner, add or update its inventory row in
the same change that introduces the runtime work. Until that row has an
admissible class and policy, the work is not admitted to the interval path.

---

## Rules

1. An owner that executes on the interval path **must** have a declared row in the inventory below.
2. Adding or extending an **unbounded** owner on the interval path is a design session — same weight as an ownership or state-transition change ([ARCHITECTURE_RULES.md](../ARCHITECTURE_RULES.md#execution-bound-rules) and architecture-checkpoint question 3).
3. Reclassifying a row (unbounded → empirically or structurally bounded) updates this table in the same commit as the firmware that enforces the new class.
4. Interval reservation, if introduced later, coordinates existing owners and never becomes a domain owner (invariant O).

---

## Owner inventory

Seeded from Stage R1C (2026-08-21). Update **Class** and **Bound** here; keep measured maxima in the research doc.

| # | Owner | Where | Bound | Class |
|---|-------|-------|-------|-------|
| 1 | `TrackManager::updateAllTracks` from clock dispatch | `ClockManager::onMidiClockPulse` | none | **Unbounded** |
| 2 | `Track::ensurePlaybackMergedMidiEventsBuilt` | `TrackPlaybackWindowBuild.cpp` | selected playback window (2 bars on long loops; full loop on short) | **Unbounded** |
| 3 | `DisplayManager::update` — resolve phase | `resolveDisplayNotes` | 5000 µs measurement only | **Unbounded** |
| 4 | `DisplayManager::update` — draw + transfer | `_display.api.display()` | fixed frame size | **Structurally bounded** (size), **blocking** |
| 5 | `Loop::advanceFallbackGatherSession` traversal | `LoopVisualCache.cpp` | 96 / 192 / 768 events | **Structurally bounded** |
| 6 | Fallback gather sort | `sortSessionEventsByTick` | none | **Unbounded** |
| 7 | Fallback gather edit projection | `applyNoteEditPassSequence` | none | **Unbounded** |
| 8 | `NoteUtils::reconstructDisplayNotes` in slice | `rebuildVisualCacheIdleSlice` | none | **Unbounded** |
| 9 | `Loop::appendOverdubPassWrapPairedNotes` | `LoopVisualCache.cpp` | none | **Unbounded** |
| 10 | `removeDisplayNotesOverlappingBars` | `rebuildVisualCacheIdleSlice` | none | **Unbounded** |
| 11 | `Loop::materializeEditViewFromPasses` | STOPPED idle | none | **Unbounded** |
| 12 | `Loop::prewarmOverdubSourceSpanCache` | STOPPED idle | gated, not bounded | **Unbounded** |
| 13 | `validateAndCleanupMidiEvents` | deferred, 60 s policy | none | **Unbounded** |
| 14 | `StorageManager::processDeferredSaveState` | `DeferredSaveScheduler` | 300 µs / 3000 µs between steps | **Empirically bounded** |
| 15 | `DeferredJobScheduler::runFrame` | `LoadLoopJob` | 250 / 1200 / 5000 µs between steps | **Empirically bounded** |
| 16 | `enqueueRemainingLoopSlotRestores` | boot fill | 4 entries per turn | **Structurally bounded** |
| 17 | `reclaimUnreferencedDisabledPasses` | `loop()` at Critical pressure | none | **Unbounded**, not resumable |
| 18 | `processHitlSerialCommands` | capture builds only | none | **Unbounded** |
| 19 | `processDeferredIdleMaintenance` × 8 | `main.cpp` | per-track only | **Unbounded** in aggregate |
| 20 | `MidiButtonProcessor::processPendingPresses` | inside `handleMidiInput` | none | **Unbounded** |
| 21 | `LoopSnapshotConstruction::step` | Record session-exit drain, NOTE_EDIT close, and idle `Loop::stepPendingSnapshotPublish(1)` | one dirty bar of the in-flight note or event change, soonest playhead arrival first | **Empirically bounded** |
| 22 | `Loop::queueSessionExitSnapshot` / `Loop::stepPendingSnapshotPublish` / `Loop::tryPublishSessionExitSnapshot` / `Loop::tryPublishNoteEditSnapshot` | Record and overdub stop queue only; idle steps one row-21 unit (no published root: OVERDUBBING in-flight only; after a root exists: STOPPED / STOPPED_RECORDING only); NOTE_EDIT/`tryPublish` drain to completion | Queue is O(1) pass-id copy. Idle applies one `step`. Sync drain still `step` then atomic `freeze` | **Empirically bounded** |

Eleven rows are unbounded. Bounding those owners is Owner-Boundary Gate work (roadmap L1–L7). It is not a reason to introduce a scheduling class or a new Manager.

### Snapshot candidate construction (rows 21–22)

Phase 2 publication is admitted at NOTE_EDIT close and idle
`stepPendingSnapshotPublish`. Record and overdub session-exit **queue** at
stop (`queueSessionExitSnapshot`) and construct from
`Track::processDeferredIdleMaintenance`. An idle turn that applies one
snapshot unit does not also run a visual-cache slice. While OVERDUBBING, idle
only applies row 21 to an already started construction, and only when no
published root exists yet (first-record publish). After a published root
exists, PLAYING and OVERDUBBING do not apply row 21. PLAYING visual slices
also stay off unless the visual cache is empty. PLAYING / STOPPED_RECORDING
give an empty dirty visual cache the idle turn before row 21 when no root
exists yet. Starting the next pass change-set stays on PLAYING /
STOPPED_RECORDING / STOPPED only while no root exists, then on STOPPED /
STOPPED_RECORDING. Wrap-ahead enqueue and `startNextPendingConstruction`
stay off the OVERDUBBING idle turn. It is **not** admitted to `handleMidiInput()`, clock dispatch,
track-tick, or `CommitReason::OverdubWrap` (wrap only records a pass id).

| Property | Row 21 `step` | Row 22 publish |
|----------|---------------|----------------|
| **Unit cap** | One dirty bar of the current `SnapshotNoteChange` or `SnapshotEventChange` | Queue: copy pass ids. Idle: one row-21 unit. Sync drain: remaining units then one store |
| **Maximum indivisible step** | Copy-on-write that bar's dirty page and membership list. Remaining bars of the same change wait for later idle turns, ordered by soonest playhead arrival. Starting a pass still reconstructs that pass's committed events into a change set on the idle/drain owner | Atomic `storePublishedSnapshot` only after the candidate is complete |
| **Progress** | `nextChangeIndex_`, `complete()`, `failed()` | Pending box queued/applied pass ids; previous published root remains selected until session-exit `freeze()` succeeds |
| **Completion** | All changes applied and `failed()` is false | `freeze()` returns a non-null complete root and idle/drain stores it |
| **Overrun / failure** | `failed_`; `freeze()` returns null | Discard the candidate; keep the previous published snapshot; do not publish a prefix; compatibility `LoopPass` records stay as committed |

A local timer is not the bound. First-record construction uses a constructed
empty parent object, not a null parent. Overdub and NOTE_EDIT publication
require an already published parent; they do not invent content from
`materializeToEventVector()`. Playback uses the stale-snapshot fallback until
idle publish stores a current root.

### Playback snapshot read (row 2)

Phase 3 playback preparation uses the existing row 2 owner.
`Loop::tryGatherPublishedSnapshotEventsInWindow` walks only the bars that
intersect the already selected playback window and copies those bar
membership events. A published root behind `playbackRevision` still supplies
that window; unpublished overdub session / pending-publish passes and their
companions overlay through windowed `CommittedEventRange` on those lists
only. Compatibility gather of every committed chunk list runs only when no
published snapshot exists. Snapshot identity is recorded on
`PlaybackMergedMidiEvents` for freshness (current root only). This does not
add a new interval owner and does not convert row 2 to a bounded class:
short-loop full gather and the no-snapshot fallback remain content-sized.
`playbackEventPhase` stays the clock-facing scalar.

---

## Rejected

| Proposal | Status |
|----------|--------|
| `RuntimeWorkBudget::admit(WorkClass)` | **Rejected.** Local class budgets do not bound nested or indivisible work. |
| Per-job memory-pool declaration as a RAM1 fix | **Rejected.** RAM1 pressure is compile-time FlexRAM bank allocation, not runtime pool consumption. |
| Interval reservation | **Not authorized** until the Owner-Boundary Gate exits (contract Stage A). |
