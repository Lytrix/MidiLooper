## ADDED Requirements

### Requirement: Loop persists content records only

The Loop file SHALL persist ordered immutable content records (`RecordPass`, `OverdubPass`, `EditPass`, and `LoopGeometry`). The system SHALL NOT persist an undo stack, history cursor, history transition, or undo/redo record. NOTE_EDIT session undo (`E:`) is pre-commit and SHALL remain outside this persist model. Session `editPassIds` SHALL stay in-session only; a committed or reboot-durable noteEditPass batch is one persist unit.

#### Scenario: Stop does not write undo history

- **WHEN** record or overdub stop admits persistence
- **THEN** admitted work includes loop content persist
- **AND** admitted work does not include `LoopUndoHistory` after Stage 3

#### Scenario: Content prefix defines effective Loop

- **WHEN** a persisted content prefix is replayed
- **THEN** the reconstructed Loop matches the effective notes for that prefix
- **AND** any information required to define that state is content metadata on the records, not a persisted undo payload

### Requirement: Load reconstructs editing state from content

After loading a Loop from disk, the runtime SHALL derive: the current content tip; the number of undo steps; the content records that constitute each undo unit; redo empty at load; which records are currently effective. Undo SHALL be able to walk those units until no content remains. Serialize then reload SHALL reconstruct the same Loop and the same derived undo units.

#### Scenario: Reboot at the tip

- **WHEN** a Loop is saved at the content tip and loaded after reboot
- **THEN** display undo depth equals the derived undo-step count
- **AND** redo is empty
- **AND** undo can walk back to empty

#### Scenario: Grouped records are one undo unit

- **WHEN** one user operation produced multiple content records (companions, `noteEditPassIndex` batch, or equivalent content metadata)
- **THEN** load derives those records as one undo unit
- **AND** one undo step disables or excludes that whole unit

#### Scenario: New work after undo is not required at load

- **WHEN** the Loop is loaded
- **THEN** the runtime does not restore a mid-session editor cursor
- **AND** the reconstructed position is the content tip
