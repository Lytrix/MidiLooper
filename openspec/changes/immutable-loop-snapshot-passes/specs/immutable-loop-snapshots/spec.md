## ADDED Requirements

### Requirement: LoopSnapshot is distinct from LoopPass

The system SHALL treat `LoopSnapshot` as the complete immutable effective-state object and `LoopPass` (`RecordPass`, `OverdubPass`, `EditPass`) as the historical/compatibility operation record. A commit MAY produce both a compatibility pass record and a `LoopSnapshot`. Those objects SHALL NOT be interchangeable.

#### Scenario: Commit produces distinct objects

- **WHEN** an overdub or note-edit commit is accepted during migration
- **THEN** the system may retain a compatibility `LoopPass` record
- **AND** it publishes a separate complete `LoopSnapshot`
- **AND** consumers read the `LoopSnapshot`, not the pass record, for effective state

#### Scenario: Terminology does not collapse the objects

- **WHEN** documentation or APIs name committed effective state
- **THEN** they use `LoopSnapshot`
- **AND** they do not call the snapshot a `LoopPass`

### Requirement: Complete immutable loop snapshot

Each published `LoopSnapshot` SHALL be a complete immutable view of effective musical state at its boundary. Snapshot content SHALL include note and non-note MIDI event objects through stable loop-scoped references. A snapshot's logical content SHALL be completely determined by its own reference root. Resolving a reference or answering a content query SHALL NOT require consulting a parent snapshot. Shared membership nodes SHALL NOT be implemented as a parent-snapshot walk or delta chain.

#### Scenario: Snapshot is self-contained

- **WHEN** a consumer reads an active or historical snapshot
- **THEN** the snapshot provides all musical content for its boundary
- **AND** resolving a reference or answering a content query does not consult a parent snapshot

#### Scenario: Successor is not a parent delta

- **WHEN** a successor snapshot shares unchanged membership nodes with its parent
- **THEN** the successor still exposes a complete logical directory
- **AND** it does not store a parent pointer used to recover missing content

#### Scenario: Non-note events remain available

- **WHEN** a loop contains CC, pitch bend, program, or system MIDI events
- **THEN** the selected snapshot retains those events
- **AND** selection does not reduce the state to notes only

### Requirement: Pitch-and-bar logical indexes with shared membership

A `LoopSnapshot` SHALL represent notes through a span index keyed by pitch then by canonical 768-tick bar. Each bar SHALL hold a membership list of `NoteRef`s for complete immutable notes whose geometry intersects that bar. A `NoteRef` MAY occur in multiple bar lists in the same snapshot; each occurrence SHALL identify the same immutable note version and `NoteId`. Overlap candidate sets SHALL deduplicate by `NoteId`.

A `LoopSnapshot` SHALL represent note and non-note MIDI events through an event index keyed by the same 1-bar granule. Each bar SHALL hold a membership list of `EventRef`s. Canonical event ordering SHALL remain the existing event-stream semantics; the bar list SHALL be an indexed view in that order and SHALL NOT be a second ordering authority. Non-note events are points and SHALL NOT appear in the span index.

The span index SHALL store complete semantic notes only. It SHALL NOT store unmatched open Ons. Occupy / open-at-S SHALL remain a consumer (`ActiveNoteLedger` / `presentAt`), not a snapshot index.

Creating a successor SHALL allocate new immutable event objects only for added or modified content and SHALL NOT copy or mutate unchanged event objects. Only dirty pitch/bar membership nodes and the minimum immutable directory structure required to reach them MAY be newly constructed. Unchanged pitch/bar membership SHALL remain shared with the parent snapshot.

The logical model is selected. Phase 1 selected a sparse fixed-capacity paged
directory as the native implementation experiment; page capacity and runtime
execution classification remain open gates. Path-copy SHALL NOT be used as the
semantic overlap model. A flat full-loop `NoteRef` vector SHALL NOT be the
overlap or candidate structure. The Phase 1 implementation does not authorize
firmware publication or runtime use of the physical directory.

#### Scenario: Unchanged objects keep the same references

- **WHEN** a successor snapshot replaces one note and leaves the remaining notes unchanged
- **THEN** the new snapshot reuses the unchanged event-object references
- **AND** it allocates a new immutable object only for the changed note
- **AND** the older snapshot still references the original object

#### Scenario: Dirty bars share unchanged membership

