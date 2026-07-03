## Purpose

SD v4 loop persistence SHALL fail hard on partial reads and restore loop geometry fields on load.
Shipped in **loop-ownership-hardening** (archived 2026-06-22).

## Requirements

### Requirement: Persisted edit tail read fails hard

**readPersistedEditsTail** SHALL return **false** when any required **ioRead** fails. It SHALL NOT
return **true** with **editPasses** cleared after a partial read.

#### Scenario: Truncated edit tail fails load

- **WHEN** SD data ends before **editCount** **EditPass** rows are read
- **THEN** **readPersistedEditsTail** returns **false**
- **AND** **readLoopPersisted** fails
- **AND** **loadState** does not report success

#### Scenario: Valid v4 tail succeeds

- **WHEN** SD contains a complete **editPasses** tail
- **THEN** **readPersistedEditsTail** returns **true**
- **AND** **passes.editPasses** matches persisted content

### Requirement: Loop geometry fields apply on load

**applySnapshotToLoop** SHALL copy persisted loop geometry fields from **PersistedLoopSnapshot**
without discarding documented temporal fields. **nextNoteId** SHALL be restored so new notes after
load receive ids strictly greater than any id present in the loaded loop.

#### Scenario: applySnapshotToLoop sets startLoopTick

- **WHEN** **applySnapshotToLoop** runs with snapshot **startLoopTick** = N
- **THEN** **loop.startLoopTick** = N after apply

#### Scenario: nextNoteId restored after load

- **WHEN** a v6 snapshot with **nextNoteId** = M is loaded
- **THEN** **loop.nextNoteId** = M after apply
- **AND** the next allocated **noteId** on that loop is ≥ M

### Requirement: SD v6 persists noteId on note-on events

**PersistedMidiEvent** for note-on rows SHALL include **noteId** on v6 save and SHALL restore
**noteId** on v6 load. Note-off rows SHALL NOT carry **noteId** on disk.

#### Scenario: Round-trip noteId on capture events

- **WHEN** a loop with note-on events is saved and loaded under v6
- **THEN** each note-on **MidiEvent** retains the same **noteId** values

### Requirement: SD v6 persists targetNoteId on edit rows

**PersistedEditPass** note rows SHALL include **targetNoteId** on v6 save and restore on v6 load.

#### Scenario: Edit row targetNoteId round-trip

- **WHEN** a note **editPass** with **targetNoteId** = N is saved and loaded
- **THEN** the in-RAM **editPass** row has **targetNoteId** = N
