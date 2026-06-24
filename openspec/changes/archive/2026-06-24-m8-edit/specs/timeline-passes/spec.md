## ADDED Requirements

### Requirement: Note edit uses NoteEditSession until saveNoteEditPass

Note-edit paths SHALL mutate **NoteEditSession.store** only. Persisted rows SHALL be appended
only via **saveNoteEditPass**. The system SHALL NOT collapse **NoteEditSession.store** into
capture passes on edit exit.

#### Scenario: Store mutations before saveNoteEditPass

- **WHEN** the user is mid-edit in note edit mode before **saveNoteEditPass**
- **THEN** mutations apply to **NoteEditSession.store** only
- **AND** no **editPass** row is appended until **saveNoteEditPass**

### Requirement: NoteEditSession undo stack

While note edit mode is active, undo and redo for pre-commit gestures SHALL operate on
**NoteEditSessionUndoStack** only.

#### Scenario: Session undo does not pop global stack

- **WHEN** the user presses undo while in note edit mode before **saveNoteEditPass**
- **THEN** **NoteEditSessionUndoStack** restores the prior store snapshot
- **AND** **GlobalUndoStack** cursor is unchanged

#### Scenario: Session redo

- **WHEN** the user presses redo while in note edit mode before **saveNoteEditPass**
- **THEN** **NoteEditSession.store** restores from the session stack

### Requirement: Edit SD autosave with synchronous exit flush

Edit-driven SD writes SHALL occur when loop edit state is dirty. **saveNoteEditPass** SHALL
mark edit state dirty without writing SD immediately. On note edit exit with dirty state, the
system SHALL flush SD before returning to normal playback, including while transport is
**PLAYING** or **OVERDUBBING**.

Periodic edit autosave SHALL use **autosaveIntervalMs** (default 5 min) and SHALL defer only
while any track is recording or overdubbing.

#### Scenario: Dirty mark without immediate SD write

- **WHEN** **saveNoteEditPass** commits an **editPass** while still in note edit mode
- **THEN** edit state is marked dirty
- **AND** SD is not written immediately

#### Scenario: Synchronous flush on note edit exit while playing

- **WHEN** the user exits note edit mode with dirty state and transport is playing
- **THEN** **StorageManager** writes state to SD before the next edit session
- **AND** clears the dirty flag when complete

#### Scenario: Autosave during playback when dirty and not capturing

- **WHEN** edit state is dirty, autosave interval elapsed, and no track is recording or overdubbing
- **THEN** the system writes state to SD

### Requirement: Overdub allowed during note edit mode

The system SHALL allow starting and completing overdub while note edit mode is active.

#### Scenario: noteEditPass close before overdub

- **WHEN** the user starts overdub while in note edit mode
- **THEN** **closeNoteEditPass** pushes **NoteEditPassClosed** for the pre-overdub **noteEditPass**
- **AND** overdub capture proceeds on **Capture**

#### Scenario: Rematerialize NoteEditSession after overdub stop

- **WHEN** overdub stops while note edit mode is still active
- **THEN** **OverdubPassAdded** is pushed on **GlobalUndoStack**
- **AND** **NoteEditSession.store** is rematerialized from **passes**
- **AND** a new **noteEditPass** begins with a fresh **NoteEditSessionUndoStack**

### Requirement: noteEditPass-level global undo after note edit exit

After note edit mode exits, global undo SHALL allow reversing **noteEditPass** batches and
capture passes in order when the user edited, overdubbed in note edit, edited again, then exited.

#### Scenario: Three-step undo after edit plus overdub

- **WHEN** the user completed the edit → overdub → edit → exit sequence
- **THEN** the first global undo reverses the post-overdub **NoteEditPassClosed** batch
- **AND** the second global undo reverses the overdub pass (**OverdubPassAdded**)
- **AND** the third global undo reverses the pre-overdub **NoteEditPassClosed** batch

### Requirement: Edit types covered by NoteEditSession undo and tests

The note edit model SHALL support **NoteEditSessionUndoStack** undo/redo for select, add,
delete, move coarse and fine, pitch, and length before **saveNoteEditPass**. Native tests
SHALL cover each operation.

#### Scenario: Move note coarse session undo

- **WHEN** the user moves a note with coarse positioning and undoes before **saveNoteEditPass**
- **THEN** the note returns to its pre-move position in **NoteEditSession.store**

## MODIFIED Requirements

### Requirement: editFlat derived cache only

**editFlat_** SHALL be a derived materialized view for playback and display. Canonical loop
MIDI history SHALL remain **passes** plus live **NoteEditSession.store** during note edit.

#### Scenario: midiEvents reads from passes materialize

- **WHEN** playback or display requests loop MIDI outside an active note-edit session
- **THEN** the system materializes from **passes**
- **AND** does not treat **editFlat_** as authoritative storage

## REMOVED Requirements

### Requirement: Note edit collapses flat store into takes on exit

**Reason**: Replaced by **saveNoteEditPass** + **passes.editPasses[]**.
**Migration**: **closeNoteEditPass** + SD flush; no **commitMaterializedStore** on edit exit.

### Requirement: Global undo NoteEditCommit snapshot on edit fader enter

**Reason**: Replaced by **NoteEditSessionUndoStack** + **NoteEditPassClosed**.
**Migration**: Remove **NoteEditCommit** undo kind when no callers remain.
