## ADDED Requirements

### Requirement: 64-bar record stop investigation trace
Firmware and HITL verification SHALL emit enough stop-path evidence to identify why long record stop fails before overdub entry.

#### Scenario: Long record stop emits finalize evidence
- **WHEN** a baseline run executes `--record-bars 64 --overdub-bars 64`
- **THEN** the serial capture includes stop-path evidence from record stop through finalize completion
- **AND** the report contains an explicit failure reason when `STOPPED_RECORDING -> PLAYING` does not occur

### Requirement: Stop-path time and memory evidence
Long record stop diagnostics SHALL report time and memory for each memory-heavy stop-path stage without dumping full MIDI event contents.

The stop-path report SHALL include, at minimum:

- stage name,
- elapsed microseconds,
- free heap before and after the stage,
- loop event count or pass event count,
- chunk count or chunk refs count when relevant,
- outcome (`ok`, `skipped`, or explicit failure reason).

Stop-path diagnostics SHALL use O(1) `MemoryMonitor::getFreeHeap()` for heap reporting. They SHALL NOT call `MemoryMonitor::logStatus()`, `MemoryMonitor::getPsramFreeBytes()`, or `MemoryMonitor::getPsramUsedBytes()` on the record-stop or overdub-stop path. Those APIs walk the PSRAM pool and can stall the main loop for hundreds of milliseconds during timing-critical work.

#### Scenario: Long stop report identifies heavy stage
- **WHEN** a 64-bar record stop fails before `STOPPED_RECORDING -> PLAYING`
- **THEN** the report identifies the last completed stage and first missing stage
- **AND** each completed heavy stage includes elapsed microseconds and free-heap delta
- **AND** the report does not require serializing full event lists

#### Scenario: Stop-path markers avoid PSRAM pool walks
- **WHEN** stop-path stage markers are emitted during record stop or overdub stop
- **THEN** heap fields come from `MemoryMonitor::getFreeHeap()` only
- **AND** firmware does not call `logStatus()`, `getPsramFreeBytes()`, or `getPsramUsedBytes()` in that path

### Requirement: Long stop-path regression gate
The baseline runner SHALL fail long-run verification when record stop does not reach `PLAYING` and overdub entry.

#### Scenario: Missing play transition fails gate
- **WHEN** `RECORDING -> STOPPED_RECORDING` occurs but `STOPPED_RECORDING -> PLAYING` is absent
- **THEN** the run is marked failed
- **AND** the report includes transition counts and abort reason for the long-run stop phase

### Requirement: Long-loop display uses bounded piano-roll window
Long-loop display rendering SHALL cap the primary piano-roll window at 16 bars and provide a compact full-loop overview strip across the display width.

The overview strip SHALL be binary in v1: each grouped segment indicates **has notes** or **no notes**. It SHALL also indicate the start and end of the current detailed piano-roll window so the user can see which part of the full loop is zoomed in.

#### Scenario: 64-bar loop remains navigable without full-width piano-roll render
- **WHEN** loop length exceeds 16 bars
- **THEN** the primary piano roll renders at most 16 bars of detailed notes
- **AND** an overview strip spans the full display width and indicates binary grouped note presence across the whole loop
- **AND** group resolution scales with loop length using 1/2/4/8/16/32/64-bar grouping
- **AND** the overview strip marks the current detailed-window start and end positions

#### Scenario: Future zoom controls are not required for v1
- **WHEN** bounded display v1 is implemented
- **THEN** the detailed piano-roll window MAY remain fixed to the active playhead or selected region
- **AND** fader 3 / hold-turn encoder control for changing the detailed-window length is deferred to a follow-up
