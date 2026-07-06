# long-record-memory-headroom

Long record/overdub passes must complete stop, persistence, and reload without RAM2 exhaustion or USB disconnect.

## Requirements

### Requirement: Length-scaling buffers reside in PSRAM first
Length-scaling, non-time-critical buffers SHALL be allocated PSRAM-first so that record length does not consume the RAM2 `malloc` heap before the PSRAM pool is used.

This applies at minimum to the per-loop note cache, the per-loop playback order vector, `materializeToFlat` output buffers, undo snapshot length-scaling vectors, **NoteEditFocus** maps, **NoteEditSessionUndoStack** entries, session flat cache, **DisplayManager** live display event buffer, **MemoryPool::globalMidiEventPool**, and UIP **`buildCanonicalSpansFromMidi` / `IntervalProjection`** batch temporaries. The event chunk pool remains PSRAM-first (unchanged). Time-critical and small fixed-size state MAY remain in fast RAM. Routing detail: [`internal-heap-external-memory-routing`](../internal-heap-external-memory-routing/spec.md), guide [`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../../../docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md).

#### Scenario: 64-bar record keeps RAM2 headroom
- **WHEN** a record pass grows to 64 bars
- **THEN** `MemoryMonitor::getFreeHeap()` at record-stop entry remains above the configured RAM2 safety floor
- **AND** the length-scaling buffers report PSRAM placement

#### Scenario: Hot path stays in fast RAM
- **WHEN** length-scaling buffers are re-targeted to PSRAM
- **THEN** MIDI clock, note-out, and playback servicing are not moved to PSRAM-resident hot buffers solely as a side effect
- **AND** HITL first-note timing thresholds for record and overdub remain within their existing limits

### Requirement: RAM2 safety-floor admission guard
Growth-heavy and persistence operations SHALL consult a RAM2 free-heap safety floor (`MemoryMonitor::getFreeHeap()`) before allocating, and SHALL defer rather than exhaust RAM2 when below the floor. The MIDI clock, note-out, and playback path SHALL be exempt and never blocked by this guard.

#### Scenario: Below-floor non-critical work defers
- **WHEN** free RAM2 is below the safety floor and a non-time-critical growth or persistence step is requested
- **THEN** the step defers and retries on a later idle iteration
- **AND** the firmware does not reach the allocator's both-exhausted `abort()` path

#### Scenario: Time-sensitive data is never blocked by the guard
- **WHEN** free RAM2 is below the safety floor
- **THEN** MIDI clock handling, note output, and loop playback continue without being gated by the guard

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
