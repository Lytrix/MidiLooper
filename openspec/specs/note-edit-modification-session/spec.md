## Purpose

During **NoteEditSession**, move / length / pitch overlap handling, baseline capture at select,
and pre-commit resolve are owned by **NoteEditFocus** + **applyNoteEditChange** on
**NoteEditSession.store**. Shipped in **note-edit-modification-session** with follow-up focus
reads in **note-edit-focus-reads** (archived 2026-06-20).

## Requirements

### Requirement: Single overlap owner for move / length / pitch

During an active **NoteEditSession**, the system SHALL route move, length, and pitch mutations through one overlap engine (`applyNoteEditChange`) that owns overlap note hide, shorten, and restore logic.

#### Scenario: Pitch and move share restore pass

- **WHEN** the user changes pitch then moves the moving note away from a hidden overlap note
- **THEN** the overlap note SHALL be restored using the same restore pass as move-only edits

### Requirement: Baseline at note select

When the user selects a note with fader 1 during note edit, the system SHALL capture a read-only baseline map of all notes from **NoteEditSession.store** and SHALL set the focus **commitBaseline** from the selected note's baseline entry.

#### Scenario: Overlap notes from committed passes included

- **WHEN** the selected moving note has other notes present in the materialized store from a **recordPass**
- **THEN** those notes SHALL appear in the baseline map without a separate pass read

### Requirement: Commit baseline vs moving note range (A1)

The system SHALL maintain separate **commitBaseline** and **movingNoteRange** on the note edit focus. Length edit SHALL update **movingNoteRange.end** to match live end tick and SHALL NOT update **commitBaseline.end** until commit.

#### Scenario: Pending length commit after lengthen

- **WHEN** the user lengthens the moving note without committing
- **THEN** `commitAllPendingNoteEditActions` SHALL detect a pending **ChangeLength** against **commitBaseline**
- **AND** inner overlap note tests SHALL use **movingNoteRange.end** equal to the lengthened end

### Requirement: Live source of truth

During note edit, **NoteEditSession.store** SHALL be the sole mutable MIDI source for `editAwareMidiEvents()`. Hiding an overlap note SHALL remove only impacted note pairs from the store while retaining geometry in the baseline map and **overlapNotes** entry.

#### Scenario: Incremental store mutation

- **WHEN** overlap hides one overlap note
- **THEN** only that note's events and the moving note's events SHALL be modified in the store

### Requirement: Pre-commit resolve (B1)

Before **saveNoteEditPass** at fader-1 reselect or commit boundary, the system SHALL resolve
**overlapNotes** into the store for impacted **`NoteId`s** only, emit **EditChange** entries only
for notes whose committed state differs from **commitBaseline** / baseline map, and rebuild the
session store via **LoopPasses::materialize** overlay.

#### Scenario: Rematerialize matches live session

- **WHEN** the user completes lengthen → move over overlap note → pitch → move back and commits
- **THEN** materialized loop MIDI events SHALL match the live store after pre-commit resolve

#### Scenario: Overlap commit does not expand overlap set

- **WHEN** fader-1 reselect commits overlap resolution for the moving note only
- **THEN** no **`NoteId`** SHALL be added to **overlapNotes** beyond impacted overlap notes already tracked

### Requirement: Focus and baseline maps keyed by NoteId

**NoteEditFocus** SHALL track **movingNoteId**, **overlapNotes**, and **baselineMap** keyed by
**`NoteId`**, not **NoteRef** geometry. **commitBaseline** and overlap restore SHALL preserve
**`NoteId`** on restored note-ons.

#### Scenario: Moving note id stable across pitch edit

- **WHEN** pitch edit changes the moving note's pitch field
- **THEN** **focus.movingNoteId** is unchanged
- **AND** the note-on **MidiEvent** retains the same **noteId**

### Requirement: CC and velocity out of overlapNotes

Velocity and control-change edits SHALL NOT use **overlapNotes**. They SHALL apply only to the focus moving note (or future **ControlChangeEditSession** scope).

#### Scenario: Velocity change during note edit

- **WHEN** the user changes velocity on the selected note
- **THEN** no **NoteRef** SHALL be added to **overlapNotes**
