## Context

At the selected base commit, `LoopPasses` stores chunk-backed capture passes and edit rows, and `LoopPasses::materializeToEventVector()` derives effective MIDI. `PersistedLoopSnapshot` exists, but `Loop::sharePassesSnapshot()` uses `deepClonePasses()`; it is not a structurally shared immutable effective-state snapshot. The maintained-LCR lifecycle added after this base is deliberately not carried into this feature branch.

The target is a complete immutable snapshot at each committed state boundary. Snapshots contain immutable note and non-note event objects through loop-scoped stable handles. Playback retains mutable cursor, execution order, ledger, and horizon ownership. Existing capture finalization, overlap/pairing, NOTE_EDIT geometry, and deferred persistence owners remain unchanged until separately approved migration phases.

The existing commit boundaries are not automatically outside the timing-critical
path. `Track::stopRecording` and `Track::stopOverdubbing` can commit from input
actions, `Track::commitOverdubWrapAtSessionStart` commits during
`TrackManager::updateAllTracks`, and NOTE_EDIT close is reachable from input
dispatch. Therefore, “off-clock” is not a sufficient safety claim: candidate
construction that runs in any of those paths is interval-path work or
timing-critical input work and must satisfy
[ExecutionBounds.md](../../docs/Authority/Architecture/ExecutionBounds.md).

## Goals / Non-Goals

**Goals:**

- Make snapshots self-contained and directly selectable.
- Preserve logical `NoteId` across immutable object versions.
- Separate semantic commit boundaries from candidate construction: construct a
  complete candidate in bounded resumable units where required, then atomically
  publish it at the existing boundary.
- Use snapshot identities for undo, redo, clear-slot restore, and consumer freshness.
- Use the existing event-stream implementation as a parity oracle during migration.
- Define the execution-bound classification, progress state, and overrun policy
  before candidate construction is introduced on a runtime path.

**Non-Goals:**

- No firmware implementation in the architecture phase.
- No maintained LCR owner, new scheduler, or new interval-path owner.
- No change to horizon size, projection math, cursor, ledger, or MIDI deadline behavior.
- No raw pointer persistence.
- No decision here on persistent sequence structure, reclamation, exact storage layout, or deduplication.
- No removal of visual cache, capture preview, overdub source-view storage, or playback scratch state.
- No content-sized candidate construction may run as one indivisible operation
  from `handleMidiInput()`, clock dispatch, or a track-tick path.

### The four-layer runtime boundary remains intact

The parent runtime architecture keeps these contracts separate:

```text
LoopSnapshot authoritative content
    → derived representation owned by the existing module
    → consumer-selected bounded interval projection
    → runtime request and consumer result
```

The snapshot change does not make `LoopSnapshot` a consumer-window store, an
`IntervalProjection` owner, a playback-runtime owner, or a persistence
scheduler. A consumer migration changes its content source and freshness
identity, not the ownership of cursor, ledger, playback order, display
windows, analyze indexes, LED queries, or deferred persistence.

## Decisions

### `Loop` publishes and `LoopPasses` stores snapshots

`Loop` owns active selection, candidate publication, and immutable object allocation. `LoopPasses` owns snapshot records and reference roots without becoming a semantic manager. This preserves the existing content ownership boundary.

**Alternative:** make `LoopContentResolution` the snapshot owner. Rejected because it would retain the duplicate derived-state ownership this branch is intended to avoid.

### Stable handles reference immutable objects

Snapshot references are loop-scoped handles, not persisted addresses. Editing a note creates a new immutable version with the same logical `NoteId`; older snapshots retain their original version.

**Alternative:** raw pointers. Rejected because addresses are not persistent identity and make reclamation implicit.

### `LoopSnapshot` supplies content; `IntervalProjection` supplies coordinates

`LoopSnapshot` stores complete canonical immutable note and non-note content
through loop-scoped references. It does not store projected intervals, playback
windows, display windows, or edit windows because those depend on consumer
context such as playhead, viewport, loop origin, queued start, and selected
tick.