- **WHEN** a successor changes notes that affect only some pitch/bar lists
- **THEN** construction does not copy unchanged pitch/bar membership
- **AND** only dirty membership lists and the minimum directory path to those lists are newly allocated
- **AND** a flat copy of every snapshot reference is not an admissible representation

#### Scenario: Candidate selection is not object mutation

- **WHEN** incoming note B (same pitch) intersects bars that also contain unchanged note A and changed note C
- **THEN** the candidate set includes A and C after `NoteId` dedup
- **AND** A remains the same immutable object if the resolver does not change it
- **AND** only C and B cause new object allocation when the resolver replaces C and adds B

#### Scenario: Logical structure is selected; physical directory is not

- **WHEN** Phase 0 records the reference-structure decision
- **THEN** the logical indexes are pitch → bar → NoteRef and bar → EventRef
- **AND** no firmware phase treats a concrete directory structure as already selected

### Requirement: Dirty region follows resolver geometry changes

Dirty pitch SHALL be the incoming note's pitch. Dirty bars SHALL be the union of bars intersected by the incoming span, every existing note whose geometry changes, and every existing note whose membership changes because of resolution. The existing overlap / pairing / NOTE_EDIT geometry owner SHALL determine which note geometries changed and SHALL supply those changed `NoteRef`s and affected spans. Snapshot construction SHALL translate that mutation into pitch/bar membership updates and SHALL NOT independently recompute overlap.

#### Scenario: Resolver-moved geometry dirties bars outside the incoming span

- **WHEN** resolution changes existing note A to A' and A' intersects bars that the incoming note does not
- **THEN** those additional bars are dirty
- **AND** snapshot construction uses the resolver-supplied affected spans

### Requirement: Wrap-crossing notes index both linear segments

A wrap-crossing note SHALL keep the existing two linear segments (`[start, loopEnd)` and `[0, end)`). Each segment SHALL contribute `NoteRef` membership in the bars it intersects. Both membership sets SHALL identify the same `NoteId`. A candidate query near the loop boundary SHALL union end-side bars and zero-side bars, then deduplicate by `NoteId`. Wrap notes SHALL NOT be a special index type.

#### Scenario: Wrap note is one identity in both regions

- **WHEN** a wrap-crossing note occupies bars near loop end and bars near zero
- **THEN** those bar lists all reference the same `NoteId`
- **AND** a boundary candidate query returns that note once after dedup

### Requirement: Immutable note and event versions

Note and non-note event objects SHALL be immutable after construction. A logical `NoteId` SHALL remain stable across an edit that creates a new object version.

#### Scenario: Editing preserves older versions

- **WHEN** a note is shortened, moved, pitched, or otherwise edited
- **THEN** the new snapshot references a new immutable object version with the same `NoteId`
- **AND** the older snapshot remains semantically unchanged

#### Scenario: Deleting removes only the new reference

- **WHEN** a logical note is deleted
- **THEN** the new snapshot does not reference that note
- **AND** older snapshots that referenced it remain unchanged

### Requirement: Atomic snapshot construction and publication

Snapshot publication SHALL occur at an existing capture or edit commit boundary after capture finalization. Candidate construction SHALL read the active snapshot as const and apply a semantic mutation supplied by the existing owner. Construction MAY complete before publication through bounded resumable units, but the complete candidate SHALL be frozen and published atomically. Failure SHALL leave active and historical snapshots unchanged.

#### Scenario: Capture commit publishes one global candidate

- **WHEN** a record or overdub session is finalized and its mutation is accepted
- **THEN** one complete candidate snapshot is published as the global after snapshot at the existing commit boundary
- **AND** an overdub session with multiple wraps does not create a global undo snapshot per wrap

#### Scenario: Failed candidate preserves active state

- **WHEN** object or reference admission fails
- **THEN** the candidate is discarded
- **AND** the previous complete snapshot remains active
- **AND** no partial collection is visible

### Requirement: Candidate construction operations have declared bounds

Candidate construction SHALL follow this architectural contract and SHALL declare a complexity and `ExecutionBounds.md` class for each step before runtime publication: identify affected content, apply immutable object replacements, construct the successor reference root, freeze the candidate, and atomically publish. Identify/apply/construct have semantic locality bounded by the affected pitch and dirty bars. Their execution cost remains dependent on intersecting memberships and the selected physical directory and therefore requires measurement before runtime publication. Path depth SHALL NOT be treated as an execution bound. A long note MAY still intersect many bars; this requirement does not by itself make construction bounded. A local timer checked only between steps SHALL NOT qualify as an execution bound. Phase 1 MAY decompose the contract into measured operations; those decompositions are not additional architectural owners.

