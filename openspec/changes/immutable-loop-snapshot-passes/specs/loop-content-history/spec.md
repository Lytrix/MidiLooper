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

### Requirement: Load reconstructs editing state from content

After loading a Loop from disk, the runtime SHALL identify the serialized snapshot tip, reconstruct loop-local stable handles, derive supported runtime undo units, and start with redo empty. An older supported format MAY be migrated off the MIDI path by replaying its records into complete snapshot boundaries. Normal undo and redo SHALL select existing snapshots rather than replay history.

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
