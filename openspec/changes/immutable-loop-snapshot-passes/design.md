## Context

At the selected base commit, `LoopPasses` stores chunk-backed capture passes and edit rows, and `LoopPasses::materializeToEventVector()` derives effective MIDI. `PersistedLoopSnapshot` exists, but `Loop::sharePassesSnapshot()` uses `deepClonePasses()`; it is not a structurally shared immutable effective-state snapshot. The maintained-LCR lifecycle added after this base is deliberately not carried into this feature branch.

The target is a complete immutable `LoopSnapshot` at each published state boundary. A snapshot contains an immutable reference root over immutable note and non-note event objects through loop-scoped stable handles. Playback retains mutable cursor, execution order, ledger, and horizon ownership. Existing capture finalization, overlap/pairing, NOTE_EDIT geometry, and deferred persistence owners remain unchanged until separately approved migration phases.

`LoopPass` remains the historical/compatibility operation record (`RecordPass`, `OverdubPass`, `EditPass`). It is not the effective-state object.

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

- Make snapshots self-contained and directly selectable. A snapshot’s logical content is completely determined by its own reference root; resolving a reference or answering a content query never requires consulting a parent snapshot.
- Keep `LoopPass` and `LoopSnapshot` as distinct objects.
- Preserve logical `NoteId` across immutable object versions.
- Reuse unchanged event-object references; allocate new objects only for added or modified content.
- Index notes as pitch → bar → bar membership lists of `NoteRef`s and events as bar → `EventRef` lists. The event list is an indexed view of existing event-stream order, not a second ordering authority. Unchanged pitch/bar membership remains shared; only dirty membership and the minimum directory path to it are newly constructed.
- Separate semantic commit boundaries from candidate construction: construct a
  complete candidate in bounded resumable units where required, then atomically
  publish it at the existing boundary.
- Use snapshot identities for undo, redo, clear-slot restore, and consumer freshness.
- Use the existing event-stream implementation as a parity oracle during migration.
- Define the execution-bound classification, progress state, and overrun policy
  before candidate construction is introduced on a runtime path.

**Non-Goals:**

- No firmware implementation in the architecture phase.
- No selection of a physical pitch/bar directory representation in Phase 0; Phase 1 / measurement chooses how the immutable directory shares unchanged membership.
- No maintained LCR owner, new scheduler, or new interval-path owner.
- No change to horizon size, projection math, cursor, ledger, or MIDI deadline behavior.
- No raw pointer persistence.
- No decision here on exact storage layout, optional indexes, or object-pool implementation.
- `LoopSnapshot` replaces authoritative effective content. Everything else retains its ownership until its migration gate.
- No content-sized candidate construction may run as one indivisible operation
  from `handleMidiInput()`, clock dispatch, or a track-tick path.
- No new testing architecture or broad test campaign.

### Specification hierarchy

```text
Core architecture
    1. Snapshot object model
    2. Pitch/bar logical indexes; physical directory later
    3. Candidate mutation
    4. Publication
    5. Selection
    6. Lifetime

Integration contracts
    7. Capture
    8. NOTE_EDIT
    9. Undo/redo
   10. Playback
   11. Persistence

Migration gates
   12. LCR retirement
   13. Compatibility materialization removal
   14. Derived-view migration
```

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

### `LoopSnapshot` is the effective-state object; `LoopPass` is not

`LoopSnapshot` is the complete immutable effective-state reference root.

`LoopPass` (`RecordPass`, `OverdubPass`, `EditPass`) is the historical and
compatibility operation record stored on `LoopPasses` during migration.

A settled commit may produce both:

```text
OverdubPass          +          LoopSnapshot
compatibility record            immutable effective-state root
```

Calling both objects `LoopPass` is rejected. Undo selects `LoopSnapshot`
identities. Compatibility pass rows remain until their caller gates pass.

**Alternative:** treat each committed `LoopPass` as the complete snapshot.
Rejected because pass history and effective-state roots have different
lifetime, undo, and persistence semantics.

### `Loop` publishes and `LoopPasses` stores snapshots

