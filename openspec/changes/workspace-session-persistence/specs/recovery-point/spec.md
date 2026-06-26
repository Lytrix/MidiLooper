## ADDED Requirements

### Requirement: RecoveryPoint is hidden safety layer under CurrentSet

**RecoveryPoint** checkpoints SHALL live under `Sets/_current/checkpoints/_YYMMDD_HHMM/`. They
SHALL NOT appear in the SavedSet browser and SHALL NOT be user-selectable for normal recall.

#### Scenario: RecoveryPoint hidden from SavedSet browser

- **WHEN** the user opens SavedSet browser
- **THEN** RecoveryPoint folders under `checkpoints/` are not listed
- **AND** only SavedSet entries and CURRENT status are shown

### Requirement: RecoveryPoint creation triggers

The system SHALL create a RecoveryPoint before at minimum:

- Destructive slot clear
- Full track clear
- Slot loop import that overwrites target slot content
- **loadSetIntoCurrent** that replaces full CurrentSet content

RecoveryPoint is a secondary crash-only safety net. It SHALL NOT replace auto **saveNewSet**
before **loadSetIntoCurrent** when CurrentSet is dirty.

#### Scenario: Checkpoint before clear slot

- **WHEN** the user clears a filled slot
- **THEN** a RecoveryPoint capturing current CurrentSet is written under `Sets/_current/checkpoints/` before the clear commits
- **AND** the clear then proceeds on CurrentSet

#### Scenario: Checkpoint before import loop

- **WHEN** the user confirms IMPORT LOOP into a filled target slot
- **THEN** a RecoveryPoint is created under `Sets/_current/checkpoints/` before the target loop blob is overwritten

#### Scenario: Checkpoint before load set into current

- **WHEN** the user confirms **loadSetIntoCurrent** from a SavedSet
- **THEN** a RecoveryPoint MAY be created under `Sets/_current/checkpoints/` before `_current` is overwritten
- **AND** auto **saveNewSet** for dirty CurrentSet still runs first when required

### Requirement: RecoveryPoint prune policy

The system SHALL automatically prune old RecoveryPoints under `Sets/_current/checkpoints/`,
retaining at minimum the newest 3 and the most recent pre-destructive checkpoint.

#### Scenario: Old checkpoints removed

- **WHEN** RecoveryPoint count under `checkpoints/` exceeds retain policy
- **THEN** oldest checkpoints beyond the retain set are deleted from SD

### Requirement: Boot recovery fallback chain

On boot, when CurrentSet load fails integrity validation, the system SHALL attempt recovery in order:

1. Latest valid RecoveryPoint under `Sets/_current/checkpoints/`
2. Newest valid SavedSet under `/Sets/`

On successful RecoveryPoint or SavedSet recovery, the system SHALL restore content to
`Sets/_current/` and RAM.

#### Scenario: Corrupt CurrentSet recovers from RecoveryPoint

- **WHEN** `Sets/_current/meta.bin` fails validation
- **AND** a valid RecoveryPoint exists under `checkpoints/`
- **THEN** CurrentSet is rebuilt from the RecoveryPoint
- **AND** the device boots with recovered musical state

#### Scenario: Recovery and SavedSet both fail

- **WHEN** CurrentSet, RecoveryPoint, and SavedSet loads all fail
- **THEN** tracks reset to empty default state
- **AND** a diagnostic log entry is emitted
