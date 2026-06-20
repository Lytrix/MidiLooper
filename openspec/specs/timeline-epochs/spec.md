## Purpose

Published loop MIDI is stored as **passes** on each `Loop` (**recordPass**, **overdubPasses**,
**editPasses**). Record and overdub use **Capture** until **commitRecordPass** or
**commitOverdubPass** promotes the **pendingCapturePass** into **`passes[]`**. This spec
documents behavior shipped through milestone M7 and the **timeline-pass-model** migration
(supersedes **m8-rename** Take vocabulary).

## Requirements

### Requirement: Published events live in active passes

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

### Requirement: Overdub capture dedupes within active capture only

During overdub, duplicate capture events SHALL be rejected only when an equivalent
event already exists in the **active capture store** for the same tick window.

#### Scenario: Second loop pass does not block overdub against committed record

- **WHEN** overdub runs across multiple loop cycles with repeating phase content
- **THEN** new overdub note-ons at the same loop phase are stored unless already
  captured in the current overdub session's capture store

### Requirement: Stop path uses wrap-window finalize only

Record and overdub stop SHALL run `finalizeLoopAtStop` / `LoopStopFinalize` on the
wrap window only, not full-loop `validateAndCleanupMidiEvents`.

#### Scenario: Full validate is deferred

- **WHEN** record or overdub stops
- **THEN** stop-path validation is limited to the finalize wrap window
- **AND** full validation runs only via idle maintenance or SD load

### Requirement: Undo snapshots are deep-copied on restore

Undo SHALL push O(1) shared store references and MUST `cloneShared()` on restore.

#### Scenario: Overdub undo before clear-slot undo

- **WHEN** the user triggers undo
- **THEN** overdub undo is attempted before clear-slot undo
- **AND** restored state does not share live chunk IDs with the snapshot stack

### Requirement: Global undo records capture pass add

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
