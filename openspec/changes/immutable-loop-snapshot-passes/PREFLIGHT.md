# Preflight — immutable-loop-snapshot-passes

**Mode:** Full — formal architecture triggers fired
**Date:** 2026-08-25
**Decision:** [`DEC-044`](../../../docs/DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction), as amended by the formal reassessment
**Reassessment:** [`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md)

## Problem

The target replaces historical pass replay as the normal effective-state
source with complete immutable `LoopSnapshot` objects. This changes the
authoritative data model, later publication and selection transitions, undo
and redo representation, and future storage format. Phase 0 records the
approved direction only. No snapshot firmware, runtime publication, physical
directory selection, or execution-bound claim is authorized by this preflight.

## Domain

Primary domains: timeline data model, capture/overdub, NOTE_EDIT, playback,
undo, persistence, and derived representations.

## Similar historical decisions

### Search locations

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] `docs/Plans/` — snapshot refinement, runtime architecture, persistence,
  playback-window, overdub overlap, and NOTE_EDIT handoffs
- [x] Active OpenSpec — this change and its three capability deltas
- [x] `openspec/specs/` — `timeline-passes`, `loop-content-history`,
  `timeline-epochs`, `note-edit-stable-note-id`, `note-edit-session-undo`,
  `undo-memory-trim`, `deferred-job-scheduler`, and storage-session specs

### Relevant findings

- **DEC-010:** ownership changes require reassessment, migration, compatibility,
  removal trigger, approval, and a decision record.
- **DEC-016:** authoritative content, derived representations, interval
  projection, and runtime requests remain separate layers.
- **DEC-024:** `TrackUndo` remains the current global undo owner; later Loop
  ownership changes require an explicit migration.
- **DEC-029 / DEC-030:** `NoteEditCurrentState` and
  `NoteGeometryResolver` own NOTE_EDIT geometry and participation.
- **DEC-032 / DEC-033:** overlap is loop-scoped same-pitch geometry and does
  not filter candidates by per-note channel.
- **DEC-035:** persistence is content-only; runtime undo history is not a
  persisted payload.
- **DEC-037:** `LoopContentResolution` is a prototype/query path, not a second
  effective-state owner.
- **DEC-038:** overdub wrap publication and session undo use existing wrap and
  session boundaries.
- **DEC-043:** bound class is a gated architectural property; local budgets do
  not bound indivisible work.
- **DEC-044:** immutable `LoopSnapshot` replaces maintained LCR as the target
  effective-state direction; logical pitch/bar indexes are approved and the
  physical directory remains open for measurement.

## Existing owner

| Later phase | Existing owner | Scope to extend |
|---|---|---|
| Phase 1 — native snapshot model | `Loop` / `LoopPasses` | Immutable objects, logical indexes, roots, and invariant checks |
| Phase 2 — capture and edit publication | `Loop` at `commitCapturePass`; `Track` stop and overdub-wrap entry points; `EditManager` close/commit | Existing commit boundaries and semantic mutation handoff |
| Phase 3 — direct consumers | `Track` / `LoopPlaybackRuntime`, `DisplayManager`, `EditManager`, analyze callers | Existing representations, revisions, bounded source selection, and fallback policy |
| Phase 4 — undo/load/persistence | `TrackUndo`, `Loop`, `StorageManager` | Snapshot selection, local handle rebuild, deferred serialization and migration |
| Phase 5 — compatibility retirement | Existing LCR/materialization callers and their owners | Individual removal only after caller migration and rollback gates |

## Existing extension point

Reuse the public and internal methods already identified by the change:

- `Loop` snapshot publication and selection methods, extended at the existing
  `Loop` / `LoopPasses` boundary.
- `Loop::commitCapturePass()` and
  `Track::commitOverdubWrapAtSessionStart()` for capture publication.
- Existing `EditManager` NOTE_EDIT close and settled-batch paths.
- `TrackUndo` before/after selection and clear-slot restore paths.
- `StorageManager` deferred save/load and `StorageLoopIo` compatibility paths.
- Existing derived-representation invalidation and source-revision paths.
- `LoopContentResolution` only as temporary implementation machinery or a
  stateless compatibility adapter; never as effective-state ownership.

No new Manager, repository, scheduler, or parallel semantic owner is proposed.

## Reuse possible

**YES**

The approved design extends existing owners. The ownership evolution concerns
the authoritative representation, not live capture, overlap resolution,
NOTE_EDIT geometry, playback runtime state, undo routing, persistence
orchestration, or consumer representation ownership.

## Architecture review required

**YES — complete.** The formal reassessment is recorded in
[`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md), and the approved
Phase 0 direction is recorded in `DEC-044`. Later phase work remains gated.

