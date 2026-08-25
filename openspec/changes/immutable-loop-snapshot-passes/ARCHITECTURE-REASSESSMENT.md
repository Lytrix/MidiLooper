# Formal architecture reassessment — immutable-loop-snapshot-passes

**Date:** 2026-08-25  
**Status:** Recommendation complete; formal approval pending  
**Scope:** OpenSpec task 1.1, Phase 0 only  
**Change:** [`immutable-loop-snapshot-passes`](.)  
**Decision context:** [`DEC-044`](../../../docs/DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction)

## Reason triggered

This change fires multiple formal reassessment triggers:

- **Ownership:** authoritative effective state is proposed to move from
  historical pass replay to complete immutable snapshots owned by `Loop` /
  `LoopPasses`.
- **Persistent state model:** the effective timeline changes from pass-derived
  materialization to a self-contained `LoopSnapshot` reference root.
- **Undo semantics:** undo and redo are proposed to select complete snapshot
  identities rather than replay intervening records.
- **Storage schema:** a future format must serialize immutable event objects,
  logical membership, and the latest saved complete snapshot.
- **State transitions:** capture, NOTE_EDIT, undo/redo, clear-slot restore, and
  load acquire snapshot publication or selection boundaries.
- **Execution bounds:** candidate construction must be classified before it is
  admitted to any timing-relevant path.

This is a design reassessment. It authorizes no firmware edit and does not
classify future candidate construction as admissible.

## Current architecture

The current implementation and authority documents establish these owners:

| Scope | Current owner and evidence | Constraint |
|---|---|---|
| Capture and committed pass mutation | `Loop` through `commitCapturePass`; `Track` owns record and overdub stop entry points | Stop entry points can be reached from MIDI input or clock dispatch |
| Historical content rows | `LoopPasses` stores `RecordPass`, `OverdubPass`, and `EditPass`; `LoopPasses::materializeToEventVector()` derives effective MIDI | Replay and materialization are compatibility paths during migration |
| Snapshot compatibility DTO | `Loop::sharePassesSnapshot()` creates `PersistedLoopSnapshot` by deep-cloning passes; `restorePassesSnapshot()` clones on restore | This is not the target structurally shared effective-state root |
| Overdub semantic resolution | Existing `Loop` / `Track` overlap and pending-note paths, including `accumulatePendingNoteChangesForIncomingNote()` and `sealPendingNoteChangesToEditPasses()` | Snapshot storage must not resolve overlap a second time |
| NOTE_EDIT geometry | `EditManager` and `NoteGeometryResolver` through the live `NoteEditSession` | Snapshot publication consumes settled mutations at existing boundaries |
| Playback runtime | `Track` / `LoopPlaybackRuntime` owns cursor, playback order, ledger, and horizon state | Snapshot content must not absorb runtime state |
| Global undo | `TrackUndo` and per-track `GlobalUndoStack` | Existing before/after snapshot fields are compatibility data until target snapshots exist |
| Persistence | `StorageManager` owns serialization, deferred save, load, and migration | Runtime addresses and derived representations are not persisted |
| Derived representations | Existing owners retain playback, display, and analyze representation storage and build policy | `LoopSnapshot` is the source, not the owner of consumer windows |
| Compatibility analysis | `LoopContentResolution` remains historical/compatibility machinery in the tree | It must not become a maintained effective-state authority for this change |

The current execution-bound inventory contains unbounded owners relevant to
future migration, including `TrackManager::updateAllTracks` from clock
dispatch, `Track::ensurePlaybackMergedMidiEventsBuilt`, fallback gather sort,
and several display/analyze paths. Phase 0 does not extend those owners or
admit candidate construction into them.

## Proposed evolution

The selected logical content model is:

```text
LoopSnapshot
    span index:  pitch → canonical 768-tick bar → membership lists of NoteRefs
    event index: bar → membership lists of EventRefs
```

The span index contains complete notes whose geometry intersects each bar.
The event index is an indexed view of existing event-stream order. Open-at-S
and ledger identities remain runtime consumer state. A snapshot's logical
content is completely determined by its own reference root; resolving content
does not consult a parent snapshot.

The recommended ownership evolution is:

- `Loop` owns active snapshot selection, candidate publication, and immutable
  object allocation.
- `LoopPasses` stores snapshot roots and retains compatibility `LoopPass` rows
  during migration.
- Existing capture, overlap, pairing, NOTE_EDIT geometry, playback, undo, and
  persistence owners retain their responsibilities.
- Temporary LCR machinery may provide candidate-construction input, but it
  does not own effective state, publication, or snapshot lifetime.
- The physical directory that implements shared pitch/bar membership remains
  open for Phase 1 measurement. Path-copy may be considered as a directory
  implementation, but not as the semantic overlap model.

This is an ownership transfer for the authoritative representation, not a
transfer of live capture, overlap, playback, or persistence ownership.

## Invariants required by the recommendation

1. A published `LoopSnapshot` is complete and self-contained.
2. Unchanged immutable event objects and unchanged pitch/bar membership remain
   shared with the parent snapshot.
3. Only dirty membership and the minimum physical directory path required to
   reach it are newly constructed.
4. Dirty bars are the incoming span plus every resolver-reported geometry or
   membership change.