Consumers select the relevant canonical span range from the selected snapshot
before calling `IntervalProjection`. `IntervalProjection` remains the
orthogonal pure-math owner: `playbackEventPhase` serves scalar playback
projection, while display and edit projection operate on caller-selected span
ranges. A full-snapshot projection is an explicit non-normal path and is not
admissible on a timing-critical path for a large loop.

The requested projection window does not alone establish a structural bound.
`generateEquivalentIntervals` calculates a `k` range from the span and window;
if one canonical span crosses multiple loop lengths, the number of candidates
can grow with that span. Any runtime caller must therefore define a maximum
candidate count or use the empirically bounded class with an explicit guard and
failure policy under `ExecutionBounds.md`.

**Alternative:** store projected windows in `LoopSnapshot`. Rejected because it
couples immutable content lifetime to mutable consumer contexts and duplicates
playback/display/edit representation ownership.

### `LoopSnapshot` is the source for derived representations

The target relationship defined by
[`DerivedViews.md`](../../docs/Authority/Architecture/DerivedViews.md) is:

```text
LoopSnapshot
    → existing representation owner
    → bounded canonical span selection
    → IntervalProjection where coordinates must be projected
    → consumer result
```

`LoopSnapshot` supplies the immutable source and source identity. It does not
own `mergedEvents`, `visualCache`, playback order, analyze indexes, or LED
queries. `Track` / `LoopPlaybackRuntime`, `Loop`, `EditManager` during live
editing, and `MidiLedManager` retain their existing representation or query
ownership and build policy.

Each derived representation records the snapshot identity from which it was
built. Snapshot publication or selection invalidates representations by that
identity; it does not require all representations to rebuild synchronously.
While bounded rebuilding is pending, the consumer follows its existing
freshness policy and must not perform a full-loop rebuild on a timing-critical
path. `LoopPasses` and materialized compatibility data may remain as parity or
fallback sources until the individual consumer migration gate passes.

**Alternative:** make `LoopSnapshot` the owner of all derived representations.
Rejected because it would couple immutable content publication to consumer
lifecycles and create a new cross-consumer owner.

### Construction applies existing semantic mutations

At an existing commit boundary, the existing semantic owner supplies a settled
mutation. Candidate construction reads the active snapshot as const and applies
that mutation to a structurally shared collection. If the work is larger than
one admissible runtime unit, construction uses persistent progress and yields
between bounded units; it does not block until a complete candidate is
available. The candidate is frozen and published atomically only after it is
complete.

**Alternative:** let snapshot storage replay or resolve edits. Rejected because it creates a second semantic owner.

### Large candidate construction follows ExecutionBounds

The maintained inventory in
[ExecutionBounds.md](../../docs/Authority/Architecture/ExecutionBounds.md) is
the gate for runtime candidate construction. Each phase that introduces or
extends this work must identify the `Loop` / `LoopPasses` owner row and declare:

- the input and storage cap for one unit;
- the maximum indivisible step;
- persistent progress and completion state;
- a structural bound, or a measured bound with an explicit overrun guard and
  failure policy; and
- the behavior when work does not fit or overruns.

The total completion time may grow with loop content, but one unit's work and
storage cannot grow with total loop length. A timer checked only between steps
does not bound an indivisible scan, allocation, sort, or nested operation.
“Deferred”, “off-clock”, and cold/flash placement do not change this
classification.

The active snapshot remains available while a candidate is incomplete. An
overrun or allocation failure leaves the active and historical snapshots
unchanged, publishes no partial candidate, and follows the phase's explicit
retry, defer, or rejection policy without silently losing the settled
mutation.

### Undo and playback select snapshots directly

Undo entries retain before/after snapshot identities. Undo, redo, and clear-slot restore select complete snapshots without replaying intervening history. Playback derives execution order from a const active snapshot while retaining runtime cursor, ledger, and horizon ownership.