## Loaded docs

- [x] `docs/Runtime/PROJECT_STATE.md`
- [x] `docs/Runtime/CURRENT_WORK.md`
- [x] `docs/DECISION_LOG.md`
- [x] `docs/Authority/PROJECT_INTENT.md`
- [x] `docs/Authority/ARCHITECTURE_RULES.md`
- [x] `docs/Authority/DELIVERY_RULES.md`
- [x] `openspec/changes/immutable-loop-snapshot-passes/tasks.md`
- [x] `docs/Authority/Architecture/RuntimeArchitecture.md`
- [x] `docs/Authority/Architecture/DerivedViews.md`
- [x] `docs/Authority/Architecture/Storage.md`
- [x] `docs/Authority/Architecture/ExecutionBounds.md`
- [x] `docs/Authority/NAMING.md`
- [x] `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- [x] `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`
- [x] `docs/Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md`
- [x] `docs/AGENT_CONTEXT_MAP.md`

## Extension point (implementation)

Later implementation must extend `Loop` / `LoopPasses` for snapshot content and
use the existing semantic owners to supply settled mutations. It must retain
the four-layer runtime boundary and use the existing deferred persistence and
consumer build policies.

## Ownership change

| Field | Answer |
|---|---|
| Current owner | Historical effective content is derived from `LoopPasses`; `Loop` owns pass content; `Track` / `EditManager` own live semantic mutation paths |
| Target owner | `Loop` publishes/selects complete `LoopSnapshot`; `LoopPasses` stores roots and compatibility passes |
| Transfer needed | YES — approved for the design/specification phase by DEC-044 and its formal reassessment |
| Compat removal trigger | Remove pass replay, maintained LCR effective-state ownership, materialization, and old readers only after tasks 6.1–6.3 caller gates and rollback triggers pass |

No transfer is proposed for capture, overlap, pairing, NOTE_EDIT geometry,
playback cursor/order/ledger/horizon, `TrackUndo` routing, `StorageManager`
orchestration, or derived-representation storage.

## Later phase gates

| Phase | Required before implementation |
|---|---|
| 1 — native snapshot model | Choose and measure the physical directory; classify allocation and lifetime; add invariant checks without changing normal consumers |
| 2 — publication adapters | Classify candidate construction in `ExecutionBounds.md`; define progress, completion, overrun/failure behavior, and atomic publication at existing boundaries |
| 3 — direct consumers | Give each consumer a snapshot source revision, bounded source range, stale/fallback policy, and parity gate |
| 4 — undo/load/persistence | Approve storage version/layout and local handle rebuild; preserve latest-saved-only reboot semantics and deferred `StorageManager` ownership |
| 5 — compatibility retirement | Prove each caller migration and rollback gate before removing its compatibility source |

### Phase 1 measurement checkpoint

The native locality check in
`test_loop_event_store::test_measure_paged_directory_successor_locality`
passed for a 1024-bar directory with one dirty bar. The flat baseline copies
1024 reference entries; the fixed-capacity paged candidate copies 32 directory
entries and the dirty membership entries while retaining the remaining pages
by reference. This selects a sparse fixed-capacity paged directory as the
Phase 1 physical implementation experiment. Page capacity remains a measured
parameter, and this native result does not establish Teensy memory safety.
Successor peak temporary memory, allocator behavior, and reclamation remain
required measurements before the implementation is accepted.

### Phase 1 implementation result

Phase 1 implements `LoopSnapshot` in `LoopSnapshot.h` /
`LoopSnapshot.cpp`, exposed through `Loop.h` without adding a runtime snapshot
field to `Loop` or `LoopPasses`. `LoopSnapshot` owns complete self-contained
span and event roots. Notes use immutable loop-scoped references keyed by
`NoteId`; events use immutable loop-scoped references keyed by `EventId` and
retain canonical event order. The selected physical experiment uses 32-bar
membership pages, copy-on-write dirty pages, and
`ExternalMemoryFirstAllocator` for snapshot allocations.

`LoopSnapshotConstruction` copies the active root references, applies settled
note/event changes with persistent change-index progress, freezes a complete
candidate, and leaves the parent unchanged on failure.

### Phase 2 publication result

`ExecutionBounds.md` classifies dirty-bar `LoopSnapshotConstruction::step`
and one-candidate publication as empirically bounded. Overrun/failure discards the candidate
and keeps the previous published snapshot. Construction is not admitted to
`handleMidiInput()`, clock dispatch, track-tick, or `OverdubWrap`.

`Loop` publishes through an opaque `PublishedSnapshotBox` (out-of-line
lifetime). Record session-exit publishes in `commitPendingCapturePass` after
the record pass is committed. Overdub session-exit publishes in
`finalizeCommitSideEffects` after companion seal. NOTE_EDIT
`closeNoteEditPass` publishes the settled session batch. In-session durable
checkpoints stay provisional. First record uses a constructed empty parent;
overdub/NOTE_EDIT require an already published parent and do not invent
content from `materializeToEventVector()`. Compatibility pass records remain.

Phase 3 consumer selection is not authorized by this result.

### Phase 3 playback result (4.1–4.2)

`ensurePlaybackMergedMidiEventsBuilt` reads published snapshot event
membership for the already selected playback window (2 bars on long loops;
full loop on short). A stale published root still supplies that window;
unpublished session / pending-publish passes and their companions overlay
through windowed CER. Snapshot identity is recorded on
`PlaybackMergedMidiEvents` and participates in freshness (current root
only). When no snapshot exists (undo/load/`resetPassTimeline`, first-record
before publish), the owner keeps compatibility gather. NOTE_EDIT preview
still replaces from `sessionMidiEvents()`. Cursor, order, ledger, and
horizon stay on `Track` / `LoopPlaybackRuntime`. `playbackEventPhase`
remains the hot-path scalar. Row 2 stays unbounded; this slice does not
convert it.

Tasks 4.3–4.6 are not authorized by this result.

### Phase 3 display result (4.4 display only)

`rebuildDisplayNotesInWindow`, committed full-loop display gather,
`rebuildVisualCacheIdleSlice`, and `rebuildVisualCacheFromPasses` read
published snapshot event membership through
`tryGatherPublishedSnapshotEventsInWindow` (and
`tryGatherPublishedSnapshotEventsInWindowWithCapture` when live capture is
in the paint path). Reconstruction stays on `reconstructDisplayNotes`.
`visualCache` remains the display list owner. Compatibility gather and LCR
prepared windows remain only when no published root exists. Task 4.3 is
skipped. NOTE_EDIT paint still uses `projectedNoteEditDisplayNotes`.
Analysis, validation, 4.5, and 4.6 identity recording are not authorized.
Live overdub paint holds the PLAYING committed handoff only while the
paint window stays inside the last gather. Follow exit or wrap
invalidate snapshot-gathers once. Per-frame snapshot gather during
overdub is not allowed
([`001048`](../../../captures/session_20260826_001048.log),
[`001907`](../../../captures/session_20260826_001907.log)).
`expandWindowWithMargin` includes a remainder of at most two margins so a
20-bar loop’s last 2 bars are in the first gather
([`002634`](../../../captures/session_20260826_002634.log)). Overdub-stop
paint holds the composed frame; `ODUB,stop,display` must not snapshot-gather
([`003459`](../../../captures/session_20260826_003459.log) display 97–154 ms).
Parked (not selected): `tasks.md` 4b.2 first-record `snapshot_idle` bound;
4b.3 post-stop `usbdisp` / `load_frame` / `late_on`; 4b.4 OVERDUBBING
`DisplayResolveLiveCapture`. Selected: 4b.1 playback-save grace on LoopPersist.

## Amendment (2026-08-25) — overdub publish is idle-stepped

Overdub and record session-exit no longer complete a candidate on the stop
path. `Loop::queueSessionExitSnapshot` copies pass ids; idle
`stepPendingSnapshotPublish(1)` applies one `LoopSnapshotConstruction::step`.
During OVERDUBBING,
idle only advances an in-flight construction (`allowStartConstruction` false).
One idle unit is one dirty bar, ordered by soonest playhead arrival. PLAYING
may start the next pass change-set. NOTE_EDIT close remains a synchronous
drain. Failure still discards the candidate. See `DEC-045`.

### Amendment (2026-08-25) — stale snapshot window plus unpublished overlay

`tryGatherPublishedSnapshotEventsInWindow` reads a published root even when
it is behind `playbackRevision`. Unpublished overdub session / pending-publish
passes and their companions overlay through windowed CER. Compatibility
full-list gather remains only when no published root exists.

### Amendment (2026-08-25) — OVERDUBBING idle skips visual reconstruct

`Track::processDeferredIdleMaintenance` does not run visual-cache slices
while OVERDUBBING. Only an in-flight `LoopSnapshotConstruction` may `step`.
[`230556`](../../../captures/session_20260825_230556.log) `idle_reconstruct`
was 241–278 ms when snapshot step had no in-flight work. PLAYING /
STOPPED_RECORDING give an empty dirty visual cache that turn before snapshot
`step` ([`231632`](../../../captures/session_20260825_231632.log)). After a
published root exists, PLAYING and OVERDUBBING do not `step` successor
construction; PLAYING visual stays off unless the cache is empty
([`232329`](../../../captures/session_20260825_232329.log)).

Native checks cover self-containment, unchanged membership sharing, immutable
object replacement, `NoteId` preservation, wrap membership, canonical event
ordering, candidate alias safety, resumable progress, and failed-candidate
preservation. The capture-serial build passed with 4640 RAM1 locals; the full
native suite passed 1453/1453. These gates do not establish a runtime
construction bound or persistence compatibility.

## Files affected

Expected later implementation scope is phase-specific and must not be combined
without a new phase review:

- Phase 1: `Loop` / `LoopPasses` snapshot types and native invariant checks.
- Phase 2: existing capture-stop, overdub-wrap, and NOTE_EDIT commit owners,
  plus `ExecutionBounds.md` for the admitted construction owner.
- Phase 3: existing playback, display, analyze, and NOTE_EDIT consumers.
- Phase 4: `TrackUndo`, `StorageManager`, `StorageLoopIo`, and migration tests.
- Phase 5: compatibility callers and their documented removal triggers.

## New abstractions

| Name | Kind | Justification |
|---|---|---|
| `LoopSnapshot` | Existing approved domain concept | Complete immutable effective-state object |
| Span index / event index | Existing approved logical roles | Pitch/bar note membership and bar event membership |
| Physical directory | Implementation choice, not a named domain owner | Must be selected by Phase 1 measurement |
| _none_ | — | No new Manager, scheduler, repository, or parallel authority |

## Persistence impact

No storage format changes occur in Phase 0. A later storage phase may add a
format for immutable objects, logical membership, metadata, and the latest
saved complete snapshot. It must not persist runtime addresses, derived
representations, playback runtime state, undo history, or abandoned redo
lineage sufficient for post-reboot undo.

## Undo impact

No runtime undo behavior changes in Phase 0. Later migration selects complete
before/after snapshots through `TrackUndo`, preserves strict LIFO and clear-slot
handling, clears the redo branch after new work, and starts rebooted sessions
with undo depth zero and redo empty. In-session `E:` and `O:` provisional
behavior remains separate from global undo.

## Migration required

**YES — staged firmware and storage migration later.** Phase 0 itself is
documentation-only. Later phases require native invariant checks, the
capture-serial firmware build with the RAM1 floor, existing capture/edit/HITL
gates where behavior is touched, and persistence round-trip/migration checks.

## Architecture review required (summary)

**YES — complete for Phase 0.** See
[`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md) and
[`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md). No later phase is
implicitly authorized by this preflight.

## Authority conflict check

| Source | Conflicts with architecture or intent? |
|---|---|
| OpenSpec / tasks | NO after DEC-044 reassessment alignment; physical directory and runtime bounds remain explicitly open |
| Proposed new class/helper | NO; no new class/helper is authorized by Phase 0 |

## Context summary

- Active change: `immutable-loop-snapshot-passes`, Phase 0 only.
- Approved logical model: pitch → 768-tick bar → `NoteRef` membership lists;
  bar → `EventRef` membership lists in existing event-stream order.
- `LoopSnapshot` is self-contained; parent-snapshot traversal is forbidden.
- `Loop` publishes/selects snapshots; `LoopPasses` stores roots and compatibility
  `LoopPass` records.
- Existing semantic owners determine overlap, pairing, NOTE_EDIT geometry, and
  runtime playback state.
- `StorageManager` remains the sole persistence/migration owner.
- Physical directory, optional indexes, storage layout, and construction bounds
  remain later decisions.
- No firmware or native test run is required for this documentation stage.
