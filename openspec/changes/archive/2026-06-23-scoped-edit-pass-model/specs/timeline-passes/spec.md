## MODIFIED Requirements

### Requirement: Four pass kinds two storage families

The system SHALL support capture pass rows and scoped edit pass rows. Capture rows SHALL remain
**recordPass** and **overdubPass** storage. Edit rows SHALL remain in **`editPasses[]`** and SHALL
use **EditSessionType** to identify the edited domain (**Note** | **ControlChange** | future
**Audio**).

The system SHALL NOT merge **recordPass** and **overdubPass** into a single capturePass type.
The system SHALL NOT split **editPasses[]** into separate note and CC arrays unless a follow-up
change proves payload incompatibility.

#### Scenario: Note edit row is scoped editPass

- **WHEN** **saveNoteEditPass** runs after a delete-note edit
- **THEN** one **editPass** with **EditSessionType::Note** is appended to **`editPasses[]`**
- **AND** **editPassIndex** tags the note-edit batch on that row

#### Scenario: ControlChange edit row is scoped editPass

- **WHEN** **saveControlChangeEditPass** runs after a CC value edit
- **THEN** one **editPass** with **EditSessionType::ControlChange** is appended to **`editPasses[]`**
- **AND** **editPassIndex** tags the control-change edit batch on that row

### Requirement: editPass in passes

Each **saveNoteEditPass** SHALL append one scoped **editPass** with **EditSessionType::Note** to
**`passes.editPasses[]`** only when heap admission succeeds for the appended scoped edit payload.
Each future **saveControlChangeEditPass** SHALL append one scoped **editPass** with
**EditSessionType::ControlChange** under the same admission rule.

An **editPass** SHALL hold **editPassId**, **EditSessionType**, scoped batch index,
**EditPassState**, **EditActionType**, **EditPropertyType**, and a target/payload scoped by
**EditSessionType**.

When heap admission fails, the save call SHALL return **`kInvalidEditPassId`** and SHALL NOT append
a row. The system SHALL NOT use a fixed maximum **editPass** row count as the primary gate.

#### Scenario: saveNoteEditPass after delete when heap allows

- **WHEN** the user completes delete-note and **saveNoteEditPass** runs
- **AND** heap admission succeeds
- **THEN** one **EditSessionType::Note** **editPass** is appended to **`passes.editPasses[]`**
- **AND** capture passes in **`passes[]`** remain chunk-backed

#### Scenario: saveControlChangeEditPass after value update when heap allows

- **WHEN** the user completes a CC value edit and **saveControlChangeEditPass** runs
- **AND** heap admission succeeds
- **THEN** one **EditSessionType::ControlChange** **editPass** is appended to **`passes.editPasses[]`**
- **AND** capture passes in **`passes[]`** remain chunk-backed

#### Scenario: saveNoteEditPass rejected on heap pressure

- **WHEN** internal heap headroom is below the configured reserve plus the estimated cost of the new scoped edit payload
- **THEN** **saveNoteEditPass** returns **`kInvalidEditPassId`**
- **AND** **`passes.editPasses.size()`** is unchanged

#### Scenario: saveNoteEditPass retry after reclaim

- **WHEN** the first **saveNoteEditPass** call fails admission
- **AND** **reclaimUnreferencedDisabledPasses** frees enough heap
- **THEN** a single retry MAY succeed

### Requirement: noteEditPass batch vs editPass row

A **noteEditPass** SHALL denote the open/close boundary in **NoteEditSession**
(**closeNoteEditPass**). Multiple scoped **editPass** rows MAY share one **editPassIndex** when
**EditSessionType::Note**.

A **controlChangeEditPass** SHALL denote the open/close boundary in **ControlChangeEditSession**
(**closeControlChangeEditPass**). Multiple scoped **editPass** rows MAY share one **editPassIndex**
when **EditSessionType::ControlChange**.

#### Scenario: Close disables note editPass ids in batch

- **WHEN** **closeNoteEditPass** runs on note edit exit
- **THEN** **NoteEditPassClosed** undo records all **editPassId** values from that **noteEditPass**
- **AND** undo disables those **editPass** rows in **`passes.editPasses[]`**

#### Scenario: Close disables control-change editPass ids in batch

