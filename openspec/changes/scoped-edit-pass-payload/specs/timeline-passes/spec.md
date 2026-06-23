## MODIFIED Requirements

### Requirement: Note editPass stored as target and MIDI fields

Note **editPass** rows SHALL store **EditPassType**, **EditActionType**, **EditPropertyType**, a
baseline **NoteRef** **target**, and the MIDI note fields required for that property:

- **Create:** **note on** + **note off** events
- **Delete:** **target** only
- **NoteRange** (move): **target**, `startTick`, `endTick`
- **Length:** **target**, `startTick`, `endTick` (start-point length edit may use `startTick` later)
- **Pitch:** **target**, pitch
- **Velocity:** **target**, **note on** velocity

Firmware SHALL NOT use a generic **payload** blob or legacy **EditChange** lists on new writes.

#### Scenario: Length row stores full tick span

- **WHEN** a length edit is committed
- **THEN** the row has **propertyType = Length** with `startTick` and `endTick`
- **AND** apply uses length/overlap semantics (not move semantics)

#### Scenario: Length row ready for start-point edit

- **WHEN** only the note end changes today
- **THEN** `endTick` reflects the new end and `startTick` is still stored on the row
- **AND** a future start-point length UI can change `startTick` without a storage format change

#### Scenario: Move row uses NoteRange

- **WHEN** a move edit is committed
- **THEN** the row has **propertyType = NoteRange** with `startTick` and `endTick`
- **AND** apply uses move semantics (distinct from **Length**)

#### Scenario: v4 state file rejected on load

- **WHEN** SD contains storage version 1–4
- **THEN** **loadState** returns **false**
- **AND** firmware runs with default empty in-RAM state

#### Scenario: v5 save writes canonical edit rows only

- **WHEN** a loop with edits is saved under v5
- **THEN** each **editPass** row on disk uses **EditPassType** and MIDI field columns only
- **AND** no **EditChange** blobs are written

#### Scenario: v5 load reads canonical edit rows only

- **WHEN** a v5 state file is loaded
- **THEN** **readPersistedEditsTail** parses only the canonical edit-row wire
- **AND** materialize matches the saved loop
