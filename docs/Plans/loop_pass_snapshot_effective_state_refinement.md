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

## Derived representation relationship

[`DerivedViews.md`](../Authority/Architecture/DerivedViews.md) remains the
representation ownership contract. `LoopSnapshot` becomes the normal immutable
source and its identity becomes the source revision. `Loop` / `LoopPasses` do
not absorb ownership of `mergedEvents`, `visualCache`, playback order, analyze
indexes, or LED queries.

Each existing representation owner retains its own invalidation, bounded build,
and stale/fallback policy. Snapshot publication or selection invalidates stale
representations by identity; it does not require synchronous rebuilding of the
complete snapshot. Compatibility `LoopPasses` or materialized reads remain
available only as parity/fallback sources until the corresponding consumer gate
passes.

## Storage relationship

[`Storage.md`](../Authority/Architecture/Storage.md) remains the persistence
ownership contract. `StorageManager` serializes and loads the content required
for a complete `LoopSnapshot`; `Loop` rebuilds loop-local handles and selects
the runtime snapshot. The storage format does not absorb derived
representations, playback runtime state, or undo history.

Large-loop save and compatibility migration use existing deferred
`StorageManager` units. They may increase total completion time, but they must
not perform content-sized synchronous work at record/overdub stop, inside
`handleMidiInput()`, during clock dispatch, or on a track-tick path. Each
interval-path unit requires the `ExecutionBounds.md` class, caps, progress,
and overrun/failure policy. Existing persisted state and the active snapshot
remain intact when work cannot complete.

## Migration stages

1. Complete formal architecture reassessment and record approval.
2. Implement immutable objects, loop-scoped handles, shared reference storage,
   and native stability/parity tests; define candidate construction bounds
   before runtime use.
3. Publish complete candidates at existing capture and NOTE_EDIT commit
   boundaries while retaining compatibility pass records; do not perform
   content-sized construction as one timing-critical operation.
4. Migrate playback, display, NOTE_EDIT, analysis, and validation consumers in
   separate gates; each representation retains its existing owner, source
   identity, invalidation, and bounded build policy.
5. Migrate undo, clear-slot restore, load, and persistence after the physical
   storage design is approved; keep serialization and compatibility migration
   under `StorageManager` deferred units.
6. Remove historical replay and compatibility state only after every caller gate
   and rollback trigger is closed.

## Design-grilling outcomes — 2026-08-25

The active OpenSpec design contains the detailed outcomes. The implementation
direction agreed during review is:

- `LoopSnapshot` is complete effective content only; structural sharing is
  allowed, but parent traversal is not.
- LCR preparation mechanics may be reused under `Loop` / `LoopPasses` snapshot
  ownership. LCR is not a second effective-state authority.
- NOTE_EDIT publishes after each complete `EditSessionActions` batch.
  `NoteEditCurrentState` is the mutable session buffer and `EditSession.store`
  is the projected content state.
- Overdub publishes after the settled wrap boundary in
  `Track::commitOverdubWrapAtSessionStart()`. Record publishes completed events
  directly so a complete snapshot is available for deferred persistence.
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
  discards unsaved session tails.
- Snapshot retention is limited to the session anchor, current revision,
  session-undo references, and persistence staging.
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
and approval are complete. The remaining implementation gate is to classify the
direct candidate delta and LCR preparation units under `ExecutionBounds.md`,
including their indivisible work and failure behavior.

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
  `115.7 ms`, respectively. They are not valid bounds for the proposed
  eight-candidate unit.

Before runtime publication is authorized, measure one complete eight-candidate
unit on the device, including immutable path copies, `NoteId` index updates,
replacement/tombstone records, allocation behavior, and wrap plus same-tick
cases. The initial falsification rule is p95 below `8.82 ms`; any indivisible
unit above `25 ms` fails investigation. These are engineering gates, not hard
MIDI guarantees. A failure requires changing the candidate quantum or
construction representation before publication work proceeds.

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
