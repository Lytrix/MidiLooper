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
| 8 | `NoteUtils::reconstructDisplayNotes` in slice | `rebuildVisualCacheIdleSlice` | none | **Unbounded** — residual **4b.1b** is Stage 2 of [#41](https://github.com/Lytrix/MidiLooper/issues/41) / [`display_visual_cache_snapshot_reuse_refinement.md`](../../Plans/display_visual_cache_snapshot_reuse_refinement.md); inventory note remains in [`playback_window_build_owner_boundary_refinement.md`](../../Plans/playback_window_build_owner_boundary_refinement.md) § *4b.1b*; Ring B/RC2 closed without bounding this row |
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
| 21 | `LoopSnapshotConstruction::step` | Record session-exit drain, NOTE_EDIT close, and Phase 3 wrap-journal idle construction | One admitted step applies at most one persistent-cursor bar and copies at most 8 membership pages of 16 refs. It does not rebuild a snapshot, enumerate a pass, or publish. An incomplete step leaves the candidate unpublished and resumable. | **Structurally bounded** |
| 22 | `Loop::queueOverdubWrapSnapshotJournal` / `Loop::queueSessionExitSnapshot` / `Loop::stepPendingSnapshotPublish` / `Loop::tryPublishSessionExitSnapshot` / `Loop::tryPublishNoteEditSnapshot` | Wrap and session-exit queue at their commit/stop owners; idle publishes from `stepPendingSnapshotPublish` | Queue only journal-capped fragments with exact parent identity. One idle turn admits two units (`kSnapshotIdleConstructionUnitsPerTurn` = 2). Each unit is one fragment start or one row-21 step. `storePublishedSnapshot` runs only after every queued wrap fragment, session-exit pass, and companion change is complete. Failure keeps the previous published root and does not publish a prefix. | **Structurally bounded** |

Eleven rows are unbounded. Bounding those owners is Owner-Boundary Gate work (roadmap L1–L7). It is not a reason to introduce a scheduling class or a new Manager.

### Snapshot candidate construction (`LoopSnapshotConstruction::step` and session-exit publish)

**Row 21 goal:** Advance one in-flight candidate by one bounded construction
unit and then return. The unit is one persistent-cursor bar and at most
eight membership pages of sixteen references. The step may copy or sort
that page and move the cursor. It must not walk the whole snapshot, rebuild
a pass, or call `storePublishedSnapshot`.

**Row 22 goal:** Separate queue from publish. Wrap and session-exit only
enqueue bounded work (journal fragments with exact parent identity, or a
session-exit pass list) at stop / wrap commit. Idle then admits two units
(`kSnapshotIdleConstructionUnitsPerTurn` = 2). Each unit is one
journal-capped fragment start or one row-21 step. Live-record
start walks `capture.store` from the persisted store index through
`tick < includeUntil` and emits at most one journal-capped changeset (32
changes). It does not copy or scan the historical store. Admission
cardinality is two units per Snapshot idle turn. Publication is atomic and only when wrap journals,
queued session-exit passes, construction, and companion changes are all
complete. A failed or incomplete candidate is never selected; the previous
published root stays authoritative.

Phase 2 publication is admitted at NOTE_EDIT close and idle
`stepPendingSnapshotPublish`. Record and overdub session-exit **queue** at
stop (`queueSessionExitSnapshot`) and construct from
`Track::processDeferredIdleMaintenance`. Phase 3 wrap publication queues
bounded journal fragments from `Track::commitOverdubWrapAtSessionStart` and
steps them during PLAYING/OVERDUBBING after the first root (`DEC-047`). An idle
turn that applies Snapshot construction does not also run a visual-cache slice.
`STOPPED` first-paint (`visualCacheNeedsFirstPaint`) does not suppress that
Snapshot turn when session-exit, in-flight, or wrap-journal work is pending.
First-record construction uses the overdub live-enqueue setup (`DEC-045` 4b.2):
RECORDING may start from `capture.store` after the playhead has left bar 1.
Wrap-ahead admission consumes only bounded journal fragments and never starts
pass reconstruction. Construction remains **not** admitted to
`handleMidiInput()`, clock dispatch, track-tick, or the
`CommitReason::OverdubWrap` call stack; the wrap path queues the fragment only.

Selected structural caps:

| Cap | Value |
|-----|-------|
| Membership page capacity | `kSnapshotMembershipPageCapacity` = 16 |
| Membership pages per bar | `kSnapshotMembershipMaxPages` = 8 |
| Journal changes | `kSnapshotWrapJournalMaxChanges` = 32 |
| Journal affected bars | `kSnapshotWrapJournalMaxAffectedBars` = 32 |
| Journal membership entries | `kSnapshotWrapJournalMaxMembershipEntries` = 64 |
| Journal bytes | `kSnapshotWrapJournalMaxBytes` = 4096 |

| Property | Row 21 goal (`step`) | Row 22 goal (queue / idle publish) |
|----------|---------------|----------------|
| **Unit cap** | One persistent-cursor bar and at most 8 membership pages of 16 references | Queue: journal-capped fragments with exact parent identity. Idle: two units per turn; each unit is one fragment start or one row-21 step |
| **Maximum indivisible step** | Select/update or copy/sort one membership page (≤ 16 refs) and advance the cursor. No full-span walk, pass reconstruction, or publish | Start at most one journal-capped fragment (32 changes). Live-record start consumes only unconsumed `capture.store` events from the persisted index. Session-exit length mismatch first clones one snapshot header when applied live-record ticks fit the sealed length. Non-fit discard remains a content-sized destructor |
| **Progress** | Persistent bar / membership cursor, `complete()`, `failed()` | Ordered fragments and pass-change cursor; previous published root stays selected until `freeze()` succeeds |
| **Completion** | All admitted changes applied and `failed()` is false | Idle stores a complete root only after wrap journals, session-exit passes, construction, and companions are done |
| **Overrun / failure** | `failed_`; `freeze()` returns null; retain the fragment and resume | Keep the previous published snapshot; do not publish a prefix; compatibility `LoopPass` records stay committed |

`T_RT_reserve` remains the 3.6 device gate for construction-attributable MIDI
gap and deadline lateness ([DEC-053](../../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)).
`T_RT_reserve_ceiling` is the 10,000 µs policy cap. Per-turn `reserve_us` is
the measured interval from snapshot-idle start to the next timing-critical
`handleMidiInput()` entry. Admission compares the classified two-unit cost
to `min(T_RT_reserve_ceiling, remaining_us − T_RT_safety_margin)` and yields
when the cost does not fit. `60% × T_clock` is a diagnostic proxy only. STOPPED
samples are excluded from this gate. Device
[`115043`](../../../captures/session_20260908_115043.log) yielded 6389 live
turns with `remaining=0` and published `pending=0` only after STOPPED. These
structural caps admit the 3.5 firmware path; they do not close that device
measurement. Do not freeze an observed unit as the ceiling.

Capture-serial `snapshot_idle` now
emits child durations for `startNextPendingConstruction`, nested
`startNextPassChangeFragment`, `LoopSnapshotConstruction::step`,
`finishPendingConstruction`, and `storePublishedSnapshot`, plus a `probe_us`
overhead sample. Session-exit length mismatch first clones one snapshot
header when applied live-record ticks fit the sealed length; non-fit
discard remains a content-sized destructor. Rows 21 and 22 stay
**structurally bounded** until that attribution shows whether the 79 ms
class sits outside the classified unit or the classification is wrong. Do
not reclassify from the parent `snapshot_idle` span alone.

A local timer is not the bound. Measured first-record idle `step(1)` vs
`MI`/`MO` note gaps: [`233308`](../../captures/session_20260825_233308.log)
tables in
[`loop_pass_snapshot_effective_state_refinement.md`](../../Plans/loop_pass_snapshot_effective_state_refinement.md)
and
[`runtime_scheduling_owner_boundary_admission_refinement.md`](../../Plans/runtime_scheduling_owner_boundary_admission_refinement.md).
First-record construction uses a constructed
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
