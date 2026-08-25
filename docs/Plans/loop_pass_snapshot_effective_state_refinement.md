# Loop Pass Snapshot Effective State Refinement

**Status:** Proposed — architecture/specification only
**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Base:** `546169f66658b87c54e6166b79eba74628aa3d1e`

## Intent

Replace historical pass replay as the normal effective-state mechanism with
complete immutable `LoopSnapshot` objects. Each published snapshot owns a
self-contained musical state represented by a persistent / structurally-shared
reference root over immutable note and non-note event objects. Consumers select
the active snapshot directly; they do not walk parent snapshots for content.

`LoopPass` (`RecordPass`, `OverdubPass`, `EditPass`) remains the
historical/compatibility operation record. It is not the effective-state object.
A commit may produce both.

This branch intentionally starts before the maintained `LoopContentResolution`
lifecycle. That lifecycle is not reintroduced as a new effective-state owner.
Any compatibility query surface must be stateless and snapshot-based until its
callers are migrated and the surface is retired.

Phase 0 does not authorize firmware. Phase 2 remains blocked until the
persistent reference structure is selected against the recorded comparison
criteria.

## Core invariant

```text
Immutable event objects
        ^
        | references
┌─────────────────────┐
│   LoopSnapshot N    │
│ immutable root      │
└─────────────────────┘
        |
        | successor
        v
┌─────────────────────┐
│   LoopSnapshot N+1  │
│ new root            │
└─────────────────────┘
```

- Unchanged event: same object, reused reference.
- Changed event: new immutable object, replaced reference.
- Deleted event: reference removed from the new root.
- Added event: new immutable object, new reference.
- The reference root itself must be persistent / structurally shared so a
  bounded number of reference changes does not copy the complete snapshot
  reference set.

## Ownership boundary

- `Loop` owns active snapshot selection, candidate publication, and immutable
  object allocation.
- `LoopPasses` owns snapshot records, persistent reference roots, and
  compatibility `LoopPass` rows.
- Existing capture, overlap, pairing, and NOTE_EDIT owners determine semantic
  mutations; snapshot storage applies those mutations.
- `Track` / `LoopPlaybackRuntime` owns mutable playback cursor, execution order,
  active ledger, and horizon state.
- `StorageManager` owns deferred persistence and serializes content, never runtime
  addresses.

## Persistent reference-structure gate

Phase 0 recorded the invariant and comparison. The proposed selection is
pitch → bar → `NoteRef` in
[`loop_snapshot_pitch_bar_span_index_refinement.md`](loop_snapshot_pitch_bar_span_index_refinement.md).
That selection is not in the OpenSpec until that plan is applied.

Compare before Phase 2:

| Candidate | Why it is in the set |
|-----------|----------------------|
| Flat reference vector | Simplest; fails the no-full-copy invariant if selected |
| Persistent chunked / reference-directory | Copy changed chunks plus directory path |
| Path-copy ordered tree | Copy path of depth only |

Score each against update cost, bounded range iteration, Teensy internal-heap
and external-memory-pool allocation, handle identity and reclamation,
serialization/migration shape, and the maximum indivisible construction step.

Firmware publication is not authorized until that comparison selects a
representation that satisfies the persistent-root invariant.

## Candidate construction operations

Construction is not one unnamed “apply mutation” step. Before runtime
publication, declare a complexity and `ExecutionBounds.md` class for:

1. Locate affected references.
2. Remove obsolete references.
3. Allocate replacement event objects.
4. Insert new references.
5. Construct the new persistent reference root.
6. Retain/release roots.
7. Update optional disposable indexes, if any.
8. Freeze the candidate.
9. Publish the candidate atomically.

Any numeric N-candidate quantum, including a previously discussed
eight-candidate LCR unit, is a measurement hypothesis only. It is not part of
the snapshot architecture.

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
that operation on a runtime path. That cap is a consumer bound, not a
`LoopSnapshot` architectural unit.

## Derived representation relationship

[`DerivedViews.md`](../Authority/Architecture/DerivedViews.md) remains the
representation ownership contract. `LoopSnapshot` becomes the normal immutable
source and its identity becomes the source revision. `Loop` / `LoopPasses` do
not absorb ownership of `mergedEvents`, `visualCache`, playback order, analyze
indexes, or LED queries.

