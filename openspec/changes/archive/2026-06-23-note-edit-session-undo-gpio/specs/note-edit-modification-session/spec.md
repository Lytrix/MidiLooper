## MODIFIED Requirements

### Requirement: Single overlap owner for move / length / pitch

During an active **NoteEditSession**, the system SHALL route move, length, and pitch mutations through one overlap engine (`applyNoteEditChange`) that owns overlap note hide, shorten, and restore logic. Session undo pushes for these mutations SHALL follow geometry-kind boundary rules (see **note-edit-session-undo**); the system SHALL NOT push session undo on every fader step or on **onEnter** of edit FSM states alone.

#### Scenario: Pitch and move share restore pass

- **WHEN** the user changes pitch then moves the moving note away from a hidden overlap note
- **THEN** the overlap note SHALL be restored using the same restore pass as move-only edits

#### Scenario: Move fader without session undo per step

- **WHEN** the user adjusts coarse and fine faders multiple times in one **move** kind phase
- **THEN** overlap handling SHALL still use **applyNoteEditChange**
- **AND** session undo SHALL push at most once for the **move** kind phase until kind changes
