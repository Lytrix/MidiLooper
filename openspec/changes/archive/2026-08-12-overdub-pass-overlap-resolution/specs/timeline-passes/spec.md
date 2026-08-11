## ADDED Requirements

### Requirement: Overdub session wrap is not a pass boundary

A loop wrap during an active overdub capture session SHALL NOT create a new `overdubPass` row and SHALL NOT create a new overdub undo unit. Pass and undo creation for overdub remain gated on successful `commitCapturePass()` at overdub stop for that session. Multiple wraps MAY occur within one pending capture before commit.

#### Scenario: Wrap mid-overdub keeps one pending session

- **GIVEN** an overdub session is open with a pending capture pass
- **WHEN** the playhead wraps the loop one or more times before stop
- **THEN** no additional `overdubPass` is committed at wrap
- **AND** no additional overdub undo unit is created at wrap
- **AND** stop plus successful `commitCapturePass` still yields one `overdubPass` and one overdub undo for the session