Each existing representation owner retains its own invalidation, bounded build,
and stale/fallback policy. Snapshot publication or selection invalidates stale
representations by identity; it does not require synchronous rebuilding of the
complete snapshot. Compatibility `LoopPass` records or materialized reads remain
available only as parity/fallback sources until the corresponding consumer gate
passes.

## Storage relationship

[`Storage.md`](../Authority/Architecture/Storage.md) remains the persistence
ownership contract. `StorageManager` serializes and loads the latest complete
`LoopSnapshot`; `Loop` rebuilds loop-local handles and selects the runtime
snapshot. The storage format does not absorb derived representations, playback
runtime state, undo history, or post-reboot undo lineage.

Large-loop save and compatibility migration use existing deferred
`StorageManager` units. They may increase total completion time, but they must
not perform content-sized synchronous work at record/overdub stop, inside
`handleMidiInput()`, during clock dispatch, or on a track-tick path. Each
interval-path unit requires the `ExecutionBounds.md` class, caps, progress,
and overrun/failure policy. Existing persisted state and the active snapshot
remain intact when work cannot complete.

After load or unplanned reboot: restore the latest saved complete snapshot,
set runtime undo depth to 0, leave redo empty, and discard unsaved session
tails.

## Lifetime, session, and branch semantics

Named roots:

```text
- active snapshot
- global undo before/after roots on the live branch
- session anchor
- session current provisional
- E: / O: session roots
- persistence staging
- consumer staging
- pending candidate parent
```

A snapshot or event object is reclaimable only when no named root and no
reachable snapshot reference collection still references it.

Provisional versus global:

- Record prefixes, overdub wraps, and NOTE_EDIT batches are playable
  provisional revisions.
- The final record-stop, overdub-stop, or note-edit close is the global undo
  transition.

Branching:

```text
A → B → C
      \
       D
```

After undo from C to B and commit of D, C is an abandoned branch and not a
redo target. C remains readable only while a remaining root holds it.

## Migration stages

1. Complete formal architecture reassessment and record approval.
2. Complete the persistent-reference comparison and measurement gate; select the
   structure before Phase 2.
3. Implement immutable objects, loop-scoped handles, shared reference storage,
   and invariant-protecting checks; define candidate construction bounds
   before runtime use.
4. Publish complete candidates at existing capture and NOTE_EDIT commit
   boundaries while retaining compatibility pass records; do not perform
   content-sized construction as one timing-critical operation.
5. Migrate playback, display, NOTE_EDIT, analysis, and validation consumers in
   separate gates; each representation retains its existing owner, source
   identity, invalidation, and bounded build policy.
6. Migrate undo, clear-slot restore, load, and persistence after the physical
   storage design is approved; keep serialization and compatibility migration
   under `StorageManager` deferred units.
7. Remove historical replay and compatibility state only after every caller gate
   and rollback trigger is closed.

## Design-grilling outcomes — 2026-08-25

The active OpenSpec design contains the detailed outcomes. The implementation
direction agreed during review is:

- `LoopSnapshot` is complete effective content only; the reference root is
  structurally shared, but parent traversal is not allowed.
- `LoopPass` remains the compatibility operation record.
- LCR preparation mechanics may be reused under `Loop` / `LoopPasses` snapshot
  ownership. LCR is not a second effective-state authority.
- NOTE_EDIT publishes after each complete `EditSessionActions` batch.
  `NoteEditCurrentState` is the mutable session buffer and `EditSession.store`
  is the projected content state. Batch snapshots are provisional until close.
- Overdub publishes after the settled wrap boundary in
  `Track::commitOverdubWrapAtSessionStart()`. Wrap snapshots are provisional
  until overdub stop.
- Record publishes completed events directly so a complete snapshot is
  available for deferred persistence. Those prefixes are provisional; only the
  final record-stop transition is a global undo snapshot.
- `capture.store` remains the capture buffer; playback consumes the active
  snapshot plus its unprocessed suffix.
- E: and O: select prior provisional snapshot identities. Session owners restore
  transient state separately; global undo does not cancel pending work.
- Pending work is valid only while its exact parent snapshot remains selected.
  Parent selection changes cause the owning handler to discard stale work.
- Pending work normally belongs to the focused slot; queued-slot preparation is
  the normal opportunity to make the next snapshot ready before a switch. If
  work is still pending during a switch, use the slot's latest complete snapshot
  without waiting and adopt newer content at a later safe playback boundary.
  Uninterrupted MIDI timing has priority over immediate content freshness.