#### Scenario: Named operations are classified before firmware publication

- **WHEN** a later phase introduces runtime candidate construction
- **THEN** each named operation has a declared complexity and bound class
- **AND** publication does not hide an unclassified O(N) reference-set copy

### Requirement: Existing owners supply semantic mutations

Snapshot construction SHALL apply mutations determined by existing capture finalization, overlap, note-pairing, and NOTE_EDIT geometry owners. Snapshot storage SHALL NOT become a second semantic owner.

#### Scenario: Snapshot storage does not resolve overlap

- **WHEN** an existing owner supplies affected notes and replacement geometries
- **THEN** snapshot construction applies those supplied changes
- **AND** it does not independently recompute overlap or pairing

#### Scenario: NOTE_EDIT publishes at its existing boundary

- **WHEN** a NOTE_EDIT batch reaches its existing close or commit boundary
- **THEN** the mutable `NoteEditSession.store` supplies the settled mutation
- **AND** one complete snapshot is published for the closed batch

### Requirement: Loop owns snapshot publication and lifetime

`Loop` SHALL own active selection, candidate publication, and immutable object allocation. `LoopPasses` SHALL own snapshot records and persistent reference roots, and SHALL retain compatibility `LoopPass` rows during migration. Snapshot lifetime SHALL follow the named-root reachability model.

#### Scenario: References are loop-scoped

- **WHEN** a reference is inserted into a loop
- **THEN** its handle belongs to that loop identity
- **AND** a reference from another `LoopId` is not inserted without cloning the object

#### Scenario: Retained snapshot remains readable

- **WHEN** an undo entry or consumer staging object retains a historical snapshot
- **THEN** its immutable objects remain readable
- **AND** reclamation waits until the retaining root is released

### Requirement: Snapshot reachability and reclamation

A snapshot or immutable event object SHALL be reclaimable only when no named root and no reachable snapshot reference collection still references it. Named roots SHALL include the active snapshot, global undo before/after roots on the live branch, the session anchor, the session current provisional snapshot, E:/O: session roots, persistence staging, consumer staging, and the pending candidate parent.

#### Scenario: Unnamed prefix snapshots become reclaimable

- **WHEN** a record session publishes provisional prefixes and then commits the final snapshot
- **THEN** the session anchor remains if a global undo entry retains it
- **AND** intermediate prefixes are reclaimable once no named root still references them

#### Scenario: Abandoned redo snapshots wait for root release

- **WHEN** undo leaves snapshot C off the active branch and a later commit publishes D
- **THEN** C is not a redo target
- **AND** C remains readable only while a remaining named root still references it
- **AND** C is reclaimable after every such root is released

### Requirement: Provisional session snapshots versus global undo snapshots

Intermediate complete prefixes during an open record, overdub, or NOTE_EDIT session SHALL be playable provisional revisions. They SHALL NOT become separate global undo entries. Only the existing session-exit commit boundary SHALL record the global before/after snapshot transition.

#### Scenario: Record prefixes stay provisional

- **WHEN** recording publishes completed-event prefixes for playback or deferred persistence
- **THEN** those prefixes are session/provisional snapshots
- **AND** only the final record-stop transition enters global undo

#### Scenario: Overdub wraps stay session-scoped

- **WHEN** an overdub session publishes wrap-level snapshots
- **THEN** those snapshots are provisional `O:` revisions
- **AND** one `OverdubPassAdded` global undo unit is recorded at overdub stop

#### Scenario: NOTE_EDIT batches stay session-scoped until close

- **WHEN** NOTE_EDIT publishes after a complete `EditSessionActions` batch
- **THEN** that snapshot is a provisional `E:` revision
- **AND** one `NoteEditPassClosed` global undo unit is recorded at the existing close/commit boundary

### Requirement: Undo/redo branch selection

Undo and redo SHALL select existing complete snapshot identities. After undo from C to B and a new committed operation D, C SHALL be an abandoned branch and SHALL NOT remain a redo target. Selecting a snapshot SHALL NOT reconstruct content by replaying intervening history. Snapshot identity SHALL be the consumer freshness key.

#### Scenario: Undo selects a prior snapshot

