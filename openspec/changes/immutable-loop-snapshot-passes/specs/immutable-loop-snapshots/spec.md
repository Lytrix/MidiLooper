## ADDED Requirements

### Requirement: Complete immutable loop snapshot

Each committed `LoopPass` snapshot SHALL be a complete immutable view of effective musical state at its boundary. Snapshot content SHALL include note and non-note MIDI event objects through stable loop-scoped references. A consumer SHALL NOT traverse a parent snapshot to obtain content.

#### Scenario: Snapshot is self-contained

- **WHEN** a consumer reads an active or historical snapshot
- **THEN** the snapshot provides all musical content for its boundary
- **AND** the consumer does not read a parent snapshot for missing content

#### Scenario: Non-note events remain available

- **WHEN** a loop contains CC, pitch bend, program, or system MIDI events
- **THEN** the selected snapshot retains those events
- **AND** selection does not reduce the state to notes only

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

#### Scenario: Capture commit publishes one candidate

- **WHEN** a capture pass is finalized and its mutation is accepted
- **THEN** one complete candidate snapshot is published at the existing commit boundary
- **AND** an overdub session with multiple wraps does not create an undo snapshot per wrap

#### Scenario: Failed candidate preserves active state

- **WHEN** object or reference admission fails
- **THEN** the candidate is discarded
- **AND** the previous complete snapshot remains active
- **AND** no partial collection is visible

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

`Loop` SHALL own active selection, candidate publication, and immutable object allocation. `LoopPasses` SHALL own snapshot records and persistent reference roots. Snapshot lifetime SHALL remain valid while reachable from active state, retained history, undo/redo, clear-slot restore, deferred persistence staging, or consumer staging.

#### Scenario: References are loop-scoped

- **WHEN** a reference is inserted into a loop
- **THEN** its handle belongs to that loop identity
- **AND** a reference from another `LoopId` is not inserted without cloning the object

#### Scenario: Retained snapshot remains readable

- **WHEN** an undo entry or consumer staging object retains a historical snapshot
- **THEN** its immutable objects remain readable
- **AND** reclamation waits until the retaining root is released

### Requirement: Direct snapshot selection for runtime state

Undo, redo, clear-slot restore, and active-state changes SHALL select existing complete snapshot identities. Selecting a snapshot SHALL NOT reconstruct content by replaying intervening history. Snapshot identity SHALL be the consumer freshness key.

#### Scenario: Undo selects a prior snapshot

- **WHEN** global undo targets a committed operation
- **THEN** the before-snapshot identity becomes active
- **AND** historical replay is not required

#### Scenario: Redo selects the forward snapshot

- **WHEN** redo is available
- **THEN** the after-snapshot identity becomes active
- **AND** it is complete before consumers observe it

#### Scenario: Clear-slot restore is isolated

- **WHEN** `ClearSlot` restores a retained before snapshot
- **THEN** the active state selects that snapshot directly
- **AND** later mutations cannot modify its immutable content

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
full-loop projection on a timing-critical path.

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
- **THEN** compatibility `LoopPasses` or materialized data may remain as a parity or fallback source
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
