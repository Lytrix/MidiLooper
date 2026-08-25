## MODIFIED Requirements

### Requirement: Loop passes owner

The system SHALL store capture and note-edit history on a loop through `passes[]` (`LoopPasses`). During migration, `LoopPasses` SHALL retain compatibility `LoopPass` records for existing capture and edit boundaries and SHALL also store complete immutable `LoopSnapshot` records that define committed musical state. A `LoopPass` SHALL be the historical/compatibility operation record. A `LoopSnapshot` SHALL be the immutable effective-state root indexed as pitch → bar → membership lists of `NoteRef`s and bar → membership lists of `EventRef`s. Its logical content SHALL be completely determined by its own reference root. Those objects SHALL NOT be interchangeable. A pass SHALL be pending only while it is not yet in `passes[]`. The system SHALL NOT use `committed` as a pass state label, container name, or pass undo kind suffix.

#### Scenario: Loop exposes compatibility passes and snapshots

- **WHEN** firmware loads or commits a loop with capture and edit history
- **THEN** `Loop` accesses compatibility history through `passes`
- **AND** the loop exposes the complete immutable `LoopSnapshot` selected for runtime state
- **AND** there is no `takes[]` / `Take` type in product code

#### Scenario: A commit may produce both objects

- **WHEN** an overdub or note-edit commit is accepted during migration
- **THEN** `LoopPasses` may retain a compatibility `OverdubPass` or `EditPass`
- **AND** it stores a separate published `LoopSnapshot`
- **AND** effective-state consumers select the snapshot

#### Scenario: Committed presence is not a pass state

- **WHEN** a loop has rows in `passes[]` after Commit
- **THEN** committed-pass presence APIs may report true
- **AND** pass rows retain existing Active/Disabled state labels
- **AND** there is no `PassState::Committed` requirement

### Requirement: Two-phase pass materialize (phase 1)

`LoopPasses::materialize` SHALL remain available as a compatibility and migration-oracle operation while snapshot migration is incomplete. Normal effective-state reads SHALL select the complete active immutable `LoopSnapshot` directly and SHALL NOT replay historical capture and edit records. Explicit compatibility materialization SHALL preserve the existing two phases: merge Active `recordPass` and `overdubPasses` by `mergeSequence`, then apply Active scoped `editPass` entries in storage order by `EditPassType`.

#### Scenario: Normal playback reads the active snapshot

- **WHEN** playback requests effective loop state after snapshot migration
- **THEN** the active complete immutable `LoopSnapshot` is selected directly
- **AND** `LoopPasses::materialize` is not the normal effective-state path

#### Scenario: Compatibility materialize remains an oracle

- **WHEN** a migration or parity test explicitly requests materialization
- **THEN** capture rows merge by `mergeSequence`
- **AND** edit rows apply in storage order by `EditPassType`
- **AND** the result remains available for comparison with snapshot content

#### Scenario: Materialize preserves edit dispatch

- **WHEN** `editPasses[]` contains Active Note and ControlChange rows
- **THEN** compatibility materialization applies each through its existing handler
- **AND** capture rows remain chunk-backed during migration

### Requirement: Pass-scoped global undo kinds without committed suffix

Global undo SHALL retain `RecordPassAdded`, `OverdubPassAdded`, `NoteEditPassClosed`, and `ControlChangeEditPassClosed` as operation labels. After snapshot migration, each SHALL record the before and after complete `LoopSnapshot` identities for the session-exit transition. Undo and redo SHALL select those snapshots and SHALL NOT reconstruct the target by replaying history or append Create/Delete edit rows for undo. Provisional session prefixes SHALL NOT receive separate global undo entries.

#### Scenario: Overdub stop records a snapshot transition

- **WHEN** overdub stops and capture is committed
- **THEN** `OverdubPassAdded` records before and after snapshot identities
- **AND** one complete after-snapshot is selected
- **AND** wrap-level provisional snapshots are not additional global undo entries

#### Scenario: Note edit close records a snapshot transition

- **WHEN** note edit exits and `closeNoteEditPass` runs
- **THEN** `NoteEditPassClosed` records complete before and after snapshots
- **AND** one snapshot transition represents the closed batch

#### Scenario: Undo selects a snapshot

- **WHEN** global undo applies a pass-scoped operation
- **THEN** the referenced before snapshot becomes active
- **AND** no Create or Delete row is appended to reconstruct the state

#### Scenario: New work after undo clears redo

- **WHEN** undo has selected snapshot B and a later commit publishes snapshot D
- **THEN** the abandoned after-snapshot is not a redo target
- **AND** D becomes the live-branch tip

#### Scenario: Compatibility rows remain isolated

- **WHEN** snapshot migration is enabled but compatibility rows remain
- **THEN** their existing Active/Disabled behavior remains available for compatibility
- **AND** normal effective-state consumers use the selected immutable snapshot
