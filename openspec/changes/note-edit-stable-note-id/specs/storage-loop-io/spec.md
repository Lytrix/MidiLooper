## MODIFIED Requirements

### Requirement: Loop geometry fields apply on load

**applySnapshotToLoop** SHALL copy persisted loop geometry fields from **PersistedLoopSnapshot**
without discarding documented temporal fields. For v6 snapshots, **applySnapshotToLoop** SHALL also
restore **`nextNoteId`** into **`Loop::nextNoteId_`**.

#### Scenario: applySnapshotToLoop sets startLoopTick

- **WHEN** **applySnapshotToLoop** runs with snapshot **startLoopTick** = N
- **THEN** **loop.startLoopTick** = N after apply

#### Scenario: applySnapshotToLoop restores nextNoteId

- **WHEN** **applySnapshotToLoop** runs with v6 snapshot **nextNoteId** = M
- **THEN** **loop.nextNoteId_** = M after apply
- **AND** the next **allocateNoteId()** on that loop does not return an id already assigned to a loaded note-on

## ADDED Requirements

### Requirement: SD v6 MidiEvent includes noteId

v6 loop persistence SHALL read and write **MidiEvent** records including **`uint32_t noteId`**. On
write, note-on events SHALL persist their assigned **`noteId`**; note-off **`noteId`** field SHALL
be written as **0**.

#### Scenario: Round-trip noteId on note-on

- **WHEN** a loop with note-ons carrying non-zero **noteId** values is saved and loaded under v6
- **THEN** loaded note-on events retain the same **noteId** values
- **AND** **reconstructNotes** reports matching **DisplayNote.noteId** values

### Requirement: SD v6 edit pass targetNoteId wire

**writePersistedEditPass** for **EditPassType::Note** SHALL write **`targetNoteId`** (uint32_t)
instead of **NoteRef** **target**. **readPersistedEditsTail** SHALL read the same field for v6
files.

#### Scenario: v6 edit row size matches targetNoteId

- **WHEN** a note Delete row is persisted under v6
- **THEN** on-disk target field is 4 bytes (**targetNoteId**)
- **AND** load restores **editPass.targetNoteId** for replay

### Requirement: Reject pre-v6 slot files

**readLoopPersisted** SHALL reject slot files with format version below v6 when **NoteId** layout
is required. Partial reads SHALL fail hard per existing **readPersistedEditsTail** rules.

#### Scenario: v5 file rejected

- **WHEN** SD contains a v5 loop file with **NoteRef**-sized edit targets
- **THEN** **readLoopPersisted** returns **false**
- **AND** **loadState** does not report success for that slot
