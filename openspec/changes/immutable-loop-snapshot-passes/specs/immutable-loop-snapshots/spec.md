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

Snapshot construction SHALL occur at an existing capture or edit commit boundary after capture finalization. The builder SHALL read the active snapshot as const, apply a semantic mutation supplied by the existing owner, freeze a complete candidate, and publish it atomically. Failure SHALL leave active and historical snapshots unchanged.

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

### Requirement: Snapshot construction has an off-clock policy

Snapshot construction SHALL remain at an existing off-clock commit boundary unless a later approved phase defines a bounded unit and overrun policy. It SHALL NOT introduce a new scheduler, interval-path owner, or content-dependent unbounded operation.

#### Scenario: Commit construction stays off the MIDI interval path

- **WHEN** a capture or edit commit constructs a candidate
- **THEN** construction runs at the existing commit boundary
- **AND** it adds no scan, allocation, sort, or drain work between `handleMidiInput()` entries

#### Scenario: Unbounded construction is not admitted silently

- **WHEN** construction is content-dependent without an enforced bound
- **THEN** it is not moved onto the interval path
- **AND** a separate architecture phase defines the bound and overrun policy
