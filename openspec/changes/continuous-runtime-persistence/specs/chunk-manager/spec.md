# chunk-manager

Runtime chunk ownership via ChunkManager (`LoopEventStore`, target). Lifecycle is internal to ChunkManager — not a standalone persistence state.

## ADDED Requirements

### Requirement: Single mutable recording chunk

Capture SHALL own exactly one mutable recording chunk per active capture store.

#### Scenario: One tail chunk during record

- **WHEN** a track is recording or overdubbing
- **THEN** capture append SHALL target exactly one `Recording` tail chunk
- **AND** no other chunk in that capture store SHALL accept append writes

### Requirement: Sealed chunk immutability

A sealed chunk SHALL be immutable — no capture or edit path may mutate its events after seal.

#### Scenario: Seal on capacity

- **WHEN** the recording tail chunk reaches `LoopEventStoreConfig::CHUNK_CAPACITY`
- **THEN** ChunkManager SHALL transition that chunk to `Sealed`
- **AND** capture SHALL allocate a new `Recording` tail chunk for subsequent events

#### Scenario: Seal on pass close

- **WHEN** `Loop::sealCapture()` closes a capture pass
- **THEN** the final tail chunk SHALL transition to `Sealed` if it contains events
- **AND** sealed chunks SHALL NOT accept further appends

### Requirement: ChunkManager ownership

ChunkManager (`LoopEventStore`, target) SHALL own pool chunk allocation, sealing, reference tracking, and reclamation when all runtime owners release references.

ChunkManager SHALL NOT own persistence queue admission, SD writes, or pass timeline membership.

#### Scenario: Reclaim after refs released

- **WHEN** a sealed chunk is persisted
- **AND** every runtime owner (`LoopPasses`, playback, undo) has released its reference
- **THEN** ChunkManager MAY return the chunk to the free pool

#### Scenario: ChunkManager does not schedule SD

- **WHEN** a chunk transitions to `Sealed`
- **THEN** ChunkManager SHALL NOT write to SD directly
- **AND** persistence queue admission SHALL be a separate subsystem responsibility

### Requirement: Runtime lifecycle states

Runtime chunk states SHALL be `Free`, `Recording`, or `Sealed`. These states SHALL NOT encode persistence progress (`Queued`, `Writing`, `Persisted`).

#### Scenario: Runtime state independent of SD

- **WHEN** a chunk is `Sealed` in RAM
- **THEN** its persistence state MAY be `Not scheduled`, `Queued`, `Writing`, or `Persisted` independently
