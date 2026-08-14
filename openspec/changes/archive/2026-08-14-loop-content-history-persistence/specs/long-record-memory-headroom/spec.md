## MODIFIED Requirements

### Requirement: Deferred save is bounded and never needs a large RAM2 allocation

Deferred save SHALL persist loop state in bounded incremental slices whose maximum temporary MIDI event buffer is no larger than one loop-event chunk (`LoopEventStoreConfig::CHUNK_CAPACITY`) so it can complete when free RAM2 is low. Runtime save requests SHALL be routed through this central deferred writer rather than direct synchronous SD writes. After Layer A Stage 3, deferred save SHALL NOT include an undo-stack persist stage; undo/redo SHALL NOT be a persisted payload.

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

#### Scenario: Stop persist is not driven by undo entry count

- **WHEN** a loop with many in-session undo entries is persisted after overdub stop
- **THEN** `PERS,bundle` slice count is not proportional to undo entry count
- **AND** no `DeferredSaveStage::UndoStacks` walk runs
