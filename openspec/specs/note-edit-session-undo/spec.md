## Purpose

In-edit session undo (**E:**) for **NoteEditSession** before **saveNoteEditPass** commits rows to
**passes.editPasses[]**. Shipped in **pool-budget** task group 9 and **note-edit-session-undo-gpio**
(kind-boundary push). Entry payload uses scoped **editPass** pre-commit rows (**editRows**), not
full **LoopEventStore** clones.

## Requirements

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

**NoteEditSessionUndoStack** entries SHALL store **SessionUndoEntry** with:

- **`editRows`** — scoped pre-commit **editPass** rows (**EditPassVec**),
- **`NoteEditFocus`** snapshot,
- **`NoteEditSelection`** and bracket metadata required for **applyUndoRedoLanding**,
- **`editPassIdsAtPush`** — committed **editPass** ids at push time (for undo pass-id filtering).

The system SHALL NOT store a full **LoopEventStore** **cloneShared** copy per undo step.

Entry size SHALL scale with edit scope (overlap notes, changed targets), not with total loop length
(e.g. 128-bar materialized event count).

#### Scenario: Push at geometry kind boundary

- **WHEN** **pushSessionUndoOnKindChange** runs before the first mutation in a new geometry kind
- **THEN** one **SessionUndoEntry** is appended containing **editRows** + **NoteEditFocus**
- **AND** no new pool chunks are allocated solely for the undo entry payload

#### Scenario: Undo rebuilds session store

- **WHEN** **sessionUndo** runs
- **THEN** firmware SHALL materialize from **passes** excluding edit passes committed after the entry baseline
- **AND** apply stored **editRows** via **applyNoteEditPassSequence**
- **AND** restore **NoteEditFocus** and selection metadata

#### Scenario: Long loop session undo memory

- **WHEN** the active loop is 128 bars with many materialized events
- **AND** the user performs four geometry-kind **E:** steps
- **THEN** undo stack memory SHALL remain bounded by entry count × edit-scope metadata
- **AND** SHALL NOT retain four full copies of the materialized loop in the undo stack

### Requirement: Session undo parity

Native tests SHALL prove that materialize + **applyNoteEditPassSequence** restore matches
**cloneShared** restore for overlap scenarios (hidden/shortened overlap notes, move → pitch → move back).

#### Scenario: Parity on overlap round-trip

- **WHEN** the same edit sequence is undone via clone-based and **editRows**-based paths
- **THEN** **NoteEditSession.store** event content and **NoteEditFocus** SHALL match

### Requirement: Session undo for live capture during note edit

When overdub stops while note edit mode is active, the system SHALL fold capture events into
**NoteEditSession.store** without publishing an **overdubPass** to **passes[]**. The system SHALL
push one **SessionUndoEntry** with pre-filled **`redoEditRows`** from **`buildSessionStoreEditPasses`**
(baseline session flat → folded session flat). Undo SHALL restore the pre-fold session store via
materialize + empty **`editRows`**; redo SHALL apply stored **`redoEditRows`**.

The system SHALL NOT push **OverdubPassAdded** on the global stack while **`isNoteEditActive()`**.
Overdub start during note edit SHALL NOT call **closeNoteEditPass**.

#### Scenario: In-edit overdub stop folds into session store

- **WHEN** overdub stops while **`isNoteEditActive()`**
- **THEN** capture events merge into **NoteEditSession.store**
- **AND** one **SessionUndoEntry** is appended with non-empty **`redoEditRows`**
- **AND** no **overdubPass** is published to **passes[]**

#### Scenario: Session undo removes folded live capture notes

- **WHEN** **sessionUndo** runs on a live-capture entry
- **THEN** **NoteEditSession.store** no longer contains notes added by that overdub
- **AND** **GlobalUndoStack** cursor is unchanged

#### Scenario: Session redo restores folded live capture notes

- **WHEN** **sessionRedo** runs after live-capture session undo
- **THEN** **NoteEditSession.store** again contains the folded overdub notes
- **AND** **applySessionRedoEntry** applies pre-filled **`redoEditRows`**

### Requirement: Session undo admission and trim

Before appending a **SessionUndoEntry**, the system SHALL check heap admission for the entry payload.
Under memory pressure, the system SHALL trim oldest session undo entries while keeping a configurable
preferred depth when affordable.

#### Scenario: Push rejected on heap pressure

- **WHEN** heap is below **HEAP_RESERVE_BYTES** plus estimated entry cost
- **THEN** the push SHALL fail with a logged warning
- **AND** **NoteEditSessionUndoStack** size SHALL be unchanged
