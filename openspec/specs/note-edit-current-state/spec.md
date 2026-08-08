## Purpose

During NOTE_EDIT, `EditSession` owns current editable note state keyed by stable `NoteId`.
`EditSession.store` is the canonical MIDI event projection of that state, not independent
editable-state authority.

Shipped 2026-08-08 (DEC-029). Closeout:
[`openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md`](../../changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md).

## Requirements

### Requirement: Note edit current state owner

During NOTE_EDIT, `EditSession` SHALL own current editable note state through a `NoteId`-keyed current-state collection.

Each editable note that can participate in NOTE_EDIT SHALL have exactly one current-state row. The row SHALL store the stable `NoteId`, committed span, current span, and presence. The system SHALL derive moved, resized, pitch-changed, restored, hidden, deleted, and added status from committed span, current span, and presence rather than storing independent mutable flags.

`baselineMap` SHALL remain transaction baseline for the active edit driver and SHALL NOT be treated as current editable geometry.

#### Scenario: One current row per NoteId

- **WHEN** a NOTE_EDIT session is opened from committed materialized events
- **THEN** each note with a valid `NoteId` has exactly one current-state row
- **AND** no current-state row shares its `NoteId` with another row

#### Scenario: Changed status is derived

- **WHEN** a note's current span differs from its committed span
- **THEN** the system derives the note's changed status from the two spans
- **AND** no separate moved, resized, or pitch-changed flag is required to determine the diff

### Requirement: Note edit presence model

Current-state rows SHALL use one explicit presence value for NOTE_EDIT visibility and lifecycle: `Visible`, `Hidden`, `Deleted`, or `Added`.

Presence SHALL be the authority for whether a row participates in current editing and projection:

- `Visible` means an existing committed note is present in the current editable state.
- `Hidden` means an existing committed note is temporarily absent from projected MIDI events while its current span remains available for restore or later overlap edits.
- `Deleted` means an existing committed note is removed from the current editable state and remains available for commit diff generation.
- `Added` means a note created during the active NOTE_EDIT session exists in current state with a new stable `NoteId`.

#### Scenario: Hidden note keeps editable geometry

- **WHEN** overlap handling hides a note
- **THEN** that note's current-state row remains present with presence `Hidden`
- **AND** its current span remains available to later restore or modify the note by `NoteId`

#### Scenario: Deleted note keeps commit identity

- **WHEN** the user deletes a note during NOTE_EDIT
- **THEN** that note's current-state row remains present with presence `Deleted`
- **AND** commit generation can emit the delete using the row's stable `NoteId`

#### Scenario: Added note receives stable identity

- **WHEN** the user creates a note during NOTE_EDIT
- **THEN** the system creates an `Added` current-state row with a valid `NoteId`
- **AND** later move, length, pitch, undo, redo, and commit paths address that row by `NoteId`

### Requirement: Canonical event projection

During NOTE_EDIT, `EditSession.store` SHALL be the canonical deterministic MIDI event projection of note edit current state. `EditSession.store` SHALL NOT be independent editable-state authority.

Projection SHALL be intentionally lossy:

- `Visible` rows project to note-on/note-off event pairs.
- `Added` rows project to note-on/note-off event pairs.
- `Hidden` rows do not project to event pairs.
- `Deleted` rows do not project to event pairs.

The system SHALL NOT encode hidden or deleted current-state rows into projected MIDI events.

#### Scenario: Visible and added rows project

- **WHEN** current state contains `Visible` and `Added` rows
- **THEN** projection writes matching note-on/note-off event pairs to `EditSession.store`
- **AND** the projected event pairs carry the current row `NoteId` values

#### Scenario: Hidden and deleted rows do not project

- **WHEN** current state contains `Hidden` and `Deleted` rows
- **THEN** projection omits MIDI event pairs for those rows
- **AND** the rows remain available in current state for restore, undo, redo, and commit diff generation

#### Scenario: Projected events are not state authority

- **WHEN** a normal NOTE_EDIT geometry reader needs the current span or presence for a `NoteId`
- **THEN** it reads note edit current state
- **AND** it does not reconstruct editable state from projected MIDI events

### Requirement: Current-state mutation ownership

All NOTE_EDIT mutations to current note geometry or presence SHALL go through the current-state owner API before downstream readers observe the result.

This applies to move, length, pitch, add, delete, hide, shorten, restore, overlap actions, folded live capture, undo, redo, and commit replay during the active session.

Direct NOTE_EDIT projected-store mutation SHALL be allowed only inside the current-state projection owner or inside temporary migration code with a named removal trigger.

#### Scenario: Apply mutates current state before projection

- **WHEN** `applyEditSessionActions` applies restore, shorten, hide, move, pitch, or length actions
- **THEN** it updates note edit current state through the owner API
- **AND** `EditSession.store` is refreshed from current-state projection

#### Scenario: Direct projected-store writers are gated

- **WHEN** code attempts to modify NOTE_EDIT geometry through `sessionMidiEvents()` or `track.editAwareMidiEvents()`
- **THEN** the write is either routed through the current-state owner
- **OR** it is marked temporary migration code with a removal trigger

### Requirement: Current-state debug verification

Debug verification SHALL protect note edit current-state ownership invariants.

At minimum, debug checks SHALL cover selected `NoteId` existence, duplicate `NoteId` rows, visible-row projection parity, hidden/deleted row projection omission, and commit/read paths that attempt to reconstruct current geometry from baseline plus event absence.

#### Scenario: Selected NoteId must exist

- **WHEN** NOTE_EDIT has an active selected primary note
- **THEN** debug verification confirms `EditorSelection.primaryNote` resolves to a current-state row

#### Scenario: Projection parity for visible rows

- **WHEN** current state projects to `EditSession.store`
- **THEN** debug verification confirms every visible or added row has a matching projected pair
- **AND** hidden and deleted rows do not require a projected pair
