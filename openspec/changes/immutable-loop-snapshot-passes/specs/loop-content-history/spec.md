## MODIFIED Requirements

### Requirement: Loop persists content records only

The Loop file SHALL persist the ordered immutable content required for complete snapshot state. During compatibility migration this includes existing `RecordPass`, `OverdubPass`, `EditPass`, and `LoopGeometry` records. The new storage version SHALL serialize immutable note and non-note event objects, snapshot/reference structure, and the serialized active branch tip. The system SHALL NOT persist an undo stack, history cursor, history transition, or undo/redo record. NOTE_EDIT session undo (`E:`) SHALL remain pre-commit and outside this persist model.

#### Scenario: Stop does not write undo history

- **WHEN** record or overdub stop admits persistence
- **THEN** admitted work includes loop content and the selected snapshot
- **AND** admitted work does not include `LoopUndoHistory`

#### Scenario: Serialized snapshot defines effective state

- **WHEN** a current-format serialized snapshot branch is loaded
- **THEN** the reconstructed Loop selects the serialized complete snapshot directly
- **AND** normal loading does not replay intervening records to derive active state

### Requirement: Snapshot persistence retains the StorageManager boundary

The system SHALL keep snapshot serialization, deferred save work, loading, and
compatibility migration owned by `StorageManager`. The storage format SHALL
persist immutable content, stable logical references, snapshot roots, loop
metadata, and the serialized active branch tip. It SHALL NOT persist runtime
addresses, derived representations, playback cursor/order, active ledgers, or
undo history.

#### Scenario: Storage does not absorb runtime representation ownership

- **WHEN** a snapshot is saved or loaded
- **THEN** `StorageManager` serializes or reconstructs snapshot content
- **AND** `Loop` rebuilds loop-local handles and selects the runtime snapshot
- **AND** derived representation owners remain responsible for their own rebuilds

### Requirement: Large-loop persistence is deferred and bounded

Large-loop save and compatibility migration SHALL use existing `StorageManager`
deferred units. They SHALL NOT perform content-sized synchronous serialization
or migration at record/overdub stop, inside `handleMidiInput()`, during clock
dispatch, or on a track-tick path. Any unit that executes between
`handleMidiInput()` entries SHALL have a declared `ExecutionBounds.md` class,
input and storage cap, maximum indivisible step, persistent progress, and
overrun/failure policy.

#### Scenario: Stop admits persistence without catch-up

- **WHEN** record or overdub stop admits a large snapshot for persistence
- **THEN** the stop path records deferred work with `StorageManager`
- **AND** it does not synchronously serialize or migrate the complete snapshot
- **AND** the active snapshot remains available to runtime consumers

#### Scenario: Persistence unit failure preserves state

- **WHEN** a save or migration unit cannot fit, overruns, or fails allocation
- **THEN** no partial snapshot is published
- **AND** the active snapshot and previously valid persisted state remain intact
- **AND** pending work follows the phase-specific retention, retry, or rejection policy without silent loss

### Requirement: Load reconstructs editing state from content

After loading a Loop from disk, the runtime SHALL identify the serialized snapshot tip, reconstruct loop-local stable handles, derive supported runtime undo units, and start with redo empty. An older supported format MAY be migrated through the existing `StorageManager` deferred owner by replaying its records into complete snapshot boundaries. Any migration unit that executes between `handleMidiInput()` entries SHALL satisfy the declared bound and overrun policy in [ExecutionBounds.md](../../../../docs/Authority/Architecture/ExecutionBounds.md). Normal undo and redo SHALL select existing snapshots rather than replay history.

#### Scenario: Reboot at the tip

- **WHEN** a Loop is saved at the content tip and loaded after reboot
- **THEN** the active state is the serialized complete snapshot at that tip
- **AND** display undo depth equals the derived undo-step count
- **AND** redo is empty
- **AND** undo can select prior retained snapshots

#### Scenario: Grouped records are one undo unit

- **WHEN** one user operation produced multiple content records
- **THEN** load derives those records as one undo unit
- **AND** one undo step selects the complete snapshot before that unit

#### Scenario: New work after undo is not required at load

- **WHEN** a Loop is loaded
- **THEN** the runtime does not restore a mid-session editor cursor
- **AND** the reconstructed position is the serialized content tip
- **AND** redo is empty until new runtime work creates a redo branch

## ADDED Requirements

### Requirement: Snapshot references are not serialized addresses

The persistence format SHALL serialize immutable object content and stable logical reference information, never raw C++ pointer addresses. Loading SHALL create loop-local runtime handles for the selected snapshot and its retained content.

#### Scenario: Save and load rebuild local handles

- **WHEN** a loop with immutable snapshot references is saved and loaded
- **THEN** the file contains no runtime pointer addresses
- **AND** the loaded Loop reconstructs handles for its loaded immutable objects
- **AND** selected snapshot content remains semantically equivalent
