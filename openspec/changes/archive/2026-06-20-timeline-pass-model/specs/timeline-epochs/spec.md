## MODIFIED Requirements

### Requirement: Published events live in active takes

The system SHALL store loop MIDI from record and overdub as **recordPass** and **overdubPass**
entries in **`passes[]`**, with chunk-backed storage. **recordPass** SHALL appear at most once
per loop. **overdubPass** entries SHALL be ordered by `mergeSequence`. Live capture SHALL use
**Capture** until **commitRecordPass** or **commitOverdubPass** adds the pass to **`passes[]`**.

#### Scenario: Playback reads passes without full flatten

- **WHEN** the transport plays a loop with **recordPass** or **overdubPass** entries
- **THEN** playback uses chunk indexed access via materialized view
- **AND** does not flatten the full loop on each tick

#### Scenario: Record stop adds recordPass to passes

- **WHEN** recording stops with non-empty capture
- **THEN** **commitRecordPass** adds **recordPass** to **`passes[]`**
- **AND** capture buffer is cleared for the next session

### Requirement: Global undo records take commit

The system SHALL push **RecordPassAdded** when a record stop adds **recordPass** to **`passes[]`**
and SHALL push **OverdubPassAdded** when an overdub stop adds **overdubPass** to **`passes[]`**, each
on **GlobalUndoStack**. The system SHALL NOT push **TakeCommitted**.

#### Scenario: Overdub stop pushes OverdubPassAdded

- **WHEN** overdub stops and **commitOverdubPass** adds an **overdubPass** to **`passes[]`**
- **THEN** **OverdubPassAdded** is pushed for that pass
- **AND** undo disables that **overdubPass**

#### Scenario: Record stop pushes RecordPassAdded

- **WHEN** record stops and **commitRecordPass** adds **recordPass** to **`passes[]`**
- **THEN** **RecordPassAdded** is pushed for that pass
- **AND** undo disables that **recordPass**

## REMOVED Requirements

### Requirement: Note edit uses materialized flat bridge until M8

**Reason:** Replaced by **NoteEditSession** + **Edit** in **`passes[]`**.  
**Migration:** **LoopPasses::materialize** replaces `applyEdits(takes, edits)`; **Edit** → **editPass**.

### Requirement: Global undo records take commit with TakeCommitted

**Reason:** **TakeCommitted** removed; use **RecordPassAdded** / **OverdubPassAdded**.  
**Migration:** See `timeline-pass-model` design D1.