- **WHEN** **closeControlChangeEditPass** runs on control-change edit exit
- **THEN** **ControlChangeEditPassClosed** undo records all **editPassId** values from that **controlChangeEditPass**
- **AND** undo disables those **editPass** rows in **`passes.editPasses[]`**

### Requirement: Pass-scoped global undo kinds without committed suffix

Global undo SHALL use:

- **RecordPassAdded** — disables one **recordPass** in **`passes[]`** by id,
- **OverdubPassAdded** — disables one **overdubPass** by id,
- **NoteEditPassClosed** — disables all **editPass** ids from one closed **noteEditPass**,
- **ControlChangeEditPassClosed** — disables all **editPass** ids from one closed **controlChangeEditPass**.

The system SHALL NOT use **TakeCommitted**, **NoteEditSessionCommitted**, or `*PassCommitted`
undo kind names. Undo and redo SHALL toggle pass state and SHALL NOT append **EditActionType**
`Create` or `Delete` rows to represent undo history.

#### Scenario: Overdub stop pushes OverdubPassAdded

- **WHEN** overdub stops and capture is committed into **`passes[]`**
- **THEN** **OverdubPassAdded** is pushed on **GlobalUndoStack**

#### Scenario: Note edit exit pushes NoteEditPassClosed

- **WHEN** note edit mode exits and **closeNoteEditPass** runs
- **THEN** **NoteEditPassClosed** is pushed with all **editPassId** values from that **noteEditPass**

#### Scenario: Control-change edit exit pushes ControlChangeEditPassClosed

- **WHEN** control-change edit mode exits and **closeControlChangeEditPass** runs
- **THEN** **ControlChangeEditPassClosed** is pushed with all **editPassId** values from that **controlChangeEditPass**

#### Scenario: Undo disables pass rows without creating edit rows

- **WHEN** global undo applies **NoteEditPassClosed** or **ControlChangeEditPassClosed**
- **THEN** the referenced **editPass** rows become **Disabled**
- **AND** no new **EditActionType::Delete** row is appended

### Requirement: Two-phase pass materialize (phase 1)

**LoopPasses::materialize** SHALL replay in two phases unless a future change supersedes:

1. Merge Active **recordPass** (if present) and Active **overdubPasses** by `mergeSequence`.
2. Apply Active scoped **editPass** entries in storage order, dispatching by **EditSessionType**.

#### Scenario: Materialize matches applyEdits overlay semantics

- **WHEN** native tests run pre-migration **applyEdits** fixture vectors after migration
- **THEN** materialized MIDI events SHALL match prior output byte-for-byte

#### Scenario: Materialize dispatches note and control-change rows

- **WHEN** **`passes.editPasses[]`** contains Active **EditSessionType::Note** and **EditSessionType::ControlChange** rows
- **THEN** materialize applies note rows through the note edit pass handler
- **AND** materialize applies control-change rows through the control-change edit pass handler
- **AND** capture rows remain chunk-backed

### Requirement: editPass op-list payload

Each scoped **editPass** SHALL store one scoped edit payload described by **EditSessionType**,
**EditActionType**, **EditPropertyType**, and a target/payload matching the session type. **editPass**
entries SHALL NOT be stored as capture chunk passes.

The system SHALL NOT use `Parameter` to describe stored edit fields. Stored editable fields SHALL be
described as **EditPropertyType** values. `Parameter` remains reserved for controller/action config.

#### Scenario: editPass holds note scoped payload

- **WHEN** **saveNoteEditPass** appends an **editPass** after a move-note edit
- **THEN** the **editPass** has **EditSessionType::Note**
- **AND** the **editPass** has **EditActionType::Update**
- **AND** the payload references a **NoteRef** target
- **AND** no new capture chunk pass is created

#### Scenario: editPass holds control-change scoped payload

- **WHEN** **saveControlChangeEditPass** appends an **editPass** after a CC value edit
- **THEN** the **editPass** has **EditSessionType::ControlChange**
- **AND** the **editPass** has **EditActionType::Update**
- **AND** the **editPass** has **EditPropertyType::Value**
- **AND** the payload references a **ControlChangeRef** target
- **AND** no new capture chunk pass is created
