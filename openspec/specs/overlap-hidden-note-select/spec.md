## Purpose

During **NoteEditSession**, NOTE_EDIT UI consumers (display, fader-1 select, delete) use one
filtered inventory derived from **NoteEditSession.store** and **overlapNotes** (**Hidden**
excluded). Delete and select share that inventory; delete targets the user-selected **NoteRef**,
not stale focus on a prior mover.

Phase 1 shipped 2026-06-19 (`filterSelectableDisplayNotes`). Follow-up overlap restore
(B1 delete/select, B2 hidden restore on pitch) shipped in **note-edit-hitl-focus-restore**
(2026-06-20). Phase 2a+ (**focus.last** reads, encoder port) continues in **note-edit-focus-reads**.

## Requirements

### Requirement: NOTE_EDIT consumers use filterSelectableDisplayNotes

During an active **NoteEditSession** in **MAIN_MODE_NOTE_EDIT**, UI consumers that enumerate notes for selection, display, or delete SHALL use **`filterSelectableDisplayNotes`** (reconstructed **DisplayNote** list from **NoteEditSession.store** minus **overlapNotes** **Hidden**), and SHALL NOT call `Track::getCachedNotes()` directly without that filter.

#### Scenario: One filter for select and display

- **WHEN** an **overlap note** is **Hidden** in **overlapNotes**
- **THEN** **`filterSelectableDisplayNotes`** SHALL exclude that note
- **AND** display and fader-1 select navigation SHALL use the same filtered result

### Requirement: Committed baselineMap vs live focus geometry

At fader-1 note select, the system SHALL build **baselineMap** from committed **passes** materialization (`LoopPasses::materialize`) and SHALL set **focus.last** from live **NoteEditSession.store** geometry for the selected note.

#### Scenario: Pending length preview does not poison commit baseline

- **WHEN** the user lengthens the moving note without committing and reselects the same note
- **THEN** **commitBaseline.end** SHALL remain the committed end tick
- **AND** **focus.last.end** SHALL reflect the live lengthened end in **session.store**

### Requirement: PREVIEW phase forbids rematerialize on fader tick

During live fader move, length, or pitch edit (PREVIEW), the system SHALL mutate **NoteEditSession.store** and **overlapNotes** only via `applyNoteEditChange` and SHALL NOT invoke full loop rematerialize (`commitEditAction` / `LoopPasses::materialize` → session store replacement).

#### Scenario: Overlap fader move without store wipe

- **WHEN** the user moves a note over overlapping notes via fader input
- **THEN** overlap hide/shorten SHALL apply incrementally
- **AND** the device SHALL NOT reboot or lose unrelated notes in **session.store**

### Requirement: Hidden overlap notes are not selectable during NoteEditSession

During an active **NoteEditSession**, any **overlap note** with store state **Hidden** SHALL NOT appear in fader-1 select navigation slots, SHALL NOT receive bracket selection, and SHALL NOT be targeted by delete unless restored to **visible** in **overlapNotes**.

#### Scenario: Contained overlap note hidden after move over long note

- **WHEN** a moving note completely contains an **overlap note** and the firmware marks that **overlap note** **Hidden** in **overlapNotes** and removes its MIDI pair from **NoteEditSession.store**
- **THEN** fader-1 navigation at that note's start tick SHALL NOT offer that **overlap note** as a selectable slot
- **AND** `selectedNoteIdx` SHALL NOT reference the **Hidden** **overlap note**

#### Scenario: Third-note select without bracket hop

- **WHEN** the user selects a different note via fader-1 after prior overlap hides
- **THEN** bracket tick and selected note SHALL match the user's slot on the first stable select
- **AND** the UI SHALL NOT briefly select a **Hidden** **overlap note** at the same or overlapping tick before correcting

### Requirement: Delete targets selected note by stable identity

When delete is invoked on a selected note during **NoteEditSession**, the firmware SHALL commit
pending edits attributable to that selection, then SHALL apply **DeleteNote** to that note's
**`NoteId`** in **NoteEditSession.store** (resolved to note-on + paired note-off at apply time).

#### Scenario: Delete note B after moving over lengthened M0

- **WHEN** the user has lengthened M0, moved other notes, fader-1 selected note B, and invokes delete
- **THEN** serial logs SHALL record **DeleteNote** for B's pitch and start tick
- **AND** the lengthened mover SHALL remain in store at its edited length
- **AND** other notes moved and committed earlier in the session SHALL remain at their edited positions

#### Scenario: Delete hidden overlap note not in selectable inventory

- **WHEN** delete is invoked on a selected note that is visible in **filterSelectableDisplayNotes**
- **THEN** delete removes the note-on and paired note-off for that note's **NoteId**
- **AND** hidden overlap notes remain excluded from the selectable inventory

#### Scenario: Delete does not mutate unrelated note

- **WHEN** delete targets note B by **NoteId**
- **THEN** the firmware SHALL NOT apply **ChangeLength** or **MoveNote** for a different **NoteId**
  unless **focus.movingNoteId** matches the delete target

### Requirement: Delete captures NoteId before commit boundary

When delete is invoked, the system SHALL resolve the delete target **`NoteId`** from
**filterSelectableDisplayNotes** / **EditorSelection.primaryNote** before any pre-commit or
**saveNoteEditPass** operation.

#### Scenario: Delete after select switch uses new primaryNote

- **WHEN** the user selects note B then invokes delete
- **THEN** delete SHALL target B's **NoteId** after scoped focus rebuild on B

### Requirement: NOTE_EDIT display matches selectable session inventory

In **MAIN_MODE_NOTE_EDIT** with active **NoteEditSession**, the piano roll display SHALL render the same set of notes as the selectable session inventory (session reconstruction minus **Hidden** **overlapNotes**).

#### Scenario: Hidden note not shown on display

- **WHEN** an **overlap note** is **Hidden** in **overlapNotes** and absent from session store pairs
- **THEN** the display SHALL NOT draw that note
- **AND** serial reconstruction from session store for verification SHALL agree with the displayed set modulo timing telemetry

### Requirement: Hidden overlap note restores on pitch change

When a moving note pitch change restores a previously **Hidden** **overlap note**, the firmware SHALL return that note to **visible** in **overlapNotes** and SHALL include it in **NoteEditSession.store** pairs so **filterSelectableDisplayNotes** and display agree.

#### Scenario: Inner overlap note visible after pitch restore

- **WHEN** the user changes moving note pitch back over a previously hidden inner **overlap note**
- **THEN** serial logs SHALL include overlap restore for that note
- **AND** fader-1 navigation SHALL offer that note again at its tick
