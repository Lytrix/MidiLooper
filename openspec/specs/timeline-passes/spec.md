## Purpose

Loop capture and note-edit history live in **`passes[]`** (**LoopPasses**) per slot: optional
**recordPass**, ordered **overdubPasses**, and **editPasses** with **EditPassType**. Shipped in
**timeline-pass-model** (supersedes **Take** / `takes[]` + **Edit** / `edits[]`).
## Requirements
### Requirement: Loop passes owner

The system SHALL store capture and note-edit history on a loop through **`passes[]`**
(**LoopPasses**). **LoopPasses** SHALL contain:

- zero or one **recordPass**,
- zero or more **overdubPass** entries ordered by `mergeSequence`,
- zero or more **editPass** entries in **`editPasses[]`**, each tagged with **editPassIndex**.

A pass SHALL be described as **pending** only while it is **not** yet in **`passes[]`**. The system
SHALL NOT use **committed** as a pass state label, container name, or pass undo kind suffix.

Architectural **Commit** (making immutable loop state runtime-visible) and APIs such as
`hasCommittedPasses` refer to **committed loop state** / presence of canonical pass content — not a
`PassState::Committed` label on rows in **`passes[]`**.

#### Scenario: Loop exposes passes not takes array

- **WHEN** firmware loads a loop with capture and edit history
- **THEN** `Loop` accesses history through **`passes`**
- **AND** there is no `takes[]` / **Take** type in product code

#### Scenario: Committed-pass presence is not a pass state enum

- **WHEN** a loop has rows in **`passes[]`** after Commit
- **THEN** committed-pass presence APIs may report true
- **AND** pass rows still use existing Active/Disabled (or equivalent) state labels
- **AND** there is no `PassState::Committed` enum value required by this change

### Requirement: Publish vocabulary retired for committed-truth APIs

Product code SHALL use Commit/Committed action+scope names for APIs that describe canonical
runtime-visible pass content (formerly Publish/Published). Guides and active OpenSpec prose for
this concern SHALL prefer **committed passes** / **committed loop state** over “published events”
for pass-level meaning. **Events** remains appropriate only for MIDI gather/range helpers.

#### Scenario: Rename replaces hasPublishedEvents

- **WHEN** the rename pass for this change is complete
- **THEN** `hasPublishedEvents` is not a public Loop API
- **AND** the successor committed-passes presence API is used at former call sites

### Requirement: Four pass kinds two storage families

The system SHALL support capture pass rows and scoped edit pass rows. Capture rows SHALL remain
**recordPass** and **overdubPass** storage. Edit rows SHALL remain in **`editPasses[]`** and SHALL
use **EditPassType** to identify the edited domain (**Note** | **ControlChange** | future
**Audio**).

The system SHALL NOT merge **recordPass** and **overdubPass** into a single capturePass type.
The system SHALL NOT split **editPasses[]** into separate note and CC arrays unless a follow-up
change proves payload incompatibility.

#### Scenario: Note edit row is scoped editPass

- **WHEN** **saveNoteEditPass** runs after a delete-note edit
- **THEN** one **editPass** with **EditPassType::Note** is appended to **`editPasses[]`**
- **AND** **editPassIndex** tags the note-edit batch on that row

#### Scenario: ControlChange edit row is scoped editPass

- **WHEN** **saveControlChangeEditPass** runs after a CC value edit
- **THEN** one **editPass** with **EditPassType::ControlChange** is appended to **`editPasses[]`**
- **AND** **editPassIndex** tags the control-change edit batch on that row

### Requirement: editPass in passes

Each **saveNoteEditPass** SHALL append one scoped **editPass** with **EditPassType::Note** to
**`passes.editPasses[]`** only when heap admission succeeds for the appended row payload.
Each future **saveControlChangeEditPass** SHALL append one scoped **editPass** with
**EditPassType::ControlChange** under the same admission rule.

An **editPass** SHALL hold **editPassId**, **EditPassType**, **editPassIndex**, **EditPassState**,
**EditActionType**, **EditPropertyType**, and row fields scoped by **EditPassType**.

When heap admission fails, the save call SHALL return **`kInvalidEditPassId`** and SHALL NOT append
a row. The system SHALL NOT use a fixed maximum **editPass** row count as the primary gate.

#### Scenario: saveNoteEditPass after delete when heap allows

- **WHEN** the user completes delete-note and **saveNoteEditPass** runs
- **AND** heap admission succeeds
- **THEN** one **EditPassType::Note** **editPass** is appended to **`passes.editPasses[]`**
- **AND** capture passes in **`passes[]`** remain chunk-backed

#### Scenario: saveControlChangeEditPass after value update when heap allows

- **WHEN** the user completes a CC value edit and **saveControlChangeEditPass** runs
- **AND** heap admission succeeds
- **THEN** one **EditPassType::ControlChange** **editPass** is appended to **`passes.editPasses[]`**
- **AND** capture passes in **`passes[]`** remain chunk-backed

