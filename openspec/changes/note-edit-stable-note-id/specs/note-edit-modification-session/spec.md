## MODIFIED Requirements

### Requirement: Scoped overlap commit before saveNoteEditPass

Before **saveNoteEditPass** at fader-1 reselect or commit boundary, the system SHALL resolve
**overlapNotes** into the store for impacted **`NoteId`s** only, emit **EditChange** entries only
for notes whose committed state differs from **commitBaseline** / baseline map, and rebuild the
session store via **LoopPasses::materialize** overlay.

#### Scenario: Overlap commit does not expand overlap set

- **WHEN** fader-1 reselect commits overlap resolution for the moving note only
- **THEN** no **`NoteId`** SHALL be added to **overlapNotes** beyond impacted overlap notes already tracked

## ADDED Requirements

### Requirement: Focus and baseline maps keyed by NoteId

**NoteEditFocus** SHALL track **movingNoteId**, **overlapNotes**, and **baselineMap** keyed by
**`NoteId`**, not **NoteRef** geometry. **commitBaseline** and overlap restore SHALL preserve
**`NoteId`** on restored note-ons.

#### Scenario: Moving note id stable across pitch edit

- **WHEN** pitch edit changes the moving note's pitch field
- **THEN** **focus.movingNoteId** is unchanged
- **AND** the note-on **MidiEvent** retains the same **noteId**
