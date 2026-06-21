## ADDED Requirements

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

#### Scenario: Trim under chunk pressure

- **WHEN** **`freeChunkCount()`** is at or below **`CHUNK_RESERVE`**
- **AND** **`entries.size() > MIN_UNDO_DEPTH`**
- **THEN** the oldest undo entry is removed
- **AND** **`reclaimUnreferencedDisabledPasses`** runs

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
