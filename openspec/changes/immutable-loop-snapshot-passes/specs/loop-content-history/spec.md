## MODIFIED Requirements

### Requirement: Loop persists content records only

The Loop file SHALL persist the ordered immutable content required for the latest complete snapshot state. During compatibility migration this includes existing `RecordPass`, `OverdubPass`, `EditPass`, and `LoopGeometry` records. The new storage version SHALL serialize immutable note and non-note event objects, logical `NoteRef` / `EventRef` membership, and the latest saved complete snapshot. The system SHALL NOT persist an undo stack, history cursor, history transition, undo/redo record, or abandoned-branch lineage sufficient to restore post-reboot undo. NOTE_EDIT session undo (`E:`) SHALL remain pre-commit and outside this persist model.

#### Scenario: Stop does not write undo history

- **WHEN** record or overdub stop admits persistence
- **THEN** admitted work includes loop content and the selected snapshot
- **AND** admitted work does not include `LoopUndoHistory`

#### Scenario: Serialized snapshot defines effective state

- **WHEN** a current-format serialized snapshot is loaded
- **THEN** the reconstructed Loop selects the serialized complete snapshot directly
- **AND** normal loading does not replay intervening records to derive active state

#### Scenario: Content prefix defines effective Loop

- **WHEN** a persisted content prefix is replayed
- **THEN** the reconstructed Loop matches the effective notes for that prefix
- **AND** any information required to define that state is content metadata on the records, not a persisted undo payload



### Requirement: Load reconstructs editing state from content

After loading a Loop from disk, the runtime SHALL identify the latest saved complete snapshot, reconstruct loop-local stable handles, and derive runtime undo units from saved active pass records with redo empty (DEC-088 — load-time undo reconstruction). An older supported format MAY be migrated through the existing `StorageManager` deferred owner by replaying its records into complete snapshot boundaries. Any migration unit that executes between `handleMidiInput()` entries SHALL satisfy the declared bound and overrun policy in [ExecutionBounds.md](../../../../docs/Authority/Architecture/ExecutionBounds.md). Runtime Undo SHALL make its action available for Redo; unrelated background slot loads SHALL preserve that redo. Saved snapshot authority is unchanged; undo/redo uses the existing root-selection and committed-pass publication owners.

#### Scenario: Reboot at the tip

- **WHEN** a Loop is saved and loaded after reboot
- **THEN** the active state is the latest saved complete snapshot
- **AND** runtime undo depth equals the units derived from saved active pass records
- **AND** redo is empty
- **AND** post-reboot undo is not restored from persisted lineage

#### Scenario: Unsaved session tail is discarded

- **WHEN** an unplanned reboot occurs during an open session
- **THEN** the latest saved complete snapshot is restored
- **AND** unsaved provisional prefixes and unresolved candidates are discarded

#### Scenario: Grouped records reconstruct one undo unit

- **WHEN** one user operation produced multiple content records before reboot
- **THEN** load reconstructs one runtime undo unit using saved grouping metadata
- **AND** later runtime work may create new undo units from new commits

#### Scenario: New work after undo is not required at load

- **WHEN** a Loop is loaded
- **THEN** the runtime does not restore a mid-session editor cursor
- **AND** the reconstructed position is the latest saved complete snapshot
- **AND** redo is empty until new runtime work creates a redo branch

#### Scenario: Grouped records are one undo unit

- **WHEN** one user operation produced multiple content records (companions, `noteEditPassIndex` batch, `overdubSessionIndex` wraps, or equivalent content metadata)
- **THEN** load derives those records as one undo unit
- **AND** one undo step disables or excludes that whole unit

## ADDED Requirements

### Requirement: Snapshot references are not serialized addresses

The persistence format SHALL serialize immutable object content and stable logical reference information, never raw C++ pointer addresses. Loading SHALL create loop-local runtime handles for the selected snapshot and its retained content.

#### Scenario: Save and load rebuild local handles

- **WHEN** a loop with immutable snapshot references is saved and loaded
- **THEN** the file contains no runtime pointer addresses
- **AND** the loaded Loop reconstructs handles for its loaded immutable objects
- **AND** selected snapshot content remains semantically equivalent

### Requirement: Snapshot persistence retains the StorageManager boundary

The system SHALL keep snapshot serialization, deferred save work, loading, and
compatibility migration owned by `StorageManager`. The storage format SHALL
persist immutable content, stable logical references, the selected snapshot
root, loop metadata, and the latest saved complete snapshot. It SHALL NOT persist
runtime addresses, derived representations, playback cursor/order, active
ledgers, undo history, or snapshot lineage for post-reboot undo.

#### Scenario: Storage does not absorb runtime representation ownership

- **WHEN** a snapshot is saved or loaded
- **THEN** `StorageManager` serializes or reconstructs snapshot content
- **AND** `Loop` rebuilds loop-local handles and selects the runtime snapshot
- **AND** derived representation owners remain responsible for their own rebuilds

#### Scenario: Runtime undo is not a persisted graph

- **WHEN** a loop is saved
- **THEN** the file contains the latest complete snapshot
- **AND** it does not contain a runtime undo stack
- **AND** it does not contain enough abandoned-branch metadata to restore redo

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