- `StorageManager` persists only the latest complete snapshot. Unplanned reboot
  discards unsaved session tails and starts with undo/redo empty.
- Snapshot retention follows the named-root reachability model.
- The current wire contract is MIDI 1.0 channel voice. Ordinary Note On/Off
  messages do not carry the loop's internal `NoteId`, so snapshot resolution
  uses internal content identities and does not depend on playback voice
  handling. UMP, MIDI 2.0, Groups, Jitter Reduction timestamps, and MIDI 2.0
  per-note messages are outside this change.
- MIDI 1.0 zero-velocity Note On remains equivalent to Note Off; MIDI 2.0
  zero-velocity Note On is not. Equal-tick Note Off-before-Note On remains the
  project presentation-order policy.

### Deferred source-level retrigger pairing

Phase 0 retains the current `Track::recordMidiEvents()` and `pendingNotes`
behavior. Newly captured same-channel/same-pitch NoteOns therefore retain the
current LIFO pairing behavior; no source-level synthetic NoteOff is added when
an open `pendingNotes` entry is replaced.

Replace-on-retrigger remains a deferred future bugfix. That change would append
an internal synthetic NoteOff before replacing the open capture entry, and
would need its own decisions for canonical publication, persistence, undo, and
missing physical NoteOffs. Existing `finalizePendingNotes()` and
`LoopStopFinalize` stop-time closure is unchanged and is not part of this
deferred item.

These decisions remain design-only until the formal architecture reassessment
and approval are complete. The remaining implementation gate is to select the
persistent reference structure and classify the named construction operations
under `ExecutionBounds.md`.

### Timing evidence and measurement gate

The existing scheduling evidence gives an operating reference, not guaranteed
free time between MIDI boundaries:

- At 170 BPM, one MIDI clock interval is `14.71 ms` (`60 / (170 × 24)`).
- The current experimental maintenance target is `8.82 ms` (`60%` of one
  clock interval), pending measurement of `T_RT_reserve`.
- The previously discussed `2 ms` target and `5 ms` ceiling are withdrawn as
  established contracts.
- Measured post-optimization overdub note-change accumulation is normally
  `0.75–2.0 ms`, with a `3.6 ms` peak. That measures candidate discovery and
  overlap accumulation, not immutable snapshot path-copy publication.
- Existing LCR index and reconstruction slices reached `63–121 ms` and
  `115.7 ms`, respectively. They are not valid bounds for snapshot successor
  construction.

Before runtime publication is authorized, measure the selected representation’s
successor-construction unit on the device. Any numeric N-candidate quantum is a
measurement hypothesis only. A failure requires changing the candidate quantum
or construction representation before publication work proceeds.

## Invariants and exclusions

- Older snapshots and immutable objects remain semantically unchanged after later
  commits.
- Undo and redo select existing snapshots; they do not reconstruct state by
  replaying intervening history.
- New work after undo clears the redo branch.
- Intermediate session prefixes are provisional, not global undo entries.
- Reboot loads the latest saved complete snapshot and starts with undo/redo empty.
- Snapshot construction remains off the MIDI interval path until an approved phase
  defines a structural bound or an empirical bound with an overrun policy.
- No raw C++ pointers are persisted.
- Horizon size, projection math, cursor semantics, ledger semantics, capture
  finalization, overlap ownership, and NOTE_EDIT geometry ownership are unchanged
  by this proposal.
- A complete `LoopSnapshot` does not authorize full-loop projection on a
  timing-critical path; consumers select bounded span ranges before
  `IntervalProjection`.
- This change does not add a new test architecture. Later phases use existing
  gates plus invariant-protecting checks only.

## Open decisions

- Persistent reference-collection representation: proposed selection is pitch →
  bar → `NoteRef` in
  [`loop_snapshot_pitch_bar_span_index_refinement.md`](loop_snapshot_pitch_bar_span_index_refinement.md).
  Not applied to OpenSpec until that plan is executed.
- Stable-handle reclamation implementation.
- Storage version/layout and old-format migration.
- Optional disposable indexes and bounded consumer span access.
- Candidate construction memory/time bound and overrun behavior for each named
  operation.
- Long-span projection candidate limit and overrun behavior (consumer bound).
- Phase-specific work identity.

Implementation is not authorized until the architecture review in the OpenSpec
change is approved, the reference-structure gate is complete, and this branch is
selected in `docs/Runtime/CURRENT_WORK.md`.
