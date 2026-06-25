## Purpose

Memory-aware global undo depth — prefer 99 entries when affordable; trim under chunk/heap pressure.
Shipped in **pool-budget** group 4 (archived 2026-06-22).

## Requirements

### Requirement: Preferred undo depth

The system SHALL define **`Config::PREFERRED_UNDO_DEPTH`** (default 99) as the target maximum undo
entries per track when memory is not under pressure.

The system SHALL NOT trim undo entries solely because **`entries.size() > 25`**.

#### Scenario: Ninety pass undos retained when memory allows

- **WHEN** a track has 90 lightweight **RecordPassAdded** / **OverdubPassAdded** /
  **NoteEditPassClosed** entries
- **AND** chunk reserve and heap reserve are satisfied
- **THEN** all 90 entries remain in **`GlobalUndoStack`**

### Requirement: Memory-pressure undo trim

The system SHALL trim the oldest undo entries when **`overUndoMemoryPressure`** is true:

- **`freeChunkCount() <= CHUNK_RESERVE`**, or
- **`MemoryMonitor::getFreeHeap() < HEAP_RESERVE_BYTES`**, or
- **`entries.size() > PREFERRED_UNDO_DEPTH`** combined with either condition above

Trimming SHALL continue while pressure remains and **`entries.size() > MIN_UNDO_DEPTH`**.

When **`cursor > 0`**, trimming SHALL remove entries from the undo-history side (index `0`) and
decrement **`cursor`**, preserving the redo branch **`entries[cursor..size)`**.

When **`cursor == 0`**, the full stack is the redo branch. Trimming SHALL NOT remove entries solely
because **`entries.size() > MIN_UNDO_DEPTH`** unless **`ABSOLUTE_MAX_UNDO_ENTRIES`** is exceeded or
**`overUndoMemoryPressure`** is true.

#### Scenario: Trim under chunk pressure

- **WHEN** **`freeChunkCount()`** is at or below **`CHUNK_RESERVE`**
- **AND** **`entries.size() > MIN_UNDO_DEPTH`**
- **THEN** the oldest undo entry is removed
- **AND** **`reclaimUnreferencedDisabledPasses`** runs

#### Scenario: Redo branch preserved after full undo when memory is healthy

- **WHEN** a track has 3 **`OverdubPassAdded`** entries and **`cursor == 0`** (all undos applied)
- **AND** chunk reserve and heap reserve are satisfied
- **AND** **`entries.size() <= ABSOLUTE_MAX_UNDO_ENTRIES`**
- **THEN** **`trimGlobalUndoStackForMemory`** removes zero entries
- **AND** **`redoCount()`** remains 3

#### Scenario: Redo branch cleared on new pass

- **WHEN** the user has undone entries leaving a non-empty redo branch
- **AND** a new capture pass commits (**`pushUndoEntry`**)
- **THEN** entries after **`cursor`** are removed before the new entry is appended
- **AND** **`redoCount()`** is 0

### Requirement: Reclaim after undo stack mutation

The system SHALL invoke **`reclaimUnreferencedDisabledPasses`** after:

- memory-pressure undo trim,
- **`dropRedoBranch`**, and
- **`eraseUndoEntriesForSlot`**.

#### Scenario: Redo branch drop frees orphaned disabled passes

- **WHEN** a new undo action drops the redo branch
- **AND** disabled passes were only pinned by removed redo entries
- **THEN** reclaim removes those pass rows and frees chunks

### Requirement: Absolute undo safety rail

The system SHALL define **`ABSOLUTE_MAX_UNDO_ENTRIES`** as an overflow guard independent of memory
pressure. When **`entries.size()`** exceeds this value, the system SHALL trim oldest entries until
at or below the rail regardless of pressure signals.

#### Scenario: Safety rail prevents unbounded growth

- **WHEN** **`entries.size() > ABSOLUTE_MAX_UNDO_ENTRIES`**
- **THEN** oldest entries are removed until within the rail
