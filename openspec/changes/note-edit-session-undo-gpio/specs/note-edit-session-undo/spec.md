## ADDED Requirements

### Requirement: Geometry-kind session undo boundaries

During an active **NoteEditSession**, the system SHALL push **`NoteEditSessionUndoStack`** snapshots only when the user transitions between geometry edit kinds (**add**, **delete**, **move**, **pitch**, **length**) or performs an atomic **add** or **delete**. The system SHALL NOT push on select/navigation-only changes, same-kind repeated mutations, or length-mode toggle without length geometry change.

#### Scenario: Multiple moves one undo step

- **WHEN** the user performs coarse and fine move adjustments without changing to another geometry kind
- **THEN** the session undo count SHALL remain unchanged until a geometry kind transition

#### Scenario: Add then move separate steps

- **WHEN** the user adds a note then moves it
- **THEN** the session undo count SHALL increase once for **add** and once when **move** kind begins (separate from **add**)

#### Scenario: Length toggle without geometry

- **WHEN** the user toggles fader length mode on and off without changing note end
- **THEN** the session undo count SHALL NOT increase

#### Scenario: First length geometry after move

- **WHEN** the user changes note end after a move phase
- **THEN** the session undo count SHALL increase once for the transition into **length** geometry

### Requirement: Select navigation is not geometry edit

Bracket and note targeting via fader 1 or encoder in **select** kind SHALL NOT push session undo. **`selectedNoteIdx = -1`** on empty step SHALL NOT push session undo.

#### Scenario: Deselect empty fader 1

- **WHEN** the user moves fader 1 to an empty step
- **THEN** **`selectedNoteIdx`** SHALL be **-1**
- **AND** the session undo count SHALL NOT increase solely due to deselect

#### Scenario: Move fader requires selected note

- **WHEN** **`selectedNoteIdx < 0`**
- **THEN** coarse, fine, and note-value faders SHALL NOT apply geometry mutations

### Requirement: Session undo display E during note edit

While **NoteEditSession** is active and the user is in the note edit overlay, the sidebar SHALL display **`E:nn`** where **nn** is **`NoteEditSessionUndoStack.undoCount()`**. **`E:00`** SHALL mean no geometry edits are undoable in the current session.

#### Scenario: Initial note edit session

- **WHEN** the user enters note edit without geometry changes
- **THEN** the sidebar SHALL show **`E:00`**

### Requirement: Global undo display U after exit

When not in the note edit overlay, the sidebar SHALL display **`U:nn`** from global pass undo (**TrackUndo**), including after **NoteEditPassClosed**.

#### Scenario: After exit note edit

- **WHEN** the user exits note edit after geometry edits
- **THEN** the sidebar SHALL show **`U:`** not **`E:`**

### Requirement: Session undo and redo restore focus and faders

**sessionUndo** and **sessionRedo** SHALL restore the store snapshot, clear **lengthEditingMode**, call **`applyUndoRedoLanding`** on **`NoteEditSessionState`**, and **`syncNoteEditSessionStateToUi`**.

#### Scenario: Undo pitch preserves approved move

- **WHEN** the user moved a note then changed pitch then undoes once
- **THEN** the note SHALL remain at the moved position with pitch restored to the pre-pitch snapshot

#### Scenario: Redo matches undo focus rules

- **WHEN** the user redoes a session undo step
- **THEN** focus, bracket, and fader sync SHALL use the same rebuild rules as undo

#### Scenario: Undo and redo always land in select kind

- **WHEN** session undo or redo completes
- **THEN** **`NoteEditSessionState.kind`** SHALL be **select**

### Requirement: Session stack clears on exit

On **exitEditMode**, the system SHALL clear **NoteEditSessionUndoStack** and close the note edit pass for global undo.

#### Scenario: Exit after edits

- **WHEN** the user exits note edit with **`E:02`**
- **THEN** global **`U:`** SHALL reflect the closed edit pass
- **AND** re-entering note edit SHALL start at **`E:00`**
