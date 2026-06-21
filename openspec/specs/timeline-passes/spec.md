## Purpose

Loop capture and note-edit history live in **`passes[]`** (**LoopPasses**) per slot: optional
**recordPass**, ordered **overdubPasses**, and **editPasses** with **EditPassKind**. Shipped in
**timeline-pass-model** (supersedes **Take** / `takes[]` + **Edit** / `edits[]`).

## Requirements

### Requirement: Loop passes owner

The system SHALL store capture and note-edit history on a loop through **`passes[]`**
(**LoopPasses**). **LoopPasses** SHALL contain:

- zero or one **recordPass**,
- zero or more **overdubPass** entries ordered by `mergeSequence`,
- zero or more **editPass** entries in **`editPasses[]`**, each tagged with **noteEditPassIndex**.

A pass SHALL be described as **pending** only while it is **not** yet in **`passes[]`**. The system
SHALL NOT use **committed** as a pass state label, container name, or pass undo kind suffix.

#### Scenario: Loop exposes passes not takes array

- **WHEN** firmware loads a loop with capture and edit history
- **THEN** `Loop` accesses history through **`passes`**
- **AND** there is no `takes[]` / **Take** type in product code

### Requirement: Four pass kinds two storage families

The system SHALL support four pass **kinds**: **recordPass**, **overdubPass**, **noteEditPass**,
and **controlChangeEditPass**. Capture kinds SHALL use chunk-backed **recordPass** and
**overdubPass** storage. Edit kinds SHALL use **editPass** rows in **`editPasses[]`** with
**EditPassKind** discriminant (**NoteEdit** | **ControlChange**).

The system SHALL NOT merge **recordPass** and **overdubPass** into a single capturePass type.
The system SHALL NOT split **editPasses[]** into separate note and CC arrays unless a follow-up
change proves payload incompatibility.

#### Scenario: Note edit row is editPass with NoteEdit kind

- **WHEN** **saveNoteEditPass** runs after a delete-note edit
- **THEN** one **editPass** with **EditPassKind::NoteEdit** is appended to **`editPasses[]`**
- **AND** **noteEditPassIndex** tags the batch on that row

#### Scenario: ControlChange kind reserved

- **WHEN** firmware loads a loop with no CC edit feature enabled
- **THEN** **`editPasses[]`** MAY contain only **EditPassKind::NoteEdit** rows
- **AND** materialize SHALL ignore or no-op **EditPassKind::ControlChange** until CC edit ships

### Requirement: editPass in passes

Each **saveNoteEditPass** SHALL append one **editPass** with **EditPassKind::NoteEdit** to
**`passes.editPasses[]`** only when **heap admission** succeeds for the appended **EditChange** list.
An **editPass** SHALL hold **editPassId**, **EditPassKind**, batch index, and an **EditChange** list.

When heap admission fails, **saveNoteEditPass** SHALL return **`kInvalidEditPassId`** and SHALL NOT
append a row. The system SHALL NOT use a fixed maximum **editPass** row count as the primary gate.

#### Scenario: saveNoteEditPass after delete when heap allows

- **WHEN** the user completes delete-note and **saveNoteEditPass** runs
- **AND** heap admission succeeds
- **THEN** one **EditPassKind::NoteEdit** **editPass** is appended to **`passes.editPasses[]`**
- **AND** capture passes in **`passes[]`** remain chunk-backed

#### Scenario: saveNoteEditPass rejected on heap pressure

- **WHEN** **`MemoryMonitor::getFreeHeap()`** is below **`HEAP_RESERVE_BYTES`** plus the estimated
  cost of the new **EditChange** list
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

A **noteEditPass** SHALL denote the open/close boundary in **NoteEditSession** (**closeNoteEditPass**).
Multiple **editPass** rows MAY share one **noteEditPassIndex** within the same closed batch.

#### Scenario: Close disables editPass ids in batch

- **WHEN** **closeNoteEditPass** runs on note edit exit
- **THEN** **NoteEditPassClosed** undo records all **editPassId** values from that **noteEditPass**
- **AND** undo disables those **editPass** rows in **`passes.editPasses[]`**

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
- **NoteEditPassClosed** — disables all **editPass** ids from one closed **noteEditPass**.

The system SHALL NOT use **TakeCommitted**, **NoteEditSessionCommitted**, or `*PassCommitted`
undo kind names.

#### Scenario: Overdub stop pushes OverdubPassAdded

- **WHEN** overdub stops and capture is committed into **`passes[]`**
- **THEN** **OverdubPassAdded** is pushed on **GlobalUndoStack**

#### Scenario: Note edit exit pushes NoteEditPassClosed

- **WHEN** note edit mode exits and **closeNoteEditPass** runs
- **THEN** **NoteEditPassClosed** is pushed with all **editPassId** values from that **noteEditPass**

### Requirement: Two-phase pass materialize (phase 1)

**LoopPasses::materialize** SHALL replay in two phases unless a future change supersedes:

1. Merge Active **recordPass** (if present) and Active **overdubPasses** by `mergeSequence`.
2. Apply Active **editPass** entries in storage order.

#### Scenario: Materialize matches applyEdits overlay semantics

- **WHEN** native tests run pre-migration **applyEdits** fixture vectors after migration
- **THEN** materialized MIDI events SHALL match prior output byte-for-byte

### Requirement: editPass op-list payload

Each **editPass** SHALL store an ordered **EditChange** list. **editPass** entries SHALL NOT
be stored as capture chunk passes.

#### Scenario: editPass holds EditChange list

- **WHEN** **saveNoteEditPass** appends an **editPass** after a move-note edit
- **THEN** the **editPass** contains one or more **EditChange** entries with **NoteRef** targets
- **AND** no new capture chunk pass is created
