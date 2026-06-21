## Purpose

Reclaim **Disabled** pass rows and free chunk refs when no **GlobalUndoStack** entry pins them.
Shipped in **pool-budget** groups 3–5 (archived 2026-06-22).

## Requirements

### Requirement: Reclaim unreferenced disabled capture passes

The system SHALL free chunk refs and remove **Disabled** **recordPass** / **overdubPass** rows when
their **passId** is not referenced by any remaining **`GlobalUndoStack`** entry on any track.

Reclaim SHALL use the same chunk release pattern as **`Loop::freeActiveCapturePassChunks`**
(staging **`LoopEventStore`** adopt → **`clear()`**).

#### Scenario: Disabled overdub reclaimed after undo trim

- **WHEN** an **OverdubPassAdded** undo entry is trimmed from the oldest position
- **AND** the corresponding **overdubPass** is **Disabled**
- **AND** no other undo entry references that **passId**
- **THEN** **`reclaimUnreferencedDisabledPasses`** frees its chunk refs
- **AND** removes the **overdubPass** row

#### Scenario: Active pass never reclaimed

- **WHEN** a capture pass is **Active**
- **THEN** reclaim SHALL NOT free its chunks or remove its row

### Requirement: Reclaim unreferenced disabled editPasses

The system SHALL remove **Disabled** **editPass** rows from **`passes.editPasses[]`** when their
**editPassId** is not referenced by any remaining **`GlobalUndoStack`** entry.

#### Scenario: Disabled editPass reclaimed after batch undo trim

- **WHEN** a **NoteEditPassClosed** entry is trimmed
- **AND** referenced **editPass** rows are **Disabled**
- **AND** no remaining undo entry lists those ids
- **THEN** those **editPass** rows are erased from **`passes.editPasses[]`**

### Requirement: ClearSlot snapshot release on undo trim

When a **ClearSlot** undo entry is removed from **`GlobalUndoStack`**, the system SHALL release
**`beforeSnapshot`** and **`afterSnapshot`** shared payloads so deep-cloned chunk copies may be freed.

#### Scenario: Snapshot memory freed after oldest clear undo dropped

- **WHEN** the oldest **ClearSlot** entry is trimmed under memory pressure
- **THEN** its snapshot **`shared_ptr`** payloads are destroyed
- **AND** reclaim runs for passes no longer pinned by that entry

### Requirement: Reference collection before reclaim

**`collectReferencedPasses`** SHALL scan all tracks' **`GlobalUndoStack::entries`** and pin:

- **passId** for **RecordPassAdded** and **OverdubPassAdded**
- each id in **noteEditPassIds** for **NoteEditPassClosed**
- all pass ids and chunk refs held by **ClearSlot** **beforeSnapshot** / **afterSnapshot**

#### Scenario: Redo branch still pins disabled pass

- **WHEN** a pass is **Disabled** by undo
- **AND** an undo entry referencing its **passId** remains in the stack (including redo branch)
- **THEN** reclaim SHALL NOT free that pass

### Requirement: TrackManager orchestration

**`TrackManager::reclaimUnreferencedDisabledPasses`** SHALL rebuild the reference set and invoke
per-slot reclaim on every track and slot.

#### Scenario: Idle reclaim runs when transport not timing-critical

- **WHEN** no track is playing, recording, or overdubbing
- **THEN** **`main.cpp`** SHALL call **`reclaimUnreferencedDisabledPasses`**