- **WHEN** global undo targets a committed operation
- **THEN** the before-snapshot identity becomes active
- **AND** historical replay is not required

#### Scenario: Redo selects the forward snapshot

- **WHEN** redo is available on the live branch
- **THEN** the after-snapshot identity becomes active
- **AND** it is complete before consumers observe it

#### Scenario: New work after undo clears redo

- **WHEN** the live branch is A → B → C, undo selects B, and D is published
- **THEN** D becomes the active tip
- **AND** C is not reachable as redo
- **AND** C is not mutated

#### Scenario: Clear-slot restore is isolated

- **WHEN** `ClearSlot` restores a retained before snapshot
- **THEN** the active state selects that snapshot directly
- **AND** later mutations cannot modify its immutable content

### Requirement: Direct snapshot selection for runtime state

Undo, redo, clear-slot restore, and active-state changes SHALL select existing complete snapshot identities. Selecting a snapshot SHALL NOT reconstruct content by replaying intervening history. Snapshot identity SHALL be the consumer freshness key.

#### Scenario: Session undo selects a provisional identity

- **WHEN** E: or O: undo targets the previous session revision
- **THEN** the previous provisional snapshot identity becomes selected
- **AND** the session owner restores transient state separately from the snapshot

### Requirement: Playback separates snapshots from runtime state

Playback consumers SHALL read a const active snapshot and derive execution range and order from its musical content. `Track` and `LoopPlaybackRuntime` SHALL retain cursor, execution order, active ledger, and horizon ownership. Snapshot adoption SHALL NOT add unbounded work between `handleMidiInput()` entries.

#### Scenario: Playback prepares from the selected snapshot

- **WHEN** playback prepares execution state
- **THEN** it checks and records the selected snapshot identity
- **AND** it does not derive normal playback content by replaying history

#### Scenario: Snapshot changes invalidate stale execution state

- **WHEN** commit, undo, redo, load, or clear selects another snapshot
- **THEN** stale preparation is rejected or invalidated by identity
- **AND** the clock-facing consumer retains bounded fallback behavior

### Requirement: Snapshot projection remains consumer-bounded

The system SHALL keep projection context, projected intervals, and consumer
windows out of `LoopSnapshot`. Playback, display, and NOTE_EDIT consumers SHALL
select the relevant canonical span range from the selected snapshot before
calling `IntervalProjection`. A complete snapshot SHALL NOT authorize
full-loop projection on a timing-critical path. A projection-candidate count
SHALL NOT become a `LoopSnapshot` architectural unit.

#### Scenario: Projection uses consumer context

- **WHEN** a consumer projects content from an active snapshot
- **THEN** the consumer supplies its own `ProjectionContext`
- **AND** the snapshot remains unchanged when playhead, viewport, loop origin, queued start, or selected tick changes

#### Scenario: Large-loop playback and display use bounded source ranges

- **WHEN** playback or windowed display consumes a large loop
- **THEN** it selects a bounded canonical span range before projection
- **AND** playback uses the scalar `playbackEventPhase` path without allocating projection candidates
- **AND** the consumer does not project the complete snapshot as one timing-critical operation

#### Scenario: Long-span candidate generation has an execution bound

- **WHEN** `generateEquivalentIntervals` handles a canonical span that crosses multiple loop lengths
- **THEN** the runtime path applies a structural candidate cap or an empirically bounded overrun guard and failure policy
- **AND** a projection window alone is not treated as a fixed execution bound
- **AND** that cap is not treated as part of the snapshot object model

#### Scenario: Full-loop projection is explicit

- **WHEN** a display or edit operation intentionally requests full-loop projection
- **THEN** it runs outside timing-critical playback and input paths
- **AND** its work unit and storage are classified under `ExecutionBounds.md`

### Requirement: Snapshot migration preserves runtime layer boundaries

The system SHALL keep authoritative snapshot content, derived
representations, interval projection, and runtime requests as separate
contracts. `LoopSnapshot` SHALL provide immutable content and identity only;
consumer-specific representation owners SHALL retain ownership of playback
state, display windows, analyze indexes, LED queries, and projection context.
`StorageManager` SHALL retain ownership of deferred persistence.

#### Scenario: Consumer migration changes source, not ownership

- **WHEN** a runtime consumer migrates from compatibility pass data to a selected `LoopSnapshot`
- **THEN** it changes its content source and freshness identity
- **AND** it retains the existing owner and policy for its derived representation or query
- **AND** it selects its bounded interval or canonical span before projection

