# Decision review — immutable-loop-snapshot-passes

**Date:** 2026-08-25
**Change:** Phase 0 approval and later-phase implementation gates
**Decision:** [`DEC-044`](../../../docs/DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction)

This is the historical Phase 0 decision review. Its statements that firmware,
runtime publication, and physical directory implementation were not authorized
describe the state on 2026-08-25. Subsequent gated slices implemented
publication, playback preparation, and stateless snapshot queries; the
remaining consumer, persistence, and compatibility-retirement slices still
require their own gates.

## Historical review

### Searched

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] Active OpenSpec — this change and its three capability deltas
- [x] `openspec/specs/` — `timeline-passes`, `loop-content-history`,
  `timeline-epochs`, `note-edit-stable-note-id`, `note-edit-session-undo`,
  `undo-memory-trim`, `deferred-job-scheduler`, and storage-session specs
- [x] `docs/Plans/` — snapshot refinement, runtime architecture, persistence,
  playback-window, overdub overlap, and NOTE_EDIT handoffs

**Search terms:** `LoopSnapshot`, `LoopPass`, `LoopPasses`, `materialize`,
`LoopContentResolution`, `NoteRef`, `EventRef`, `NoteId`, `commitCapturePass`,
`commitOverdubWrapAtSessionStart`, `TrackUndo`, `StorageManager`,
`ExecutionBounds`, `presentAt`, `ActiveNoteLedger`, `visualCache`,
`PersistedLoopSnapshot`, ownership, persistence, and structural sharing.

### Relevant findings

- **DEC-010:** ownership evolution requires reassessment, migration,
  compatibility, removal trigger, approval, and a decision record.
- **DEC-016:** authoritative content, derived representations, interval
  projection, and runtime requests are separate layers.
- **DEC-024:** global undo ownership remains with `TrackUndo` until a later
  explicit migration.
- **DEC-029 / DEC-030:** NOTE_EDIT geometry and participation remain with
  `NoteEditCurrentState` / `NoteGeometryResolver`.
- **DEC-032 / DEC-033:** overlap uses loop-scoped same-pitch geometry and does
  not filter candidates by per-note channel.
- **DEC-035:** persistence is content-only; undo history is not persisted.
- **DEC-037:** `LoopContentResolution` is a prototype/query path and not a
  second effective-state owner.
- **DEC-038:** overdub wrap publication and session undo use existing lifecycle
  boundaries.
- **DEC-043:** bound class and indivisible-step policy are required before
  timing-relevant work is admitted.
- **DEC-044:** complete immutable snapshots are the target direction; the
  logical pitch/bar indexes are approved and physical realization remains open.

### Existing reusable pattern

Extend `Loop` / `LoopPasses` for snapshot publication and root storage.
Continue using existing capture-stop and overdub-wrap boundaries, existing
NOTE_EDIT settled-batch boundaries, `TrackUndo` selection, `StorageManager`
deferred persistence, current derived-representation owners, and existing
native/HITL/persistence verification gates.

### Reuse decision

**YES**

No equivalent new owner is required. The proposed transfer is the approved
authoritative representation evolution in DEC-044, with compatibility retained
until caller-specific migration gates pass.

## Ownership and transition review

### Ownership

`Loop` / `LoopPasses` become the target owners for complete immutable snapshot
content and selection. Existing owners retain live capture, overlap/pairing,
NOTE_EDIT geometry, playback runtime state, global undo routing,
`StorageManager` persistence, and derived-representation storage.

No new Manager, scheduler, repository, or parallel effective-state owner is
approved. Temporary LCR machinery can supply candidate-construction input but
cannot own effective state, publication, or snapshot lifetime.

### State transitions

Later phases may add snapshot publication or selection at existing record-stop,
overdub-stop, NOTE_EDIT-close, undo/redo, clear-slot, and load boundaries.
Provisional session prefixes are not global undo entries. These transitions
remain phase-specific and require their own implementation review and
verification.

### Execution bounds

Phase 0 adds no interval-path work. Later candidate construction must use the
existing `ExecutionBounds.md` process. Semantic locality by affected pitch and
dirty bars is not an execution bound. A long note can touch many bars, and
physical-directory cost remains a measured implementation concern.

## Decision

Approve the Phase 0 architecture direction:

1. Use `LoopSnapshot` as complete immutable effective state and `LoopPass` as
   the historical/compatibility operation record.
2. Use pitch → canonical 768-tick bar → `NoteRef` membership lists for the
   span index and bar → `EventRef` membership lists as an indexed view of
   existing event-stream order.
3. Keep the snapshot self-contained; shared membership is not a parent-delta
   chain.
4. Keep physical directory representation, optional indexes, storage layout,
   and runtime construction bounds as later measured decisions.
5. Retain existing semantic and consumer owners and remove compatibility paths
   only after individual migration gates pass.

This decision is recorded in DEC-044 and does not authorize firmware,
runtime publication, or a physical directory implementation.

## Migration and validation

**Migration required:** YES, later and staged.

- **Snapshot model:** native invariant checks for completeness, immutable
  replacement, shared unchanged references, older-snapshot stability, and
  selection identity.
- **Capture and NOTE_EDIT publication:** existing capture/edit gates,
  parity against materialization, and capture-serial/HITL validation where
  behavior changes.
- **Consumers:** existing playback, display, NOTE_EDIT, analyze, and LED
  policies with snapshot identity and bounded source-range checks.
- **Undo and persistence:** existing undo/clear-slot and deferred
  `StorageManager` gates, plus save/load round trips and old-format migration.
- **Compatibility retirement:** no removal until the relevant caller gate and
  rollback trigger are closed.

No new test architecture is introduced. Phase 0 itself needs no firmware
build, native test run, or HITL run.