**Alternative:** disable rows and replay to reconstruct. Rejected because it preserves history-proportional work.

### Persistence is a separate physical design

The runtime model requires serialization of immutable objects and snapshot
structure in a future format. Exact layout, reclamation, active-tip encoding,
and old-format migration are separate decisions. Migration uses the existing
`StorageManager` deferred owner; any unit that executes between
`handleMidiInput()` entries still requires an admissible
`ExecutionBounds.md` classification.

The storage boundary is content-only: the future format stores immutable
objects, stable logical references, snapshot roots, loop metadata, and the
serialized active branch tip. It does not store runtime addresses, derived
representations, playback cursor/order, active ledgers, or undo history.
`StorageManager` serializes and loads this content; `Loop` rebuilds local
handles and owns runtime snapshot selection.

Large-loop save and migration may take longer in total, but neither may become
a content-sized synchronous operation at record/overdub stop, inside
`handleMidiInput()`, during clock dispatch, or on a track-tick path. Existing
`StorageManager` deferred units carry progress and retain the active snapshot
and previously valid persisted state when a unit cannot fit, overruns, or fails.
The phase must define retention/retry/rejection behavior without silently
discarding pending content.

## Risks / Trade-offs

- **[Memory overhead]** Immutable versions retain history → measure object/reference pools and admission before firmware.
- **[Construction cost]** Mutations may be content-dependent → classify the
  owner in `ExecutionBounds.md`, use bounded resumable units, and define an
  explicit overrun policy before runtime publication.
- **[Projection cost]** A requested window does not cap the number of
  `generateEquivalentIntervals` candidates for a span crossing multiple loop
  lengths → bound the source span range and candidate count before runtime
  projection.
- **[Parity]** Snapshot output may diverge → compare against current materialization for overlap, pairing, wrap, ordering, and non-note events.
- **[Lifetime]** Retained snapshots may leak or alias mutable data → test reachability, loop scope, and older-snapshot stability.
- **[Compatibility]** Storage changes may reject existing files → retain old readers until migration and round-trip gates pass.

## Migration Plan

1. Complete formal architecture reassessment and record approval before firmware work.
2. Implement immutable objects, stable handles, shared reference storage, and native tests with current consumers unchanged.
3. Define and verify candidate construction bounds, progress, and overrun behavior before runtime publication.
4. Adapt capture and NOTE_EDIT commit boundaries while retaining compatibility pass records; publication may occur only after a complete candidate is ready.
5. Migrate playback and other consumers independently, using snapshot identity and parity gates.
6. Migrate undo/load/persistence after physical format approval.
7. Remove replay/materialized compatibility paths only after all caller-specific gates pass.

Rollback keeps the prior representation active and discards any failed candidate. Existing storage readers remain until the new reader and migration are proven.

## Design-grilling outcomes — 2026-08-25

These outcomes sharpen the proposed Phase 0 model. They remain pending the formal
architecture reassessment and approval recorded in `ARCHITECTURE-REVIEW.md`.

### Complete content and LCR role

- A published `LoopSnapshot` is complete effective canonical content. Structural
  sharing is permitted, but consumers never traverse a parent snapshot for
  missing content.
- LCR preparation mechanics may be reused under `Loop` / `LoopPasses` snapshot
  ownership. LCR is not the maintained effective-state authority.
- Heavy preparation may advance through existing LCR preparation opportunities.
  Semantic resolution publishes a complete candidate directly after its settled
  boundary; no partial candidate is published.

### MIDI protocol boundary

- The current firmware scope is MIDI 1.0 channel voice over the existing USB,
  USB Host, and DIN serial paths. UMP, MIDI 2.0 channel voice, Groups, Jitter
  Reduction timestamps, and MIDI 2.0 per-note messages are out of scope.
- MIDI 1.0 Note On/Off carries channel, note number, and velocity, not the
  loop's internal `NoteId`. Snapshot resolution therefore operates on internal
  content identities and never asks playback to identify a wire-level note.
