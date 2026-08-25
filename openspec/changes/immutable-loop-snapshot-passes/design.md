## Context

At the selected base commit, `LoopPasses` stores chunk-backed capture passes and edit rows, and `LoopPasses::materializeToEventVector()` derives effective MIDI. `PersistedLoopSnapshot` exists, but `Loop::sharePassesSnapshot()` uses `deepClonePasses()`; it is not a structurally shared immutable effective-state snapshot. The maintained-LCR lifecycle added after this base is deliberately not carried into this feature branch.

The target is a complete immutable snapshot at each committed state boundary. Snapshots contain immutable note and non-note event objects through loop-scoped stable handles. Playback retains mutable cursor, execution order, ledger, and horizon ownership. Existing capture finalization, overlap/pairing, NOTE_EDIT geometry, and deferred persistence owners remain unchanged until separately approved migration phases.

## Goals / Non-Goals

**Goals:**

- Make snapshots self-contained and directly selectable.
- Preserve logical `NoteId` across immutable object versions.
- Construct and atomically publish one candidate at existing capture/edit commit boundaries.
- Use snapshot identities for undo, redo, clear-slot restore, and consumer freshness.
- Use the existing event-stream implementation as a parity oracle during migration.

**Non-Goals:**

- No firmware implementation in the architecture phase.
- No maintained LCR owner, new scheduler, or new interval-path owner.
- No change to horizon size, projection math, cursor, ledger, or MIDI deadline behavior.
- No raw pointer persistence.
- No decision here on persistent sequence structure, reclamation, exact storage layout, or deduplication.
- No removal of visual cache, capture preview, overdub source-view storage, or playback scratch state.

## Decisions

### `Loop` publishes and `LoopPasses` stores snapshots

`Loop` owns active selection, candidate publication, and immutable object allocation. `LoopPasses` owns snapshot records and reference roots without becoming a semantic manager. This preserves the existing content ownership boundary.

**Alternative:** make `LoopContentResolution` the snapshot owner. Rejected because it would retain the duplicate derived-state ownership this branch is intended to avoid.

### Stable handles reference immutable objects

Snapshot references are loop-scoped handles, not persisted addresses. Editing a note creates a new immutable version with the same logical `NoteId`; older snapshots retain their original version.

**Alternative:** raw pointers. Rejected because addresses are not persistent identity and make reclamation implicit.

### Construction applies existing semantic mutations

At an existing commit boundary, construction reads the active snapshot as const, receives the settled mutation from existing capture/overlap/pairing/NOTE_EDIT owners, applies it to a structurally shared collection, freezes the candidate, and publishes atomically.

**Alternative:** let snapshot storage replay or resolve edits. Rejected because it creates a second semantic owner.

### Undo and playback select snapshots directly

Undo entries retain before/after snapshot identities. Undo, redo, and clear-slot restore select complete snapshots without replaying intervening history. Playback derives execution order from a const active snapshot while retaining runtime cursor, ledger, and horizon ownership.

**Alternative:** disable rows and replay to reconstruct. Rejected because it preserves history-proportional work.

### Persistence is a separate physical design

The runtime model requires serialization of immutable objects and snapshot structure in a future format. Exact layout, reclamation, active-tip encoding, and old-format migration are separate decisions; migration runs off the MIDI path.

## Risks / Trade-offs

- **[Memory overhead]** Immutable versions retain history → measure object/reference pools and admission before firmware.
- **[Construction cost]** Mutations may be content-dependent → keep construction off-clock and define an explicit bound or overrun policy.
- **[Parity]** Snapshot output may diverge → compare against current materialization for overlap, pairing, wrap, ordering, and non-note events.
- **[Lifetime]** Retained snapshots may leak or alias mutable data → test reachability, loop scope, and older-snapshot stability.
- **[Compatibility]** Storage changes may reject existing files → retain old readers until migration and round-trip gates pass.

## Migration Plan

1. Complete formal architecture reassessment and record approval before firmware work.
2. Implement immutable objects, stable handles, shared reference storage, and native tests with current consumers unchanged.
3. Adapt capture and NOTE_EDIT commit boundaries while retaining compatibility pass records.
4. Migrate playback and other consumers independently, using snapshot identity and parity gates.
5. Migrate undo/load/persistence after physical format approval.
6. Remove replay/materialized compatibility paths only after all caller-specific gates pass.

Rollback keeps the prior representation active and discards any failed candidate. Existing storage readers remain until the new reader and migration are proven.

## Open Questions

- Which persistent reference structure and reclamation policy meet memory and lifetime requirements?
- What exact storage version/layout and compatibility migration are required?
- Which optional indexes are justified without becoming a second authority?
- What construction memory/time bound and overrun policy are required?
- Which phase and work identity authorize each consumer migration?