`Loop` owns active selection, candidate publication, and immutable object allocation. `LoopPasses` owns snapshot records and reference roots without becoming a semantic manager. Compatibility pass rows remain on `LoopPasses` until retirement. This preserves the existing content ownership boundary.

**Alternative:** make `LoopContentResolution` the snapshot owner. Rejected because it would retain the duplicate derived-state ownership this branch is intended to avoid.

### Stable handles reference immutable objects

Snapshot references are loop-scoped handles (`NoteRef` / `EventRef`), not persisted addresses. Editing a note creates a new immutable version with the same logical `NoteId`; older snapshots retain their original version.

**Alternative:** raw pointers. Rejected because addresses are not persistent identity and make reclamation implicit.

### Logical indexes: pitch → bar → NoteRef, and bar → EventRef

A snapshot SHALL represent effective content through two logical indexes, not
one flat reference collection and not a path-copy tree used as the overlap
model.

```text
incoming span
    → same pitch
    → intersecting 1-bar membership lists
    → NoteRefs (deduplicated by NoteId)
    → existing overlap resolver
    → dirty pitch/bar membership
    → new LoopSnapshot root
```

**Span index (overlap / complete-note lookup).** Keyed by pitch, then by
canonical 768-tick bar. Each bar holds a **membership list of `NoteRef`s**:
loop-scoped handles to complete immutable note objects whose geometry
intersects that bar. A note that spans several bars appears in each
intersecting list; every occurrence identifies the same immutable note
version and `NoteId`. Candidate sets deduplicate by `NoteId`. Overlap remains
pitch + tick (`DEC-032`); channel is not an index key.

The span index stores complete semantic notes only. It SHALL NOT store
unmatched open Ons. Occupy / open-at-S remains `ActiveNoteLedger` /
`presentAt` at the consumer, not a third snapshot index.

**Event index (playback / display / analyze).** Keyed by the same 1-bar
granule. Each bar holds a membership list of `EventRef`s for note and
non-note events whose tick falls in that bar. Canonical event ordering
remains the existing event-stream semantics; the bar list is an indexed view
in that order, not a second ordering authority. Non-note events are points
and do not appear in the span index.

**Dirty region is semantic, not “incoming bars only.”** For an incoming note:

```text
dirtyPitch = incoming.pitch
dirtyBars  = union of bars intersected by:
    the incoming span
    + every existing note whose geometry changes
    + every existing note whose membership changes because of resolution
```

The existing overlap / pairing / NOTE_EDIT geometry owner determines which
note geometries changed and returns those changed `NoteRef`s and affected
spans. Snapshot construction translates that mutation into pitch/bar
membership updates. Snapshot storage SHALL NOT become a second overlap owner.

Candidate selection and object mutation are different: inspecting `A` because
it occupies a dirty bar does not replace `A`. Only notes the resolver
replaces allocate a new immutable version.

**Wrap-crossing notes** keep the existing two linear segments
(`[start, loopEnd)` and `[0, end)`). Each segment contributes membership in
the bars it intersects; both membership sets point to the same `NoteId`. A
query near the loop boundary unions end-side bars and zero-side bars, then
deduplicates by `NoteId`. Wrap notes are not a special index type.

**Long notes.** A note intersecting *B* bars costs *B* `NoteRef` memberships.
That cost is domain-visible. A 1000-bar note can still touch 1000 bars. This
logical model does **not** by itself make candidate construction bounded.

**Logical sharing invariant.** Only dirty pitch/bar membership nodes and the
minimum directory path required to reach them MAY be newly allocated.
Unchanged pitch/bar membership remains shared with the parent snapshot.

**Physical directory representation was not selected by Phase 0.** Phase 1
measurement selected a sparse fixed-capacity paged directory experiment for
the native model. Path-copy is rejected as the *semantic overlap model*; it
is not rejected as a possible directory implementation. Page capacity,
allocator behavior, serialization, reclamation, and the maximum indivisible
construction step remain measured/runtime-gated properties. “Dirty bars” does
not automatically solve directory sharing.

