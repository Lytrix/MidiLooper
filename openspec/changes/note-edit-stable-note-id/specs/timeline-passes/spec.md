## MODIFIED Requirements

### Requirement: Note editPass stored as target and MIDI fields

Note **editPass** rows SHALL store **EditPassType**, **EditActionType**, **EditPropertyType**, a **`NoteId` `targetNoteId`**, and the MIDI note fields required for that property:

- **Create:** **note on** + **note off** events (note-on carries assigned **`noteId`**; **`targetNoteId`** = invalid)
- **Delete:** **`targetNoteId`** only
- **NoteRange** (move): **`targetNoteId`**, `startTick`, `endTick`
- **Length:** **`targetNoteId`**, `startTick`, `endTick` (start-point length edit may use `startTick` later)
- **Pitch:** **`targetNoteId`**, pitch
- **Velocity:** **`targetNoteId`**, **note on** velocity

Firmware SHALL NOT use a generic **payload** blob, legacy **EditChange** lists on new writes, or **`NoteRef`** geometry as note target identity on new writes.

#### Scenario: Length row stores full tick span

- **WHEN** a length edit is committed
- **THEN** the row has **propertyType = Length** with `startTick` and `endTick`
- **AND** apply uses length/overlap semantics (not move semantics)
- **AND** **`targetNoteId`** identifies the note-on to mutate

#### Scenario: Length row ready for start-point edit

- **WHEN** only the note end changes today
- **THEN** `endTick` reflects the new end and `startTick` is still stored on the row
- **AND** a future start-point length UI can change `startTick` without a storage format change

#### Scenario: Move row uses NoteRange

- **WHEN** a move edit is committed
- **THEN** the row has **propertyType = NoteRange** with `startTick` and `endTick`
- **AND** apply uses move semantics (distinct from **Length**)
- **AND** **`targetNoteId`** is unchanged across the edit session for that logical note

#### Scenario: v5 state file rejected on load

- **WHEN** SD contains storage version 1–5 with **NoteRef**-sized edit targets or pre-v6 **MidiEvent** layout
- **THEN** **loadState** returns **false**
- **AND** firmware runs with default empty in-RAM state

#### Scenario: v6 save writes targetNoteId rows

- **WHEN** a loop with note edits is saved under v6
- **THEN** each note **editPass** row on disk uses **`targetNoteId`** (uint32_t) instead of **NoteRef**
- **AND** each **MidiEvent** in **addedEvents** includes **`noteId`** field

#### Scenario: v6 load reads targetNoteId rows

- **WHEN** a v6 state file is loaded
- **THEN** **readPersistedEditsTail** parses **`targetNoteId`** per note row
- **AND** materialize replays rows via **`findNoteOnById`**, not geometry search

## REMOVED Requirements

### Requirement: v5 save writes canonical edit rows only

**Reason**: Superseded by v6 wire format with **`targetNoteId`** and larger **MidiEvent**.

**Migration**: Dev wipe SD; re-record loops under v6. No v5→v6 migration path.

### Requirement: v5 load reads canonical edit rows only

**Reason**: v5 **NoteRef** target wire is incompatible with **`NoteId`** identity model.

**Migration**: Reject v5 files on load; dev wipe and re-record.
