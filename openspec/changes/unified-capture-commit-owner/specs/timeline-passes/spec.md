## ADDED Requirements

### Requirement: Capture commit at stop

When a capture pass closes at record or overdub stop, the system SHALL publish the pass through
a single capture commit pipeline on **Track** (`commitCaptureForStop`), after mode-specific stop
preparation completes.

The system SHALL NOT run duplicate parallel commit paths (sync record inline + deferred overdub
FSM) for the same active capture pass.

#### Scenario: Record stop publishes through commit pipeline

- **WHEN** `stopRecording` or `stopRecordingToStopped` completes pass publish
- **THEN** flush, seal, and finalize occur through `commitCaptureForStop` (Phase 1+)
- **AND** `CommitReason` reflects record stop variant

#### Scenario: Overdub stop publishes through commit pipeline

- **WHEN** overdub stop commit completes
- **THEN** flush, seal, and finalize occur through `commitCaptureForStop`
- **AND** published chunks live under `passes.overdubPasses[]` as today