Steps that identify and rewrite membership have **semantic locality** bounded
by the affected pitch and dirty bars. Their execution cost remains dependent
on the number of intersecting memberships and the selected physical directory
and therefore requires measurement before runtime publication. Path depth is
a directory-implementation detail, not that locality.

```text
LoopSnapshot N                         LoopSnapshot N+1
  pitch 60                               pitch 60
    bar 2 [A]          shared              bar 2 [A]
    bar 3 [A]          shared              bar 3 [A]
    bar 4 [A, C]       new                 bar 4 [A, C', B]
    bar 5 [A]          new                 bar 5 [A, B]
```

Incoming B (pitch 60, bars 4–5) yields candidates `{A, C}` by `NoteId`
dedup. Resolver may change only C → C' and add B; A is inspected, not
replaced. Dirty bars are 4 and 5 (and any further bars the resolver reports
if C's geometry moved).

**Rejected alternatives:**

| Alternative | Why rejected |
|-------------|--------------|
| Flat full-loop `NoteRef` vector as the overlap/candidate structure | Successor copies or scans all loop content |
| Path-copy ordered tree as the overlap/semantic model | Overlap is pitch + tick in bars, not tree-path identity |
| On/Off events in one bar as the overlap index | Misses notes that occupy the bar without an event in it |
| Unmatched open Ons in the span index | Span index is complete notes only; ledger owns open-at-S |
| Two-bar index cells | Playback’s 2-bar window is a consumer, not the index granule |
| Tile / lane / bucket / grid / TileManager | Bar is the domain time unit; do not invent a partitioning noun |

**Alternative:** copy a complete `NoteRef` / `EventRef` vector per snapshot while sharing event objects. Rejected because successor construction would remain O(N) in snapshot size.

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

That projection-candidate count is a consumer-side `IntervalProjection` bound.
It is not a `LoopSnapshot` architectural unit.

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
path. Compatibility `LoopPass` records and materialized data may remain as
parity or fallback sources until the individual consumer migration gate passes.

**Alternative:** make `LoopSnapshot` the owner of all derived representations.
Rejected because it would couple immutable content publication to consumer
lifecycles and create a new cross-consumer owner.

### Construction applies existing semantic mutations

At an existing commit boundary, the existing semantic owner supplies a settled
mutation. Candidate construction reads the active snapshot as const and applies
that mutation to the pitch/bar membership indexes.

The architectural construction contract is:

1. Identify affected content.
2. Apply immutable object replacements.
3. Construct the successor reference root.
4. Freeze the candidate.
5. Atomically publish.

Phase 1 decomposes those steps into measured operations (including retain/release
and any later optional indexes). Those decompositions are not Phase 0 architecture.

If the work is larger than one admissible runtime unit, construction uses
persistent progress and yields between bounded units; it does not block until a
complete candidate is available. The candidate is frozen and published atomically
only after it is complete.

Identify/apply/construct have semantic locality bounded by the affected pitch
and dirty bars. Their execution cost remains dependent on intersecting
memberships and the selected physical directory and therefore requires
measurement before runtime publication. A long note can still touch every bar
it intersects; this model does not by itself make construction bounded.

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

### Provisional session snapshots versus global undo snapshots

Intermediate complete prefixes during an open session are playable provisional
revisions. They are not global undo entries.

```text
S10  session anchor (global before)
S11  provisional prefix
S12  provisional prefix
S13  current provisional
        |
        | session commit
        v
S13  becomes the global after snapshot
```

- Record: completed-event prefixes may publish provisional snapshots so playback
  and deferred persistence can read a latest complete snapshot while recording
  is active. Only the final record-stop transition enters global undo.
- Overdub: wrap-level publication stays at
  `Track::commitOverdubWrapAtSessionStart()`. Those wrap snapshots are
  provisional / session (`O:`) revisions. One `OverdubPassAdded` global undo
  unit is recorded at overdub stop.
- NOTE_EDIT: each complete `EditSessionActions` batch may publish a provisional
  session (`E:`) snapshot. One `NoteEditPassClosed` global undo unit is recorded
  at the existing close/commit boundary.

**Alternative:** every completed prefix is a global snapshot revision. Rejected
because record and wrap prefixes would inflate global undo and persistence
history.

### Snapshot lifetime is reachability from named roots

A snapshot or immutable event object is reclaimable only when no named root and
no reachable snapshot reference collection still references it.

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

Retention examples:

- After record commit, `S13` is the global after snapshot. `S10` remains if a
  global undo entry retains it. `S11` and `S12` are reclaimable once no session,
  consumer, or persistence root still names them.
- After `A → B → C`, undo to `B`, then commit `D`, the redo branch to `C` is
  cleared from active history. `C` remains readable only while a consumer,
  persistence, or other root still holds it. It is not a redo target.

### Undo and redo select snapshots and clear the redo branch

Undo and redo select existing complete snapshot identities. They do not replay
history and they do not persist a runtime undo stack.

```text
A → B → C
      ^
     undo
      v
      B
      |
      v
      D
```

Result:

```text
A → B → C     C is an abandoned branch, not a redo target
      \
       D      D is the new active tip
```

`C` is not rewritten. It becomes unreachable from the active branch. Redo is
empty after `D` publishes. Reclamation follows the reachability model above.

**Alternative:** keep abandoned branches as persisted redo history. Rejected
because this change does not persist undo/redo and does not grow disk history
for abandoned runtime branches.

### Persistence loads the latest saved snapshot only

The runtime model requires serialization of immutable objects and the latest
complete snapshot in a future format. Exact layout, reclamation implementation,
and old-format migration are separate decisions. Migration uses the existing
`StorageManager` deferred owner; any unit that executes between
`handleMidiInput()` entries still requires an admissible
`ExecutionBounds.md` classification.

Distinguish:

```text
runtime undo stack
    NOT persisted

persistent snapshot content
    latest saved complete snapshot only
```

The storage boundary is content-only: the future format stores immutable
objects, stable logical references, the selected snapshot root, loop metadata,
and the serialized latest complete snapshot. It does not store runtime
addresses, derived representations, playback cursor/order, active ledgers,
undo history, or abandoned-branch lineage sufficient to restore post-reboot
undo.

After load or unplanned reboot:

```text
load latest saved complete snapshot
runtime undo depth = 0
redo = empty
unsaved session tails and unresolved candidates are discarded
```

This change does not promise post-reboot undo. If a later storage design wants
persisted snapshot lineage, that is a separate decision.

`StorageManager` serializes and loads this content; `Loop` rebuilds local
handles and owns runtime snapshot selection.

Large-loop save and migration may take longer in total, but neither may become
a content-sized synchronous operation at record/overdub stop, inside
`handleMidiInput()`, during clock dispatch, or on a track-tick path. Existing
`StorageManager` deferred units carry progress and retain the active snapshot
and previously valid persisted state when a unit cannot fit, overruns, or fails.
The phase must define retention/retry/rejection behavior without silently
discarding pending content.

### Undo and playback select snapshots directly

Undo entries retain before/after snapshot identities while those entries remain
on the live runtime stack. Undo, redo, and clear-slot restore select complete
snapshots without replaying intervening history. Playback derives execution
order from a const active snapshot while retaining runtime cursor, ledger, and
horizon ownership.

**Alternative:** disable rows and replay to reconstruct. Rejected because it preserves history-proportional work.

## Risks / Trade-offs

- **[Memory overhead]** Immutable versions retain history → measure object/reference pools and admission before firmware.
- **[Construction cost]** Mutations may be content-dependent → classify the
  owner in `ExecutionBounds.md`, use bounded resumable units, and define an
  explicit overrun policy before runtime publication.
- **[Directory implementation]** Dirty bars do not automatically share the
  pitch/bar directory. Phase 1 / measurement must choose a physical directory
  that newly allocates only dirty membership and the minimum path to it.
- **[Projection cost]** A requested window does not cap the number of
  `generateEquivalentIntervals` candidates for a span crossing multiple loop
  lengths → bound the source span range and candidate count before runtime
  projection. That bound is not a snapshot-architecture unit.
- **[Parity]** Snapshot output may diverge → compare against current materialization for overlap, pairing, wrap, ordering, and non-note events.
- **[Lifetime]** Retained snapshots may leak or alias mutable data → test reachability, loop scope, and older-snapshot stability.
- **[Record-prefix pressure]** Record-prefix snapshot retention may create substantial temporary reference/object pressure during long recordings and must be measured against the named-root lifetime policy.
- **[Compatibility]** Storage changes may reject existing files → retain old readers until migration and round-trip gates pass.

## Migration Plan

1. Complete formal architecture reassessment and record approval before firmware work.
2. Measure and select the physical pitch/bar directory representation before Phase 2 firmware; the logical indexes are already selected.
3. Implement immutable objects, stable handles, shared reference storage, and invariant-protecting native checks with current consumers unchanged.
4. Define and verify candidate construction bounds, progress, and overrun behavior before runtime publication.
5. Adapt capture and NOTE_EDIT commit boundaries while retaining compatibility pass records; publication may occur only after a complete candidate is ready.
6. Migrate playback and other consumers independently, using snapshot identity and existing verification gates.
7. Migrate undo/load/persistence after physical format approval.
8. Remove replay/materialized compatibility paths only after all caller-specific gates pass.

Rollback keeps the prior representation active and discards any failed candidate. Existing storage readers remain until the new reader and migration are proven.

## Design-grilling outcomes — 2026-08-25

These outcomes sharpen the proposed Phase 0 model. They remain pending the formal
architecture reassessment and approval recorded in `ARCHITECTURE-REVIEW.md`.

### Complete content and LCR role

- A published `LoopSnapshot` is complete effective canonical content. Its
  logical content is completely determined by its own reference root. Resolving
  a reference or answering a content query never requires consulting a parent
  snapshot. Shared membership nodes are not a parent-walk.
- Existing LCR preparation mechanisms may be reused temporarily as
  implementation machinery where they produce data needed to construct a
  snapshot candidate. They do not own effective state, publication, or
  snapshot lifetime.

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

### Deferred source-level retrigger pairing

- Phase 0 retains the current `Track::recordMidiEvents()` and `pendingNotes`
  behavior as the first source guard. No new replace-on-retrigger guard is
  introduced by the immutable snapshot design.
- Current same-channel/same-pitch capture pairing remains the existing LIFO
  behavior. The source-level change that would append a synthetic NoteOff
  before replacing an open `pendingNotes` entry is deferred to a separate
  future bugfix.
- Existing stop finalization remains separate: `finalizePendingNotes()` and
  `LoopStopFinalize` already close notes that remain open at record/overdub
  stop. This deferred item concerns retrigger-time closure during live capture.
- The future retrigger change must define its canonical event, persistence,
  undo, and missing-physical-NoteOff behavior before implementation. It is not
  part of the current snapshot publication scope.

### Session publication boundaries

- NOTE_EDIT publishes after each complete `EditSessionActions` batch produced by
  `NoteGeometryResolver`. `NoteEditCurrentState` remains the mutable NOTE_EDIT
  buffer; `EditSession.store` remains its projected content state. No separate
  NOTE_EDIT action buffer is introduced. Batch snapshots are provisional `E:`
  revisions until the existing close/commit records one global undo unit.
- Overdub publishes after the existing wrap-level settled boundary:
  `Track::commitOverdubWrapAtSessionStart()` seals the overdub pass and companion
  changes before candidate publication. Raw incomplete note events are not
  published as snapshots. Wrap snapshots are provisional `O:` revisions until
  overdub stop records one global undo unit.
- Record uses the same direct-publication model for completed events so a latest
  complete snapshot is available for deferred persistence while recording is
  active. Those prefixes are provisional session revisions. Only the final
  record-stop transition is a global undo snapshot.
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
- Snapshot retention follows the named-root reachability model. Record releases
  older event-prefix roots once they are no longer named by a root.
- Pending candidate work normally belongs to the focused slot. Queued-slot
  preparation remains the normal opportunity for making the next complete
  snapshot ready before a switch.
- If a slot switch encounters pending work despite that preparation, switching
  uses the slot's latest complete snapshot without waiting. The newer snapshot
  is adopted at a later safe playback boundary; uninterrupted MIDI timing has
  priority over immediately exposing the newest content.

### Persistence and reboot

- `StorageManager` may persist the latest complete snapshot through its existing
  deferred units while a session is active. Open session state, unresolved
  candidate work, the runtime undo stack, and abandoned redo branches are not
  serialized.
- After an unplanned reboot, the latest saved complete snapshot is restored.
  Runtime undo depth is 0, redo is empty, and the unsaved session tail is
  discarded. Post-reboot undo is not a requirement of this change.

### Execution boundary

Post-exit candidate work resumes through existing
`Track::processDeferredIdleMaintenance()` opportunities after MIDI service.
Candidate construction is not introduced as an unbounded operation in
`handleMidiInput()`, clock dispatch, or `TrackManager::updateAllTracks()`.
The implementation phase must still classify the architectural construction
contract and any reused LCR candidate-construction machinery under
`ExecutionBounds.md`; preparation readiness does not replace the
indivisible-step bound. LCR machinery still does not own effective state.

### Timing evidence and measurement hypothesis

Existing runtime measurements provide a sizing reference, not a guaranteed
maintenance reservation, and not a snapshot-architecture unit:

- At 170 BPM, one MIDI clock interval is `14.71 ms` (`60 / (170 × 24)`).
- The runtime scheduling plan uses `8.82 ms` (`60%` of that interval) as an
  experimental operating target until `T_RT_reserve` is measured.
- The earlier `2 ms` target and `5 ms` ceiling are withdrawn as established
  scheduling contracts.
- Post-RC-K1–K3 overdub note-change accumulation measured `0.75–2.0 ms`
  normally, with a `3.6 ms` observed peak; this is not a measurement of
  immutable snapshot path-copy publication.
- Existing LCR preparation slices measured `63–121 ms` for index work and up to
  `115.7 ms` for reconstruction. These operations are not admissible references
  for snapshot successor construction.

Any numeric “N-candidate” construction quantum, including a previously discussed
eight-candidate LCR unit, is an implementation measurement hypothesis only. It
is not part of the snapshot architecture and must not become an implicit
architectural unit. Before runtime publication, measure the selected
representation’s successor-construction unit on device, including path-copy or
equivalent structural sharing, `NoteId` index updates if present, allocation
behavior, and wrap plus same-tick cases. If the measured unit does not fit the
declared bound, the candidate quantum or construction representation must change
before runtime publication is implemented.

## Post-stop MIDI stability

Device [`004934`](../../../captures/session_20260826_004934.log): overdub-stop
display hold is PASS (`ODUB,stop,display` 8.8 / 12.2 ms). PLAYING after first
publish is 7.4–8.4 ms. **Selected now:** `4b.0` — overdub wrap keeps the composed `DisplayNote`
vector; `ODUB,stop,display` holds it while `visualCache` is dirty
([`011829`](../../../captures/session_20260826_011829.log) 155.6 ms after wrap
cleared the frame).

Parked until selected:

- `4b.1` persist / first-record snapshot same-turn exclusion
- `4b.2` first-record `snapshot_idle` until the first publish
- `4b.3` post-overdub-stop PLAYING `usbdisp` / `load_frame` / `late_on`
- `4b.4` OVERDUBBING `DisplayResolveLiveCapture` (58 ms on `004934`)

## Open Questions

- Which physical directory representation implements the selected pitch → bar
  indexes with shared unchanged membership?
- What exact storage version/layout and compatibility migration are required?
- Which optional indexes are justified without becoming a second authority?
- What execution-bound class, unit cap, progress state, and overrun policy
  will each named candidate-construction operation use on each runtime path?
- Which existing owner holds an incomplete candidate and its continuation, and
  how is a stale candidate discarded if the active snapshot changes?
- Which phase and work identity authorize each consumer migration?
