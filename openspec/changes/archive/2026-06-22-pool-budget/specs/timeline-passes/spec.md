## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: editPasses cardinality cap

**Reason**: Replaced by heap admission and **pass-reclaim** in **pool-budget**. Fixed row counts
block valid sessions when memory is available and do not free memory when rows are disabled.

## ADDED Requirements

### Requirement: Capture pass admission by chunk pool

The system SHALL NOT refuse **commitCapturePass** / **sealCapture** based on a fixed maximum
**capturePassCount** per slot. Capture admission SHALL be governed by **loop-event-pool-admission**
and **pass-reclaim**.

#### Scenario: Seal succeeds with high capture pass count

- **WHEN** a slot has more than 25 capture pass rows
- **AND** chunk admission with reserve succeeds
- **THEN** a new **overdubPass** or **recordPass** MAY be published