#### Scenario: saveNoteEditPass rejected on heap pressure

- **WHEN** internal heap headroom is below the configured reserve plus the estimated cost of the new row payload
- **THEN** **saveNoteEditPass** returns **`kInvalidEditPassId`**
- **AND** **`passes.editPasses.size()`** is unchanged

#### Scenario: saveNoteEditPass retry after reclaim

- **WHEN** the first **saveNoteEditPass** call fails admission
- **AND** **reclaimUnreferencedDisabledPasses** frees enough heap
- **THEN** a single retry MAY succeed

### Requirement: Capture pass admission by chunk pool

The system SHALL NOT refuse **commitCapturePass** / **sealCapture** based on a fixed maximum
**capturePassCount** per slot. Capture admission SHALL be governed by **loop-event-pool-admission**
and **pass-reclaim**.

#### Scenario: Seal succeeds with high capture pass count

- **WHEN** a slot has more than 25 capture pass rows
- **AND** chunk admission with reserve succeeds
- **THEN** a new **overdubPass** or **recordPass** MAY be published

### Requirement: noteEditPass batch vs editPass row

A **noteEditPass** SHALL denote the open/close boundary in **NoteEditSession**
(**closeNoteEditPass**). Multiple scoped **editPass** rows MAY share one **editPassIndex** when
**EditPassType::Note**.

A **controlChangeEditPass** SHALL denote the open/close boundary in **ControlChangeEditSession**
(**closeControlChangeEditPass**). Multiple scoped **editPass** rows MAY share one **editPassIndex**
when **EditPassType::ControlChange**.

**saveNoteEditPass** SHALL remain the merge boundary into **`passes.editPasses[]`**.

Leaving NOTE_EDIT via **exitEditMode** SHALL evaluate pending NOTE_EDIT actions and, when edit
changes exist, SHALL append scoped **editPass** rows before **closeNoteEditPass**.

**cycleEditSession** SHALL toggle **EditSessionType** (`Loop` ↔ `Note`) only and SHALL NOT run
**closeNoteEditPass** from the toggle alone.

#### Scenario: Full NOTE_EDIT exit appends and closes pass

- **WHEN** NOTE_EDIT leaves through **exitEditMode**
- **AND** pending note-edit changes exist
- **THEN** **saveNoteEditPass** appends one or more rows to **`passes.editPasses[]`**
- **AND** **closeNoteEditPass** emits **NoteEditPassClosed** for that closed batch

#### Scenario: NOTE_EDIT session toggle does not close pass

- **WHEN** NOTE_EDIT transitions through **cycleEditSession**
- **THEN** **EditSessionType** toggles between **Loop** and **Note**
- **AND** **closeNoteEditPass** does not run from the toggle alone

#### Scenario: Close disables note editPass ids in batch

- **WHEN** **closeNoteEditPass** runs on note edit exit
- **THEN** **NoteEditPassClosed** undo records all **editPassId** values from that **noteEditPass**
- **AND** undo disables those **editPass** rows in **`passes.editPasses[]`**

#### Scenario: Close disables control-change editPass ids in batch

- **WHEN** **closeControlChangeEditPass** runs on control-change edit exit
- **THEN** **ControlChangeEditPassClosed** undo records all **editPassId** values from that **controlChangeEditPass**
- **AND** undo disables those **editPass** rows in **`passes.editPasses[]`**

### Requirement: Deferred save boundary after note edit exit

NOTE_EDIT exit persistence SHALL use deferred save handoff. Runtime behavior SHALL preserve MIDI
timing during playback by avoiding blocking save work in playback hot paths.

The system SHALL provide evidence markers from save request to deferred-save completion for
NOTE_EDIT exit persistence.

#### Scenario: Exit triggers deferred save request and completion markers

- **WHEN** NOTE_EDIT exits with `isEditStateDirty() == true`
- **THEN** the serial trace includes deferred save request markers (`PERS,request`)
- **AND** completion markers include `PERS,result,ok` when deferred save completes

#### Scenario: Replay checks after exit align with persisted boundary

- **WHEN** NOTE_EDIT workflows perform in-edit undo/redo and post-exit undo/redo
- **THEN** marker sequence includes **NoteEditPassClosed** and scoped post-exit undo/redo markers
- **AND** replay verification does not report missing insertions caused by exit persistence boundary loss

### Requirement: Pending capture pass

While the user captures a record or overdub pass, the system SHALL hold a **pendingCapturePass**
outside **`passes[]`**. **Capture** SHALL append MIDI until seal; **commitRecordPass** or
**commitOverdubPass** SHALL move the pass into **`passes[]`**.

#### Scenario: Capture pending until commit

- **WHEN** record is in progress
- **THEN** MIDI appends through **Capture**
- **AND** the pass is **pending** (not yet in **`passes[]`**)

#### Scenario: Commit moves pass into passes

