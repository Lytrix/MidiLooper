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

## Execution-bound refinement for large work

The [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md)
classification applies to snapshot construction, not only to clock dispatch.
The existing commit boundaries are reachable from timing-sensitive paths:
`Track::stopRecording` and `Track::stopOverdubbing` can be invoked by input
actions, `Track::commitOverdubWrapAtSessionStart` runs during
`TrackManager::updateAllTracks`, and NOTE_EDIT close is reachable from input
dispatch. “Off-clock”, deferred placement, or cold/flash placement therefore
does not make content-sized construction safe.

Before any phase introduces runtime candidate construction:

- classify the `Loop` / `LoopPasses` owner in the maintained inventory;
- define the input and storage cap for one unit, the maximum indivisible step,
  persistent progress, and completion state;
- use a structurally bounded unit, or an empirically bounded unit with an
  explicit overrun guard and failure policy;
- keep total completion time proportional to loop content while keeping each
  unit independent of total loop length; and
- preserve the active snapshot and settled mutation when a unit cannot fit,
  overruns, or allocation fails. No partial candidate may be published.

A local timer checked only between steps is not a bound on an indivisible scan,
allocation, sort, or nested operation. A complete candidate may be built over
multiple bounded opportunities, but publication remains one atomic operation
after completion. The phase must name the existing owner that holds progress
and the policy for stale or failed construction; no new scheduler or parallel
semantic owner is introduced by this refinement.

## IntervalProjection relationship

`LoopSnapshot` supplies complete canonical immutable content. It does not store
projected intervals or consumer windows. Playback, display, and NOTE_EDIT
consumers select the relevant canonical span range and then call the existing
`IntervalProjection` owner with their own `ProjectionContext`.

For large loops, a complete snapshot does not authorize full-loop projection on
a timing-critical path. `projectDisplayNotes` must use its bounded span-range
form for windowed consumers; playback keeps scalar `playbackEventPhase` in the
hot path. Full-loop display or edit projection remains an explicit
non-timing-critical operation and must still satisfy `ExecutionBounds.md`.

The projection window bounds selected output, not necessarily Stage 1 work:
`generateEquivalentIntervals` derives a `k` range from each span and the
window. A span crossing multiple loop lengths can therefore produce a
content-dependent candidate count. Runtime phases must define a structural
candidate cap or an empirical overrun guard and failure policy before using
that operation on a runtime path.

## Migration stages

1. Complete formal architecture reassessment and record approval.
2. Implement immutable objects, loop-scoped handles, shared reference storage,
   and native stability/parity tests; define candidate construction bounds
   before runtime use.
3. Publish complete candidates at existing capture and NOTE_EDIT commit
   boundaries while retaining compatibility pass records; do not perform
   content-sized construction as one timing-critical operation.
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
- A complete `LoopSnapshot` does not authorize full-loop projection on a
  timing-critical path; consumers select bounded span ranges before
  `IntervalProjection`.

## Open decisions

- Persistent reference-collection representation and stable-handle reclamation.
- Storage version/layout, active-tip encoding, and old-format migration.
- Optional disposable indexes and bounded consumer span access.
- Candidate construction memory/time bound and overrun behavior.
- Long-span projection candidate limit and overrun behavior.
- Phase-specific work identity and verification gates.

Implementation is not authorized until the architecture review in the OpenSpec
change is approved and this branch is selected in `docs/Runtime/CURRENT_WORK.md`.
