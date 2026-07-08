## ADDED Requirements

### Requirement: Capture append unfrozen on record entry

When **beginCapture(Record)** runs via `startRecording`, the system SHALL clear any prior overdub
freeze state and SHALL set `captureAppendFrozen_` to false on the active loop.

#### Scenario: Record after overdub queue cancelled

- **WHEN** `startRecording` runs after a prior overdub stop was cancelled or aborted
- **THEN** `captureAppendFrozen_` is false
- **AND** `appendCaptureEvent` accepts new events during recording

### Requirement: In-progress capture commit cancellable

An in-progress deferred capture commit SHALL be cancellable through a single lifecycle abort path
(Phase 4: `cancelCaptureCommit` / `resetCaptureCommitSession`).

#### Scenario: Transport stop aborts queued overdub commit

- **WHEN** transport stops while `OverdubStopCommitStage` is not `None`
- **THEN** the in-progress commit session is aborted
- **AND** `captureAppendFrozen_` is cleared

#### Scenario: Clear slot aborts queued commit

- **WHEN** `Track::clear` runs while a deferred commit is in progress
- **THEN** the commit session is aborted before `discardCapture`
