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
| 2 | `Track::ensurePlaybackMergedMidiEventsBuilt` | `TrackPlaybackWindowBuild.cpp` | none | **Unbounded** |
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
| 21 | `LoopSnapshotConstruction::step` | Record session-exit (`commitPendingCapturePass` after a record pass), overdub session-exit (`Loop::tryPublishSessionExitSnapshot` after companion seal), and NOTE_EDIT `closeNoteEditPass` only | one settled note or event change; dirty bars = that change's membership | **Empirically bounded** |
| 22 | `Loop::tryPublishSessionExitSnapshot` / `Loop::tryPublishNoteEditSnapshot` | Same owners as row 21; one candidate completed or failed in that invocation | `step(totalChangeCount)` then atomic `freeze`, or failure | **Empirically bounded** |

Eleven rows are unbounded. Bounding those owners is Owner-Boundary Gate work (roadmap L1–L7). It is not a reason to introduce a scheduling class or a new Manager.

### Snapshot candidate construction (rows 21–22)

Phase 2 publication is admitted only at the existing session-exit and NOTE_EDIT
close owners. It is **not** admitted to `handleMidiInput()`, clock dispatch,
track-tick, or `CommitReason::OverdubWrap`.

| Property | Row 21 `step` | Row 22 publish |
|----------|---------------|----------------|
| **Unit cap** | One settled `SnapshotNoteChange` or `SnapshotEventChange` | One complete candidate for the settled mutation |
| **Maximum indivisible step** | Apply that change, including copy-on-write of its dirty pages and membership lists. A long or wrap note can touch every intersecting bar. | Run remaining `step` units through completion, then one atomic `publishedSnapshot_` store |
| **Progress** | `nextChangeIndex_`, `complete()`, `failed()` | Previous published root remains selected until `freeze()` succeeds |
| **Completion** | All changes applied and `failed()` is false | `freeze()` returns a non-null complete root |
| **Overrun / failure** | `failed_`; `freeze()` returns null | Discard the candidate; keep the previous published snapshot; do not publish a prefix; compatibility `LoopPass` records stay as committed |

A local timer is not the bound. Phase 2 does not yield mid-publish and does
not introduce a scheduler. First-record construction uses a constructed empty
parent object, not a null parent. Overdub and NOTE_EDIT publication require an
already published parent; they do not invent content from
`materializeToEventVector()`.

---

## Rejected

| Proposal | Status |
|----------|--------|
| `RuntimeWorkBudget::admit(WorkClass)` | **Rejected.** Local class budgets do not bound nested or indivisible work. |
| Per-job memory-pool declaration as a RAM1 fix | **Rejected.** RAM1 pressure is compile-time FlexRAM bank allocation, not runtime pool consumption. |
| Interval reservation | **Not authorized** until the Owner-Boundary Gate exits (contract Stage A). |
