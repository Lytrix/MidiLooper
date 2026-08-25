# Loop Pass Snapshot Effective State Refinement

**Status:** Proposed — architecture/specification only
**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Base:** `546169f66658b87c54e6166b79eba74628aa3d1e`

## Intent

Replace historical pass replay as the normal effective-state mechanism with
complete immutable snapshots. Each committed snapshot owns a self-contained
musical state represented by structurally shared references to immutable note and
non-note event objects. Consumers select the active snapshot directly; they do
not walk parent snapshots for content.

This branch intentionally starts before the maintained `LoopContentResolution`
lifecycle. That lifecycle is not reintroduced as a new effective-state owner.
Any compatibility query surface must be stateless and snapshot-based until its
callers are migrated and the surface is retired.

## Ownership boundary

- `Loop` owns active snapshot selection, candidate publication, and immutable
  object allocation.
- `LoopPasses` owns snapshot records and persistent reference roots.
- Existing capture, overlap, pairing, and NOTE_EDIT owners determine semantic
  mutations; snapshot storage applies those mutations.
- `Track` / `LoopPlaybackRuntime` owns mutable playback cursor, execution order,
  active ledger, and horizon state.
- `StorageManager` owns deferred persistence and serializes content, never runtime
  addresses.

## Migration stages

1. Complete formal architecture reassessment and record approval.
2. Implement immutable objects, loop-scoped handles, shared reference storage,
   candidate construction, and native stability/parity tests.
3. Publish candidates at existing capture and NOTE_EDIT commit boundaries while
   retaining compatibility pass records.
4. Migrate playback, display, NOTE_EDIT, analysis, and validation consumers in
   separate gates.
5. Migrate undo, clear-slot restore, load, and persistence after the physical
   storage design is approved.
6. Remove historical replay and compatibility state only after every caller gate
   and rollback trigger is closed.

## Invariants and exclusions

- Older snapshots and immutable objects remain semantically unchanged after later
  commits.
- Undo and redo select existing snapshots; they do not reconstruct state by
  replaying intervening history.
- Snapshot construction remains off the MIDI interval path until an approved phase
  defines a structural bound or an empirical bound with an overrun policy.
- No raw C++ pointers are persisted.
- Horizon size, projection math, cursor semantics, ledger semantics, capture
  finalization, overlap ownership, and NOTE_EDIT geometry ownership are unchanged
  by this proposal.

## Open decisions

- Persistent reference-collection representation and stable-handle reclamation.
- Storage version/layout, active-tip encoding, and old-format migration.
- Optional disposable indexes.
- Candidate construction memory/time bound and overrun behavior.
- Phase-specific work identity and verification gates.

Implementation is not authorized until the architecture review in the OpenSpec
change is approved and this branch is selected in `docs/Runtime/CURRENT_WORK.md`.
