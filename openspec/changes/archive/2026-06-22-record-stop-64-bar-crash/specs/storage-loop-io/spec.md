## ADDED Requirements

### Requirement: Long record stop persists before reboot loss
After a successful long record stop finalize, loop persistence SHALL contain the finalized record data so reload after reboot reproduces the recorded loop.

#### Scenario: 64-bar record survives reboot
- **WHEN** a 64-bar record pass is stopped and persistence completes
- **THEN** reloading from SD restores loop length and record content for that slot
- **AND** the restored slot can enter playback without missing the recorded pass

### Requirement: Persist-failure evidence on long stop
When long stop finalize cannot persist loop state, firmware SHALL emit explicit persistence failure evidence.

#### Scenario: Persistence failure is explicit
- **WHEN** stop-path persistence fails during or immediately after long record stop
- **THEN** logs identify the failing persistence step
- **AND** verification marks the run failed with a storage-related reason

### Requirement: Capture pass persistence uses chunk-stream write path
Capture pass persistence SHALL provide a chunk-stream writer path that does not require one full flattened in-memory vector per pass before SD write.

The writer SHALL process capture pass chunk refs in bounded batches. The maximum temporary MIDI event buffer for a capture pass write SHALL be no larger than one loop-event chunk (`LoopEventStoreConfig::CHUNK_CAPACITY`) unless a later spec explicitly changes the bound.

#### Scenario: Long capture pass writes without full pre-flatten buffer
- **WHEN** a long capture pass is persisted to SD
- **THEN** the writer emits pass payload from chunk-backed data in bounded steps
- **AND** persistence does not require creating a full-pass `MidiEventVec` first
- **AND** on-wire persisted content remains compatible with existing read path

#### Scenario: Writer reports bounded memory use
- **WHEN** a 64-bar capture pass is persisted
- **THEN** verification can report pass event count, chunk ref count, and maximum temporary event buffer size
- **AND** maximum temporary event buffer size is bounded by `LoopEventStoreConfig::CHUNK_CAPACITY`
