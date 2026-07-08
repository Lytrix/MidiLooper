## ADDED Requirements

### Requirement: Capture commit single owner

The system SHALL ensure exactly one runtime component on **Track** owns an in-progress **capture
commit** from the moment recording or overdubbing stops until the loop reaches its next stable
runtime state.

That owner SHALL be responsible for:

- progressing commit stages
- coordinating persistence requests after pass publish
- updating playback state after commit
- updating display state after commit
- emitting commit-stage telemetry
- handling cancellation of an in-progress commit
- reporting commit completion

Whether the request originated from record or overdub SHALL be contextual information only
(**CommitReason**).

#### Scenario: Overdub deferred commit has one owner

- **WHEN** overdub stops and `queueOverdubStopCommit` runs
- **THEN** `processDeferredOverdubStop` (or successor) is the sole owner until commit completes or is cancelled
- **AND** no parallel sync commit path runs for the same capture pass

#### Scenario: Record commit converges on same pipeline

- **WHEN** Phase 3 ships and record stop is queued
- **THEN** record and overdub use the same deferred commit owner and `commitCaptureForStop`
- **AND** record-specific preparation completes before the shared pipeline runs

### Requirement: Capture stop separate from capture commit

The system SHALL treat **capture stop** (user stop request and mode-specific preparation) as
distinct from **capture commit** (flush, seal, publish, finalize, runtime finalization).

#### Scenario: Record stop preparation outside pipeline

- **WHEN** record stops
- **THEN** loop-length quantization, truncation, and empty-slot state advance MAY run in the stop entry path
- **AND** those steps SHALL complete before `commitCaptureForStop` is invoked

#### Scenario: Overdub freeze outside pipeline

- **WHEN** overdub button is held down during overdub
- **THEN** `freezeOverdubCapture` runs in the stop entry path
- **AND** freeze initiation SHALL NOT be owned by `commitCaptureForStop`

### Requirement: Capture commit pipeline boundary

`commitCaptureForStop` SHALL own:

- flushing pending notes into capture
- `commitCapturePass` (seal and publish)
- `finalizeCommitSideEffects`
- post-commit playback cache invalidation
- display notification after commit
- persistence request after pass publish
- commit-stage telemetry

`commitCaptureForStop` SHALL NOT own:

- capture input (`appendCaptureEvent`)
- transport control
- record/overdub button behavior
- quantization policy or loop-length calculation
- freeze initiation
- NOTE_EDIT preparation before commit

#### Scenario: Loop commit authority unchanged

- **WHEN** `commitCaptureForStop` runs
- **THEN** seal and publish occur only through `Loop::commitCapturePass`
- **AND** post-publish side effects occur only through `Track::finalizeCommitSideEffects`

### Requirement: Behavior-preserving extraction Phases 1 and 2

Phases 1 and 2 of this change SHALL be behavior-preserving refactors.

During Phases 1 and 2:

- HITL capture results SHALL remain identical to the pre-change baseline
- telemetry ordering SHALL remain unchanged
- runtime state transitions SHALL remain unchanged
- no optimization or behavioral simplification SHALL occur

Behavioral changes SHALL begin only in Phase 3 when record migrates onto the deferred commit owner.

#### Scenario: Phase 1 native and HITL gate

- **WHEN** Phase 1 ships
- **THEN** `pio test -e native` passes
- **AND** existing HITL baseline produces unchanged pass/fail criteria

### Requirement: Naming glossary

Product code introduced by this change SHALL use the Phase 0 glossary:

| Concept | Function / type (Phase 1–4) | Phase 5 rename |
|---------|----------------------------|----------------|
| Pipeline orchestrator | `commitCaptureForStop` | keep |
| Deferred queue | `queueOverdubStopCommit` | `queueCaptureCommit` |
| Deferred processor | `processDeferredOverdubStop` | `processDeferredCaptureCommit` |
| Commit stage enum | `OverdubStopCommitStage` | `CaptureCommitStage` |
| Abort in-progress commit | `cancelDeferredOverdubStop` | `cancelCaptureCommit` |

The system SHALL NOT introduce `execute*` verbs for capture commit orchestration.

`Loop::commitCapturePass` and `Track::finalizeCommitSideEffects` SHALL NOT be renamed.
