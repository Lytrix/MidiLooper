## ADDED Requirements

### Requirement: Session undo for overdub during note edit

When overdub stops while note edit mode is active, the system SHALL push a **SessionUndoEntry**
with **`overdubPassIdAtPush`** referencing the published **overdubPass**. The system SHALL NOT
clear the entire **NoteEditSessionUndoStack** without pushing that entry.

#### Scenario: Overdub stop pushes session undo entry

- **WHEN** overdub stops while **`isNoteEditActive()`**
- **THEN** one **SessionUndoEntry** is appended with valid **`overdubPassIdAtPush`**
- **AND** **NoteEditSession.store** is rematerialized from **passes** including the new overdub

#### Scenario: Session undo disables overdub pass without exiting note edit

- **WHEN** **sessionUndo** runs on an entry with **`overdubPassIdAtPush`**
- **THEN** the referenced **overdubPass** becomes **Disabled**
- **AND** **NoteEditSession.store** rematerializes without that overdub layer
- **AND** **GlobalUndoStack** cursor is unchanged

#### Scenario: Session redo restores overdub pass

- **WHEN** **sessionRedo** runs after session overdub undo
- **THEN** the referenced **overdubPass** becomes **Active** again
- **AND** **NoteEditSession.store** rematerializes with that overdub layer