5. Candidate selection is not object mutation; candidate sets deduplicate by
   `NoteId`.
6. A note may occur in multiple bar membership lists while remaining one
   immutable version and one logical `NoteId`.
7. Wrap notes use their two existing linear segments and share one `NoteId`
   across both indexed regions.
8. Event-list order is an indexed view of existing event-stream order, not a
   second canonical ordering authority.
9. No unmatched open Ons enter the span index.
10. No partial snapshot is published. The active snapshot remains selected
    until a complete successor is ready.

## State-transition impact

The later migration defines these transitions without changing the Phase 0
implementation scope:

- A record or overdub session publishes one global snapshot at its existing
  session-exit commit boundary. Intermediate record prefixes and overdub wraps
  are provisional session snapshots.
- A settled NOTE_EDIT batch may produce a provisional snapshot; the existing
  close boundary creates the global undo transition.
- Undo, redo, and clear-slot restore select complete snapshot identities.
- After undo followed by new work, the abandoned redo branch is not a redo
  target and is reclaimed when no named root reaches it.
- Reboot loads the latest saved complete snapshot, starts with undo depth zero
  and redo empty, and discards unsaved session tails.

These transitions require later phase approval and verification. They are not
implemented by this reassessment.

## Execution-bound impact

Phase 0 adds no runtime operation and no new interval-path owner. The future
candidate-construction contract is:

```text
identify affected content
    → apply immutable replacements
    → construct successor root
    → freeze
    → atomically publish
```

Affected pitch and dirty bars define semantic locality only. Execution cost
remains dependent on intersecting membership count and the physical directory.
A long note can touch every bar it intersects. Therefore:

- physical-directory selection requires measurement;
- each later runtime construction owner requires an `ExecutionBounds.md` row;
- the row must define input/storage cap, maximum indivisible step, progress,
  completion, and overrun/failure policy;
- a local timer between steps is insufficient;
- deferred, off-clock, or cold placement does not make content-sized work
  admissible;
- failure must preserve the active snapshot and settled mutation, with no
  partial publication.

No Phase 0 claim upgrades an existing unbounded inventory row.

## Storage and persistence impact

`StorageManager` remains the sole persistence and migration owner. A later
format decision must serialize immutable note and non-note objects, logical
`NoteRef` / `EventRef` membership, loop metadata, and the latest saved complete
snapshot. It must not serialize runtime addresses, derived representations,
playback cursor/order, active ledgers, undo history, or abandoned-branch
lineage sufficient to restore post-reboot undo.

The current `PersistedLoopSnapshot` deep-clone DTO and existing readers remain
compatibility paths until the physical format and migration gates are approved.
No storage schema is changed by this reassessment.

## Compatibility and migration impact

Compatibility is retained until each caller migration gate passes:

1. Construct immutable objects and the selected logical indexes without
   changing normal consumers.
2. Measure and select the physical directory.
3. Add bounded candidate construction and publication at existing boundaries.
4. Migrate playback, display, analyze, NOTE_EDIT, persistence, and load
   independently while retaining pass/materialization parity.
5. Migrate undo and clear-slot restore to snapshot selection.
6. Remove maintained replay, LCR effective-state ownership, materialization,
   and old readers only after their individual rollback triggers close.

Rollback keeps the existing pass/materialized representation active and
discards any incomplete or failed snapshot candidate. No compatibility path
is removed by Phase 0.

## Alternatives considered

### Keep historical replay or maintained LCR as the effective-state authority

**Rejected.** It preserves history-proportional reconstruction and the
duplicate effective-state ownership that this change is intended to remove.
Existing LCR machinery may remain temporary construction machinery, but it does
not own the published effective state.

### Select a generic persistent collection in Phase 0

**Rejected.** The domain requirement is the logical pitch/bar locality and
complete-note span membership. Selecting a flat vector, chunk arrangement, or
tree before physical measurement would conflate semantic overlap with directory
implementation and reopen the larger generic-structure design.

### Use a flat full-loop `NoteRef` vector as the overlap structure

**Rejected.** It does not preserve the no-full-copy successor invariant and
does not make same-pitch, intersecting-bar candidate discovery local.

### Use event On/Off membership or open-at-S state for overlap

**Rejected.** Event membership misses notes whose On occurs in an earlier bar,
and open-at-S / ledger state is transient runtime state rather than complete
semantic note content.

## Migration validation

Phase 0 requires documentation and authority review only; it does not require a
firmware build or native test run. Later phases use the existing native,
capture-serial, persistence, and HITL gates, plus narrowly scoped checks for
snapshot completeness, reference sharing, immutable replacement, older-snapshot
stability, `NoteId` deduplication, ordering, wrap indexing, and selection
identity. No new test architecture is introduced.

## Recommendation and approval

**Recommendation:** approve the proposed ownership evolution and the selected
logical pitch/bar indexes. Keep the physical directory representation,
candidate-construction bound, storage layout, and optional indexes as later
measured decisions under the existing owners and phase gates.

**Approval required:** YES, before any firmware phase. Approval does not
authorize runtime publication by itself; later phases must complete their
preflight, physical-directory measurement, and execution-bound classification.

**Decision state:** task 1.1 reassessment is complete as a recommendation.
Task 1.2 remains responsible for recording the approved decision and aligning
the durable authority documents after approval.