#### Scenario: Snapshot does not absorb consumer context

- **WHEN** playhead, viewport, loop origin, queued start, or selected tick changes
- **THEN** the selected snapshot remains unchanged
- **AND** the consumer updates its request or projection context without mutating snapshot content

### Requirement: Snapshot-backed derived representations retain owner policies

The system SHALL use the selected `LoopSnapshot` as the source for normal
derived representations. Existing representation owners SHALL retain
ownership of their representation storage, source and representation revisions,
invalidation, bounded build policy, and consumer-specific freshness behavior.
`LoopSnapshot` SHALL NOT own `mergedEvents`, `visualCache`, playback order,
analyze indexes, or LED queries.

#### Scenario: Snapshot identity is the source revision

- **WHEN** a new snapshot is published or another snapshot is selected
- **THEN** derived representations built from the previous snapshot are identified as stale by snapshot identity
- **AND** each representation owner decides when and how its bounded rebuild occurs

#### Scenario: Pending representation rebuild does not block timing

- **WHEN** a large-loop representation is stale and its rebuild is pending
- **THEN** the consumer follows its existing stale, fallback, or deferred-freshness policy
- **AND** it does not synchronously rebuild the complete snapshot on a timing-critical path

#### Scenario: Compatibility sources are transitional

- **WHEN** a consumer has not completed its snapshot migration gate
- **THEN** compatibility `LoopPass` records or materialized data may remain as a parity or fallback source
- **AND** it is not introduced as a second normal effective-state authority

### Requirement: LoopContentResolution is a transitional stateless adapter

During migration, `LoopContentResolution` SHALL provide stateless queries over one explicit complete snapshot. It SHALL NOT maintain a second historical effective-state representation, traverse parents for content, or make replay the normal consumer path. Its maintained-history state SHALL be removed only after caller migration gates pass.

#### Scenario: Query uses one explicit snapshot

- **WHEN** a consumer requests a window or state
- **THEN** the query reads the supplied snapshot and disposable indexes
- **AND** it does not apply historical captures or edit rows

#### Scenario: Retirement waits for migration

- **WHEN** all affected consumers pass their migration gates
- **THEN** maintained LCR replay and old materialization dependencies may be removed
- **AND** compatibility fallback is not removed before those gates pass

### Requirement: Snapshot construction has an execution-bound policy

The system SHALL apply an execution-bound policy to any candidate construction
that executes from `handleMidiInput()`, input-dispatched actions, clock
dispatch, or a track-tick path. Such construction SHALL have a declared row in
the maintained
[ExecutionBounds.md](../../../../docs/Authority/Architecture/ExecutionBounds.md)
inventory before runtime implementation. Large construction SHALL be divided
into units with an enforced input and storage cap, a maximum indivisible step,
persistent progress, and a completion condition. An empirically bounded unit
SHALL additionally have an explicit overrun guard and failure policy. A local
timer checked only between steps SHALL NOT qualify as an execution bound.

#### Scenario: Existing commit boundaries do not bypass bound review

- **WHEN** a capture or edit commit is reached from input dispatch, clock dispatch, or a track-tick path
- **THEN** candidate construction is classified as timing-critical or interval-path work
- **AND** the existing commit boundary does not make content-sized construction admissible

#### Scenario: Large construction progresses in bounded units

- **WHEN** a complete candidate requires more work than one admissible unit
- **THEN** the construction resumes from persistent progress across work opportunities
- **AND** one unit's work and storage do not grow with total loop length
- **AND** the active snapshot remains selected until the candidate is complete

#### Scenario: Publication is the atomic completion step

- **WHEN** all bounded construction units complete
- **THEN** one complete candidate is frozen and published atomically
- **AND** publication does not perform the deferred content-sized construction

#### Scenario: Empirical overrun preserves state

- **WHEN** an empirically bounded unit reaches its overrun guard or cannot fit
- **THEN** no partial candidate is published
- **AND** the active and historical snapshots remain unchanged
- **AND** the phase-specific retry, defer, or rejection policy retains the settled mutation without silent loss

#### Scenario: Unbounded construction is not admitted

- **WHEN** construction is content-dependent without an enforced bound
- **THEN** it is not introduced on the interval path or timing-critical input path
- **AND** a separate approved phase defines the bound, progress, and overrun policy before runtime publication
