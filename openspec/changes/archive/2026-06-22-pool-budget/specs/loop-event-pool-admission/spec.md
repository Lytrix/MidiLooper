## ADDED Requirements

### Requirement: O(1) chunk pool stats

The system SHALL expose **`LoopEventStore::usedChunkCount`** and **`LoopEventStore::freeChunkCount`**
derived from the global chunk pool bitmap without walking PSRAM allocators.

#### Scenario: Stats match alloc state

- **WHEN** the global pool has N chunks marked in use
- **THEN** **`usedChunkCount()`** returns N
- **AND** **`freeChunkCount()`** returns **`POOL_CHUNK_COUNT - N`**

### Requirement: Chunk reserve for admission

The system SHALL define **`PassConfig::CHUNK_RESERVE`** as a minimum number of free chunks that
capture seal SHALL treat as unavailable for new capture pass chunks.

**`LoopEventStore::canAllocChunkWithReserve`** SHALL return true only when
**`freeChunkCount() > CHUNK_RESERVE`**.

#### Scenario: Seal blocked when reserve violated

- **WHEN** **`freeChunkCount()`** is at or below **`CHUNK_RESERVE`**
- **AND** **`sealCapture`** needs a new chunk
- **THEN** seal returns **`SealOutcome::PoolExhausted`**
- **AND** no new capture pass is published

### Requirement: PoolExhausted replaces AtPassCap

The system SHALL NOT reject **`sealCapture`** based solely on **`capturePassCount()`** or a fixed
maximum capture pass row count.

**`SealOutcome::AtPassCap`** SHALL be removed or renamed to **`SealOutcome::PoolExhausted`** for
chunk pool exhaustion.

#### Scenario: Many small overdubs allowed when chunks available

- **WHEN** **`capturePassCount()`** exceeds 25
- **AND** **`canAllocChunkWithReserve()`** is true
- **THEN** **`sealCapture`** MAY succeed
- **AND** a new **overdubPass** is published
