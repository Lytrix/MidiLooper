## MODIFIED Requirements

### Requirement: NoteEditSession undo stack

While note edit mode is active, undo and redo for pre-commit gestures SHALL operate on **NoteEditSessionUndoStack** only.

Undo and redo SHALL restore note edit current state and **NoteEditSession.store** projection as one logical session snapshot.

#### Scenario: Session undo does not pop global stack

- **WHEN** the user presses undo while in note edit mode before **saveNoteEditPass**
- **THEN** **NoteEditSessionUndoStack** restores the prior edit state
- **AND** **GlobalUndoStack** cursor is unchanged

#### Scenario: Session redo

- **WHEN** the user presses redo while in note edit mode before **saveNoteEditPass**
- **THEN** note edit current state restores from the session stack
- **AND** **NoteEditSession.store** restores to the canonical projection of that state

### Requirement: Small session undo entries

**NoteEditSessionUndoStack** entries SHALL store **SessionUndoEntry** with:

- current-state rows or a lossless current-state delta required to restore edited `NoteId` rows,
- projected **NoteEditSession.store** data when required for compatibility during migration,
- **NoteEditFocus** snapshot,
- **EditorSelection** and bracket metadata required for **applyUndoRedoLanding**,
- **editPassIdsAtPush** — committed **editPass** ids at push time (for undo pass-id filtering).

The system SHALL NOT store a full **LoopEventStore** **cloneShared** copy per undo step.

Entry size SHALL scale with edit scope or documented current-state delta scope, not with total loop length when a scoped representation is active.

#### Scenario: Push at geometry kind boundary

- **WHEN** **pushSessionUndoOnKindChange** runs before the first mutation in a new geometry kind
- **THEN** one **SessionUndoEntry** is appended containing current-state restore data, **NoteEditFocus**, and selection metadata
- **AND** no full **LoopEventStore** clone is allocated solely for the undo entry payload

#### Scenario: Undo restores state and projection

- **WHEN** **sessionUndo** runs
- **THEN** firmware restores note edit current state from the entry payload
- **AND** refreshes or restores **NoteEditSession.store** as the canonical projection of that state
- **AND** restores **NoteEditFocus** and selection metadata

#### Scenario: Long loop session undo memory

- **WHEN** the active loop is 128 bars with many materialized events
- **AND** the user performs four geometry-kind **E:** steps
- **THEN** undo stack memory SHALL remain bounded by entry count × edit-scope or current-state-delta metadata
- **AND** SHALL NOT retain four full copies of the materialized loop in the undo stack

### Requirement: Session undo parity

Native tests SHALL prove that current-state restore plus projected-store refresh matches expected NOTE_EDIT behavior for overlap scenarios, hidden/shortened overlap notes, move → pitch → move back, added rows, deleted rows, and repeated same-pitch moves.

During migration, tests SHALL compare current-state restore output against legacy materialize + **applyNoteEditPassSequence** paths for scenarios that legacy handles correctly.

#### Scenario: Parity on overlap round-trip

- **WHEN** the same edit sequence is undone via current-state restore and the migration parity path
- **THEN** **NoteEditSession.store** event content, note edit current state, **NoteEditFocus**, and **EditorSelection** SHALL match expected state

### Requirement: Session undo for live capture during note edit

When overdub stops while note edit mode is active, the system SHALL fold capture events into note edit current state without publishing an **overdubPass** to **passes[]**. The system SHALL refresh **NoteEditSession.store** from current-state projection and SHALL push one **SessionUndoEntry** with redo data derived from current state.

The system SHALL NOT push **OverdubPassAdded** on the global stack while **`isNoteEditActive()`**. Overdub start during note edit SHALL NOT call **closeNoteEditPass**.

#### Scenario: In-edit overdub stop folds into current state

- **WHEN** overdub stops while **`isNoteEditActive()`**
- **THEN** capture events merge into note edit current state as `Added` rows or current-state updates
- **AND** **NoteEditSession.store** refreshes from current-state projection
- **AND** one **SessionUndoEntry** is appended with redo data
- **AND** no **overdubPass** is published to **passes[]**

#### Scenario: Session undo removes folded live capture notes

- **WHEN** **sessionUndo** runs on a live-capture entry
- **THEN** note edit current state no longer exposes notes added by that overdub as visible current rows
- **AND** **NoteEditSession.store** projection no longer contains their event pairs
- **AND** **GlobalUndoStack** cursor is unchanged

#### Scenario: Session redo restores folded live capture notes

- **WHEN** **sessionRedo** runs after live-capture session undo
- **THEN** note edit current state again contains the folded overdub notes
- **AND** **NoteEditSession.store** projection again contains matching event pairs

### Requirement: Session undo stores EditorSelection with NoteIds

Note edit session undo entries SHALL snapshot **EditorSelection** (**trackId**, **loopId**, **selectedNotes**, **primaryNote**, **bracketTick**) and current-state restore data keyed by **`NoteId`**. Restore SHALL reinstate the same **NoteId** values on **EditorSelection** and current-state rows without allocating replacement ids.

#### Scenario: Undo round-trip preserves primaryNote

- **WHEN** the user edits a note, pushes session undo, then restores from undo
- **THEN** **EditorSelection.primaryNote** matches the pre-edit snapshot
- **AND** the targeted current-state row retains the same **noteId**

#### Scenario: Session undo diff emits targetNoteId rows

- **WHEN** redo or commit rows are built from restored current state
- **THEN** Delete rows carry **targetNoteId** from the removed current-state row
- **AND** Create rows carry assigned **noteId** from the added current-state row
- **AND** Update rows carry **targetNoteId** plus payload fields
