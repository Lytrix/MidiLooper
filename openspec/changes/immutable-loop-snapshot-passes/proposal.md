## Why

The current loop model derives effective musical state by replaying capture passes and edit rows. It also carries a maintained `LoopContentResolution` representation, duplicating state and making playback, undo, and consumer freshness depend on reconstruction. This change defines complete immutable `LoopSnapshot` objects so the active loop state can be selected directly without continuing the maintained-LCR architecture.

## What Changes

- Introduce complete immutable `LoopSnapshot` objects. Each snapshot indexes notes as pitch → bar → membership lists of `NoteRef`s and events as bar → membership lists of `EventRef`s. Event-list order is an indexed view of existing event-stream semantics. A snapshot's logical content is completely determined by its own reference root.
- Keep `LoopPass` (`RecordPass`, `OverdubPass`, `EditPass`) as the historical/compatibility operation record. A commit may produce a compatibility pass record and a `LoopSnapshot`; those are not the same object.
- Keep `Loop` / `LoopPasses` as the snapshot publication and storage boundary: `Loop` publishes and selects; `LoopPasses` stores snapshot records and compatibility pass rows.
- Keep `StorageManager` as the sole persistence and migration owner; large-loop save/load work uses its existing deferred units and does not synchronously rebuild or serialize the complete loop on timing-critical paths.
- Keep capture finalization, overlap, note pairing, and NOTE_EDIT geometry semantics in their existing owners.
- Make undo, redo, and clear-slot restore select existing snapshot identities rather than replay history.
- Migrate overdub source resolution, playback, display, NOTE_EDIT, analysis,
  validation, persistence, undo, redo, clear-slot restore, and load in
  separately gated stages. Every normal consumer reads the selected immutable
  snapshot; derived caches retain ownership but do not become effective-state
  authorities.
- Keep derived representation ownership, revision, invalidation, and build policy with the existing owners defined by [`DerivedViews.md`](../../docs/Authority/Architecture/DerivedViews.md).
- Keep persistence and compatibility migration with `StorageManager` as defined by [`Storage.md`](../../docs/Authority/Architecture/Storage.md), using deferred bounded units for large loops.
- Preserve the four-layer boundary in [`RuntimeArchitecture.md`](../../docs/Authority/Architecture/RuntimeArchitecture.md): authoritative snapshot content, derived representations, interval projection, and runtime requests remain separate owners and contracts.
- **BREAKING**: historical replay and maintained LCR state become
  compatibility-only verification/rollback sources and are retired after all
  caller migration gates pass. This change does not optimize compatibility
  paths as an intermediate effective-state solution.
- **BREAKING**: a future storage version is required for immutable objects and snapshot structure; its physical layout is a separate design.

## Specification hierarchy

The architectural idea is small. Later sections keep integration and migration constraints, but they are not the core model.

### Core architecture

1. Snapshot object model (`LoopSnapshot` vs `LoopPass`).
2. Pitch/bar logical indexes with shared unchanged membership.
3. Candidate mutation from a const parent snapshot.
4. Atomic publication of a complete candidate.
5. Direct snapshot selection.
6. Reachability, lifetime, and reclamation.

### Integration contracts

7. Capture publication (provisional prefixes vs one global record transition).
8. NOTE_EDIT batch publication.
9. Undo/redo branch selection.
10. Playback and derived-representation consumers.
11. Persistence and reboot (latest saved snapshot only).

### Migration gates

12. LCR retirement.
13. Compatibility materialization removal.
14. Derived-view source migration.

## Capabilities

### New Capabilities

- `immutable-loop-snapshots`: Complete immutable loop snapshots, stable loop-scoped references, candidate publication, lifetime, and direct consumer selection.

### Modified Capabilities

- `timeline-passes`: Complete immutable `LoopSnapshot` objects become the normal effective-state representation; `LoopPass` records remain compatibility data during migration.
- `loop-content-history`: Runtime undo/redo selects snapshots, and future persistence serializes immutable objects and the latest complete snapshot without persisting undo history or promising post-reboot undo.

## Impact

- Affects `Loop` / `LoopPasses`, playback preparation, `TrackUndo`, `StorageManager`, NOTE_EDIT and display consumers, and `LoopContentResolution`.
- Implementation plan: [`overdub_wrap_source_snapshot_migration_refinement.md`](../../../docs/Plans/overdub_wrap_source_snapshot_migration_refinement.md).
- Phase 0 remains architecture and specification only. The logical indexes are selected and the formal reassessment is approved. The activated implementation slice is Phase 3 tasks `3.5`/`3.6`: bounded overdub-wrap mutation-journal publication, runtime pair `NoteId` / non-note `EventId` handling, fixed-capacity membership construction, parity, and device gates. Firmware remains gated by the phase review and `ExecutionBounds.md` admission. Direct consumer migration, persistence, and snapshot-identity undo remain later phases.
- Verification for later phases is limited to existing gates plus invariant-protecting checks: old snapshot unchanged, new snapshot complete, unchanged objects shared, changed objects replaced, selection identity, and declared construction bounds. This change does not add a new test architecture.
- Open decisions: measured page capacity and reclamation implementation, storage layout/version and migration, optional disposable indexes, bounded consumer span access, long-span projection candidate limits, and later-phase overrun behavior. The Phase 3 construction policy is fixed by bounded mutation journals, fixed-capacity membership pages, persistent bar cursors, exact parent identity, and retry retention.
- No change is authorized here to horizon size, projection math, cursor or ledger semantics, capture finalization, overlap ownership, NOTE_EDIT geometry ownership, or interval reservation.
- Any runtime candidate construction must first be classified against
  [`ExecutionBounds.md`](../../docs/Authority/Architecture/ExecutionBounds.md):
  a fixed-cap resumable unit, or an empirically bounded unit with an explicit
  overrun guard and failure policy. “Off-clock” or deferred placement alone is
  not an execution bound.
- `LoopSnapshot` stores canonical immutable content, not projection-specific
  intervals or consumer windows. Large-loop consumers must select bounded span
  ranges before calling `IntervalProjection`; a complete snapshot does not
  authorize full-loop projection on a timing-critical path.
- `DerivedViews.md` remains the representation ownership contract: snapshot
  identity becomes the source revision, existing representation owners retain
  their invalidation and build policies, and compatibility `LoopPasses` reads
  remain until each consumer migration gate passes.
