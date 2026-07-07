# persistence-scheduler

Cooperative budget-driven persistence scheduling in the main loop. Owner: `StorageManager`, `main.cpp`.

## ADDED Requirements

### Requirement: Cooperative budget-driven slices

Persistence SHALL be cooperative and budget-driven: each main-loop iteration MAY advance at most one bounded persistence sub-step, then yield to the next iteration.

#### Scenario: Slice yield pattern

- **WHEN** `processDeferredSaveState()` runs in the main loop
- **THEN** it SHALL advance at most one finite-state-machine sub-step per call when budget allows
- **AND** SHALL return control so clock, MIDI, playback, and display run on subsequent iterations

#### Scenario: Active capture budget

- **WHEN** transport is active (`RECORDING` or `OVERDUBBING`) and persistence is enabled (Phase 3+)
- **THEN** each slice SHALL respect `PersistenceBudget::resolveMaxPersistenceMicros()` active budget (~300 µs default)
- **AND** SHALL NOT run unbounded SD work in one iteration

### Requirement: No transport hard block (Phase 3+)

After Phase 3 ships, `isCaptureActiveForPersistence()` SHALL NOT cause an unconditional return that blocks all persistence slices during capture.

Persistence MAY be delayed by budget exhaustion or runtime precedence — not by a blanket transport gate.

#### Scenario: Writer advances during overdub

- **WHEN** a 64-bar overdub is in progress (Phase 4+)
- **AND** sealed chunks are queued
- **THEN** at least one persistence slice SHALL complete during the overdub window in HITL baseline conditions
- **AND** `oldestDirtyChunkAge` SHALL not grow unbounded without diagnostic alarm

### Requirement: Scheduler depends on queue

Budget-driven scheduler changes SHALL NOT ship before persistence queue admission for sealed chunks exists (Phase 2 before Phase 3).

#### Scenario: Phase ordering gate

- **WHEN** implementing transport-gate removal
- **THEN** sealed-chunk queue enqueue on seal SHALL already be implemented and tested

### Requirement: Runtime work first

Main-loop ordering SHALL service MIDI clock, note output, playback, buttons, and display before persistence slices in the same iteration policy as today.

#### Scenario: Playback not starved by SD

- **WHEN** playback is active and persistence queue is non-empty
- **THEN** playback servicing SHALL not be skipped for an unbounded persistence batch
