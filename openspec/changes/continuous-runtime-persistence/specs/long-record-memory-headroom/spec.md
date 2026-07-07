## MODIFIED Requirements

### Requirement: Long record does not crash or drop USB serial

Recording well past 32 bars and stopping SHALL complete without a firmware crash or USB serial disconnect, and SHALL preserve the recorded pass.

#### Scenario: 48-bar record-only completes cleanly

- **WHEN** a 48-bar record-only run is stopped
- **THEN** USB serial remains connected through stop and persistence
- **AND** persistence emits a success result (`PERS,result,...,ok`)

#### Scenario: 64+64 baseline reaches overdub and persists

- **WHEN** a 64-bar record is stopped and a 64-bar overdub is requested
- **THEN** transitions include `STOPPED_RECORDING -> PLAYING` and `PLAYING -> OVERDUBBING`
- **AND** persistence emits a success result and the loop reloads after a reboot simulation

#### Scenario: Persistence progresses during overdub (Phase 4+)

- **WHEN** continuous runtime persistence is enabled (Phase 4+)
- **AND** a 64-bar overdub follows a 64-bar record on HITL track 2 / slot 1
- **THEN** at least one persistence slice SHALL complete during the overdub window
- **AND** `PERS,result,...,ok` SHALL appear after overdub stop without USB disconnect
- **AND** `freeChunkCount` SHALL remain above `CHUNK_RESERVE` through the 64+64 run

### Requirement: Deferred save is bounded and never needs a large RAM2 allocation

Deferred save SHALL persist loop state in bounded incremental slices whose maximum temporary MIDI event buffer is no larger than one loop-event chunk (`LoopEventStoreConfig::CHUNK_CAPACITY`), including the undo-stack stage, so it can complete when free RAM2 is low. Runtime save requests SHALL be routed through this central deferred writer rather than direct synchronous SD writes.

#### Scenario: 64-bar save completes under low RAM2

- **WHEN** a 64-bar loop is persisted while free RAM2 is at the safety floor
- **THEN** the deferred save runs to completion across idle iterations
- **AND** the maximum temporary event buffer used is bounded by `LoopEventStoreConfig::CHUNK_CAPACITY`

#### Scenario: Save yields to time-sensitive work

- **WHEN** deferred save is in progress and playback is active
- **THEN** each main-loop iteration services MIDI clock, note-out, and playback before advancing at most one persistence slice
- **AND** save resumes on the next idle iteration without restarting from the beginning

#### Scenario: Runtime save call sites do not write synchronously

- **WHEN** runtime interactions such as record stop, overdub stop, undo/redo, loop edit debounce, clear track, edit autosave, or clock-source transition request persistence
- **THEN** they enqueue deferred save work
- **AND** they do not call the synchronous full `saveState()` path from the runtime interaction

#### Scenario: Save not starved for entire capture (Phase 3+)

- **WHEN** continuous runtime persistence scheduler is enabled (Phase 3+)
- **AND** a deferred save is pending during `RECORDING` or `OVERDUBBING`
- **THEN** persistence SHALL NOT be blocked for the entire capture window by a transport-only gate
- **AND** slice progress SHALL be observable in serial (`PERS,slice` or Phase 0 diagnostic equivalents)
