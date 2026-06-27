## MODIFIED Requirements

### Requirement: Deferred save is bounded and never needs a large RAM2 allocation

Deferred save SHALL persist loop state in bounded incremental slices whose maximum temporary MIDI
event buffer is no larger than one loop-event chunk (`LoopEventStoreConfig::CHUNK_CAPACITY`),
including the undo-stack stage, so it can complete when free RAM2 is low. Runtime save requests
SHALL be routed through this central deferred writer rather than direct synchronous SD writes.

In v6, deferred save slices SHALL target **CurrentSet** files (`MidiLooper/current/workspace.bin` and
`Sets/_current/loop_TT_SS.bin`) instead of a single monolith byte stream. Slice boundaries and
admission rules are unchanged; only write targets change.

#### Scenario: 64-bar save completes under low RAM2

- **WHEN** a 64-bar loop is saved via deferred writer after record stop
- **AND** free RAM2 is above the safety floor at save start
- **THEN** persistence emits `PERS,result,...,ok`
- **AND** `Sets/_current/loop_TT_SS.bin` for that slot contains the recorded pass

#### Scenario: One slot per deferred slice

- **WHEN** `processDeferredSaveState` advances loop pool stage
- **THEN** at most one CurrentSet slot file write step completes per main-loop call
- **AND** capture-active gating still prevents save during RECORDING or OVERDUBBING

#### Scenario: Display skips during CurrentSet write

- **WHEN** `isDeferredSaveActive()` is true during CurrentSet file write
- **THEN** OLED updates that compete with SD I/O are skipped
- **AND** MIDI clock and playback are unaffected
