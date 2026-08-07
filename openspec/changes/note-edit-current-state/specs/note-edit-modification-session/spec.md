## MODIFIED Requirements

### Requirement: Baseline at note select

When the user selects a note with fader 1 during note edit, the system SHALL preserve the active edit driver's read-only transaction baseline map and SHALL set the focus **commitBaseline** from the selected note's committed baseline entry.

Current editable geometry for the selected `NoteId` SHALL be read from note edit current state, not reconstructed from `NoteEditSession.store` or display ordering.

#### Scenario: Overlap notes from committed passes included

- **WHEN** the selected moving note has other notes present in committed materialized state from a **recordPass**
- **THEN** those notes SHALL appear in the transaction baseline map without a separate pass read
- **AND** the selected note's current editable span SHALL come from note edit current state

### Requirement: Live source of truth

During note edit, note edit current state SHALL be the sole mutable source of current editable note geometry and presence. **NoteEditSession.store** SHALL be the canonical MIDI event projection for `editAwareMidiEvents()`, playback preview, serialization, and compatibility during migration.

Hiding an overlap note SHALL mark the row `Hidden` in current state while omitting the projected event pair from **NoteEditSession.store**. **`overlapNotes`** scratch is not commit, filter, or current-geometry authority.

#### Scenario: Incremental current-state mutation

- **WHEN** overlap hides one overlap note via **`applyEditSessionActions`**
- **THEN** only the affected current-state rows change for that action batch
- **AND** **NoteEditSession.store** is refreshed from the canonical projection of those rows

### Requirement: Macro commit produces one noteEditPass batch

At **`commitAllPendingNoteEditActions`**, the system SHALL emit **one `noteEditPass` batch** with **`EditPass` rows for every changed `NoteId`** (mover, overlap hide/shorten/restore, add, delete) from committed baseline compared to note edit current state. **`overlapNotes`** and projected-store pair absence MUST NOT be the row source.

Legacy store-diff builders MAY remain during migration only as parity checks. They MUST NOT change the committed **`EditPass`** output after current-state commit authority is active.

#### Scenario: Overlap rows from current-state diff

- **WHEN** macro commit runs after a move that hid an overlap note
- **THEN** **`EditPass`** rows for the mover and hidden target are derived from current-state rows compared to committed baseline
- **AND** **`buildPreCommitOverlapEditPasses`** from **`overlapNotes`** is not used

#### Scenario: Legacy store diff does not own macro commit

- **GIVEN** legacy store-diff diagnostic output exists for the open NOTE_EDIT session
- **WHEN** macro commit runs
- **THEN** the persisted **`noteEditPass`** batch is serialized from current state compared to committed baseline
- **AND** legacy store-diff diagnostic output is not used as persistence authority

### Requirement: Overlap and focus use derived length at macro commit

After **`normalizeAll`** at macro commit, focus moving-note length SHALL equal the current-state span length for **`EditorSelection.primaryNote`**.

**`movingNoteRange`** is **retired** as length authority.

#### Scenario: Macro commit refreshes mover length from current state

- **WHEN** macro commit normalizes a moved NOTE_EDIT session
- **THEN** the moving-note length is read from the current-state row for **`EditorSelection.primaryNote`**
- **AND** **`movingNoteRange`** is not used as length authority

### Requirement: Focus and baseline maps keyed by NoteId

**NoteEditFocus** SHALL track **movingNoteId**, **`overlapNotes`** (scratch only), and **baselineMap** keyed by **`NoteId`**. **commitBaseline** and overlap restore SHALL preserve **`NoteId`** on restored note-ons.

Current editable geometry and presence SHALL be read from note edit current state. **NoteEditFocus** SHALL NOT become a second current-geometry owner.

#### Scenario: Moving note id stable across pitch edit

- **WHEN** pitch edit changes the moving note's pitch field
- **THEN** **focus.movingNoteId** is unchanged
- **AND** the current-state row for that **`NoteId`** owns the updated pitch

#### Scenario: Focus reads current state after display reorder

- **WHEN** display ordering changes after same-pitch note movement
- **THEN** focus refresh resolves **`EditorSelection.primaryNote`** by **`NoteId`**
- **AND** reads that row's current span from note edit current state