- Same-pitch receiver voice behavior is not used as the snapshot identity
  model. A complete snapshot still updates the source note's effective span,
  replaces any stale wrapped NoteOff, and stores the incoming note's complete
  span.
- MIDI 1.0 zero-velocity Note On is treated as Note Off; this rule must not be
  applied to MIDI 2.0 Note On messages.
- Equal-tick Note Off-before-Note On remains the project presentation-order
  policy. The protocol boundary does not remove the need to keep the complete
  source geometry correct.

### Session publication boundaries

- NOTE_EDIT publishes after each complete `EditSessionActions` batch produced by
  `NoteGeometryResolver`. `NoteEditCurrentState` remains the mutable NOTE_EDIT
  buffer; `EditSession.store` remains its projected content state. No separate
  NOTE_EDIT action buffer is introduced.
- Overdub publishes after the existing wrap-level settled boundary:
  `Track::commitOverdubWrapAtSessionStart()` seals the overdub pass and companion
  changes before candidate publication. Raw incomplete note events are not
  published as snapshots.
- Record uses the same direct-publication model for completed events so a latest
  complete snapshot is available for deferred persistence while recording is
  active.
- `capture.store` remains the capture buffer. Once a processed prefix is
  published, live playback reads the active snapshot plus only the unprocessed
  capture suffix.

### Provisional snapshot and undo behavior

- The session-start snapshot is retained as the rollback anchor. Prefix snapshots
  are playable provisional revisions, not separate global undo entries.
- E: and O: session undo select the previous provisional snapshot identity
  directly. Existing session owners restore their transient state separately;
  `LoopSnapshot` contains no selection, focus, capture, ledger, cursor, or other
  runtime state.
- A pending child is valid only while its exact parent snapshot identity remains
  selected. A parent change causes each existing pending-work handler to discard
  its stale child/progress; global undo does not own cancellation.
- UI/session exit may occur while candidate work remains. Same-slot capture or
  NOTE_EDIT admission is blocked or deferred until that work completes, while
  other slots may continue.
- If pending work cannot complete after its retry policy, the latest complete
  prefix remains the result and the remaining mutation is explicitly rejected.
- Snapshot retention is limited to the session anchor, current revision, E:/O:
  entries, and persistence staging. Record releases older event-prefix roots once
  the next complete snapshot publishes.
- Pending candidate work normally belongs to the focused slot. Queued-slot
  preparation remains the normal opportunity for making the next complete
  snapshot ready before a switch.
- If a slot switch encounters pending work despite that preparation, switching
  uses the slot's latest complete snapshot without waiting. The newer snapshot
  is adopted at a later safe playback boundary; uninterrupted MIDI timing has
  priority over immediately exposing the newest content.

### Persistence and reboot

- `StorageManager` may persist the latest complete snapshot through its existing
  deferred units while a session is active. Open session state and unresolved
  candidate work are not serialized.
- After an unplanned reboot, the latest saved complete snapshot is restored and
  the unsaved session tail is discarded.

### Execution boundary

Post-exit candidate work resumes through existing
`Track::processDeferredIdleMaintenance()` opportunities after MIDI service.
Candidate construction is not introduced as an unbounded operation in
`handleMidiInput()`, clock dispatch, or `TrackManager::updateAllTracks()`.
The implementation phase must still classify the direct candidate delta and the
LCR preparation units under `ExecutionBounds.md`; preparation readiness does not
replace the indivisible-step bound.

## Open Questions

- Which persistent reference structure and reclamation policy meet memory and lifetime requirements?
- What exact storage version/layout and compatibility migration are required?
- Which optional indexes are justified without becoming a second authority?
- What execution-bound class, unit cap, progress state, and overrun policy
  will candidate construction use on each runtime path?
- Which existing owner holds an incomplete candidate and its continuation, and
  how is a stale candidate discarded if the active snapshot changes?
- Which phase and work identity authorize each consumer migration?
