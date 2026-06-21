## MODIFIED Requirements

### Requirement: NoteEditSession undo stack

While note edit mode is active, undo and redo for pre-commit gestures SHALL operate on
**NoteEditSessionUndoStack** only.

#### Scenario: Session undo does not pop global stack

- **WHEN** the user presses undo while in note edit mode before **saveNoteEditPass**
- **THEN** **NoteEditSessionUndoStack** restores the prior edit state
- **AND** **GlobalUndoStack** cursor is unchanged

#### Scenario: Session redo

- **WHEN** the user presses redo while in note edit mode before **saveNoteEditPass**
- **THEN** **NoteEditSession.store** restores from the session stack

### Requirement: Small session undo entries

**NoteEditSessionUndoStack** entries SHALL store a net **EditChangeList** and **NoteEditFocus**
snapshot (plus selection/bracket metadata required for **applyUndoRedoLanding**). The system SHALL
NOT store a full **LoopEventStore** **cloneShared** copy per undo step.

Entry size SHALL scale with edit scope (overlap notes, changed targets), not with total loop length
(e.g. 128-bar materialized event count).

#### Scenario: Push at geometry kind boundary

- **WHEN** **pushSessionUndoOnKindChange** runs before the first mutation in a new geometry kind
- **THEN** one **SessionUndoEntry** is appended containing **EditChangeList** + **NoteEditFocus**
- **AND** no new pool chunks are allocated solely for the undo entry payload

#### Scenario: Undo rebuilds session store

- **WHEN** **sessionUndo** runs
- **THEN** firmware SHALL **rematerializeEditView** into **NoteEditSession.store**
- **AND** apply stored **EditChange** entries for the restore target
- **AND** restore **NoteEditFocus** and selection metadata

#### Scenario: Long loop session undo memory

- **WHEN** the active loop is 128 bars with many materialized events
- **AND** the user performs four geometry-kind **E:** steps
- **THEN** undo stack memory SHALL remain bounded by entry count × edit-scope metadata
- **AND** SHALL NOT retain four full copies of the materialized loop in the undo stack

### Requirement: Session undo parity before clone removal

Before removing the legacy **cloneShared** push path, native tests SHALL prove that
**rematerializeEditView** + **applyEditChangeList** restore matches **cloneShared** restore for
overlap scenarios (hidden/shortened overlap notes, move → pitch → move back).

#### Scenario: Parity on overlap round-trip

- **WHEN** the same edit sequence is undone via clone-based and EditChange-based paths
- **THEN** **NoteEditSession.store** event content and **NoteEditFocus** SHALL match

### Requirement: Session undo admission and trim

Before appending a **SessionUndoEntry**, the system SHALL check heap admission for the entry payload.
Under memory pressure, the system SHALL trim oldest session undo entries while keeping a configurable
preferred depth when affordable.

#### Scenario: Push rejected on heap pressure

- **WHEN** heap is below **HEAP_RESERVE_BYTES** plus estimated entry cost
- **THEN** the push SHALL fail with a logged warning
- **AND** **NoteEditSessionUndoStack** size SHALL be unchanged