- **WHEN** record stops with non-empty capture and commit succeeds
- **THEN** a **recordPass** entry exists in **`passes[]`**
- **AND** **pendingCapturePass** is cleared

### Requirement: recordPass cardinality

The system SHALL allow at most one **recordPass** in **`passes[]`** per loop slot. **recordPass**
MAY be absent (empty loop bootstrap — product TBD).

#### Scenario: Empty loop without recordPass

- **WHEN** a loop has `loopLengthTicks > 0` and no **recordPass** (future product)
- **THEN** **`passes[]`** MAY contain only **editPass** entries and/or **overdubPass** entries
- **AND** materialize SHALL produce playback from those layers only

### Requirement: overdubPass ordering

Each **overdubPass** in **`passes[]`** SHALL carry a `mergeSequence` used with **recordPass**
(when present) to order capture layers during materialize.

#### Scenario: Multiple overdub passes merge in sequence

- **WHEN** a loop has one **recordPass** and two Active **overdubPass** entries
- **THEN** materialize SHALL include **recordPass** chunks first
- **AND** SHALL merge **overdubPass** entries in ascending `mergeSequence` before applying **editPass** entries

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

### Requirement: Overdub session wrap is not a pass boundary

A loop wrap during an active overdub capture session SHALL NOT create a new `overdubPass` row and SHALL NOT create a new overdub undo unit. Pass and undo creation for overdub remain gated on successful `commitCapturePass()` at overdub stop for that session. Multiple wraps MAY occur within one pending capture before commit.

#### Scenario: Wrap mid-overdub keeps one pending session

- **GIVEN** an overdub session is open with a pending capture pass
- **WHEN** the playhead wraps the loop one or more times before stop
- **THEN** no additional `overdubPass` is committed at wrap
- **AND** no additional overdub undo unit is created at wrap
- **AND** stop plus successful `commitCapturePass` still yields one `overdubPass` and one overdub undo for the session

### Requirement: Two-phase pass materialize (phase 1)

**LoopPasses::materialize** SHALL replay in two phases unless a future change supersedes:

1. Merge Active **recordPass** (if present) and Active **overdubPasses** by `mergeSequence`.
2. Apply Active scoped **editPass** entries in storage order, dispatching by **EditPassType**.

#### Scenario: Materialize matches applyEdits overlay semantics

- **WHEN** native tests run pre-migration **applyEdits** fixture vectors after migration
- **THEN** materialized MIDI events SHALL match prior output byte-for-byte

#### Scenario: Materialize dispatches note and control-change rows

- **WHEN** **`passes.editPasses[]`** contains Active **EditPassType::Note** and **EditPassType::ControlChange** rows
- **THEN** materialize applies note rows through **applyNoteEditPass**
- **AND** materialize applies control-change rows through the control-change edit pass handler
- **AND** capture rows remain chunk-backed

### Requirement: Note editPass stored as targetNoteId and MIDI fields

Note **editPass** rows SHALL store **EditPassType**, **EditActionType**, **EditPropertyType**, a
**`targetNoteId`** (**NoteId**), and the MIDI note fields required for that property:

- **Create:** **note on** + **note off** events (note-on carries assigned **noteId**)
- **Delete:** **targetNoteId** only
- **NoteRange** (move): **targetNoteId**, `startTick`, `endTick`
- **Length:** **targetNoteId**, `startTick`, `endTick` (start-point length edit may use `startTick` later)
- **Pitch:** **targetNoteId**, pitch
- **Velocity:** **targetNoteId**, **note on** velocity

Firmware SHALL NOT use a generic **payload** blob or legacy **EditChange** lists on new writes.

#### Scenario: Length row stores full tick span

- **WHEN** a length edit is committed
- **THEN** the row has **propertyType = Length** with `startTick` and `endTick`
- **AND** apply uses length/overlap semantics (not move semantics)

#### Scenario: Length row ready for start-point edit

- **WHEN** only the note end changes today
- **THEN** `endTick` reflects the new end and `startTick` is still stored on the row
- **AND** a future start-point length UI can change `startTick` without a storage format change

#### Scenario: Move row uses NoteRange

- **WHEN** a move edit is committed
- **THEN** the row has **propertyType = NoteRange** with `startTick` and `endTick`
- **AND** apply uses move semantics (distinct from **Length**)

#### Scenario: v4 state file rejected on load

- **WHEN** SD contains storage version 1–4
- **THEN** **loadState** returns **false**
- **AND** firmware runs with default empty in-RAM state

#### Scenario: v6 save writes canonical edit rows with targetNoteId

- **WHEN** a loop with edits is saved under v6
- **THEN** each **editPass** row on disk uses **EditPassType**, **`targetNoteId`**, and MIDI field columns only
- **AND** no **EditChange** blobs are written

#### Scenario: v6 load reads canonical edit rows with targetNoteId

- **WHEN** a v6 state file is loaded
- **THEN** **readPersistedEditsTail** parses **targetNoteId** on each note row
- **AND** materialize matches the saved loop

