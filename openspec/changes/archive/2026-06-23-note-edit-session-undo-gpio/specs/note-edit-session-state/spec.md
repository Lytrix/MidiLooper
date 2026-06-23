## ADDED Requirements

### Requirement: NoteEditSessionState single owner of edit kind and selection

During an active note edit overlay, **`NoteEditSessionState`** on **`EditManager`** SHALL be the sole owner of:

- **edit kind**: **select** | **add** | **delete** | **move** | **pitch** | **length** (default **select** on enter)
- **selection**: **none** (`selectedNoteIdx == -1`) | **selected** (**`NoteRef`**, filtered display index, bracket tick)

**`selectedNoteIdx`**, **`EditManager::currentState`**, and **`EDIT_MODE_*`** program output SHALL be derived from **`NoteEditSessionState`** via **`syncNoteEditSessionStateToUi`**, not updated independently by fader or encoder paths.

#### Scenario: Default on enter note edit

- **WHEN** the user enters the note edit overlay
- **THEN** **edit kind** SHALL be **select**
- **AND** session-state bracket tick SHALL be set to the current transport tick
- **AND** selection SHALL auto-select by bracket-first/nearest rule (note at bracket tick first, otherwise nearest note to current tick, otherwise none)
- **AND** encoder turn SHALL navigate bracket like fader 1 select

#### Scenario: Enter via loop/edit mode switch

- **WHEN** loop/edit mode switch changes main mode from **`MAIN_MODE_LOOP_EDIT`** to **`MAIN_MODE_NOTE_EDIT`**
- **THEN** note edit session-state SHALL open with the same default-enter initialization

#### Scenario: Deselect empty

- **WHEN** fader 1 moves to an empty step
- **THEN** **selection** SHALL be **none**
- **AND** **edit kind** SHALL remain **select**
- **AND** session undo SHALL NOT push solely due to deselect

### Requirement: All session-state transitions through one API

Fader paths, **`cycleNoteEditType`**, encoder nav, undo/redo landing, and add/delete entry SHALL mutate **`NoteEditSessionState`** only through **`EditManager`** transition helpers (e.g. **`applySelectNav`**, **`applyCycleEditKind`**, **`applyGeometryKindFromControl`**, **`applyUndoRedoLanding`**), then call **`syncNoteEditSessionStateToUi`**.

#### Scenario: Fader move updates session-state kind

- **WHEN** the user applies a coarse or fine fader move with a selected note
- **THEN** **edit kind** SHALL become **move** before the mutation
- **AND** this fader-origin kind change SHALL NOT rebase encoder cycle order

#### Scenario: Add and delete set session-state kind

- **WHEN** the user triggers add or delete geometry
- **THEN** **edit kind** SHALL become **add** or **delete** for that operation before mutation and undo boundary checks

#### Scenario: Fader pitch updates session-state kind

- **WHEN** the user changes pitch via fader 4
- **THEN** **edit kind** SHALL become **pitch**
- **AND** encoder **`onEncoderTurn`** SHALL use pitch acceleration and pitch handler until kind changes

#### Scenario: Encoder cycle resets away from fader drift

- **WHEN** **edit kind** is already **move** from fader use
- **AND** the user presses the encoder to **`cycleNoteEditType`**
- **THEN** the encoder cycle step SHALL resolve to **move** as cycle anchor, then continue in **`select_move_pitch_length`** on subsequent encoder presses

### Requirement: Geometry faders require selected note

**move**, **pitch**, and **length** geometry controls (faders and encoder turn in those kinds) SHALL NOT mutate store when **selection** is **none**.

#### Scenario: Coarse fader with no selection

- **WHEN** **selection** is **none**
- **THEN** coarse and fine faders SHALL NOT apply move geometry

### Requirement: Session-state scope boundaries

**NoteEditSessionState** SHALL NOT own **`MAIN_MODE_NOTE_EDIT` vs LOOP_EDIT**, fader grace/feedback timing, overlap **`NoteEditFocus`**, or session undo push policy. Those SHALL observe kind transitions via the transition helpers.

#### Scenario: Session mode unchanged by kind cycle

- **WHEN** the user **`cycleNoteEditType`** among move/pitch/length
- **THEN** **`NoteEditManager::currentMainEditMode`** SHALL remain **NOTE_EDIT**
