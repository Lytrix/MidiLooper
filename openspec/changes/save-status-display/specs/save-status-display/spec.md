## ADDED Requirements

### Requirement: Sidebar save status indicator
The display SHALL show a 4-dot save status indicator in the bottom-right sidebar, below the undo field, right-aligned within the existing sidebar column. The indicator SHALL reflect combined deferred save activity (loop capture, edit-pass flush, loop-length edit, transport stop) as a single channel.

#### Scenario: Indicator placement does not overlap sidebar text
- **WHEN** the display refreshes during any transport state
- **THEN** the 4-dot row is drawn below the undo field (E:/U: line)
- **AND** the dots do not overlap BPM, mode, or undo text

#### Scenario: Idle when no save activity
- **WHEN** no deferred save is pending or in progress
- **AND** no completed or failed flash window is active
- **THEN** all four dots are off (brightness 0)

### Requirement: Pending state shows dim static dots
When a deferred save is queued but not yet dispatching, the indicator SHALL show all four dots at dim brightness.

#### Scenario: Pending after record stop
- **WHEN** record stop marks the active slot dirty and calls `requestDeferredSaveState`
- **AND** the deferred save has not yet dispatched
- **THEN** all four dots are dim static

#### Scenario: Pending while capture blocks save
- **WHEN** a deferred save is pending
- **AND** any track is RECORDING or OVERDUBBING (save slices blocked)
- **THEN** the indicator shows Pending (all dim)
- **AND** does not return to Idle until the save completes or a new request is queued

### Requirement: In-progress state rotates one bright dot
When a deferred save job is in progress, the indicator SHALL animate by cycling a single bright dot through positions 0, 1, 2, and 3 every approximately 200 ms.

#### Scenario: In-progress during PLAYING
- **WHEN** deferred save is in progress while transport is PLAYING
- **THEN** exactly one dot is bright at a time
- **AND** the bright dot position advances through all four positions
- **AND** the loop slot file is written without requiring transport stop

### Requirement: Completed flash after successful save
When a deferred save completes successfully, the indicator SHALL briefly show all four dots at full brightness, then return to Idle.

#### Scenario: Completed flash after PERS result ok
- **WHEN** deferred save completes with serial `PERS,result,...,ok`
- **THEN** all four dots are bright for at most 800 ms
- **AND** then all dots turn off (Idle)

#### Scenario: New save request clears completed flash
- **WHEN** a completed flash is active
- **AND** a new `requestDeferredSaveState` is issued
- **THEN** the indicator immediately transitions to Pending or InProgress

### Requirement: Failed flash after unsuccessful save
When a deferred save fails, the indicator SHALL show all four dots at mid brightness for at most 800 ms, then return to Idle.

#### Scenario: Failed save indication
- **WHEN** deferred save completes with a failed result
- **THEN** all four dots are mid brightness for at most 800 ms
- **AND** no rotation animation is shown during the failed flash

### Requirement: Display reads save phase via StorageManager snapshot
The display SHALL obtain save status through `StorageManager::getDeferredSaveDisplayStatus(nowMs)` and SHALL NOT read internal deferred-save static flags directly.

#### Scenario: Snapshot is allocation-free
- **WHEN** `getDeferredSaveDisplayStatus` is called from the display update path
- **THEN** no heap allocation occurs
- **AND** no full heap walk occurs

### Requirement: Capture telemetry reports save phase transitions
When session capture is enabled, firmware SHALL emit `#CAP,SAVE,<phase>,rotateStep` on save-status phase transitions (not every display frame) for HITL verification.

#### Scenario: SAVE line on phase change
- **WHEN** save display phase transitions (e.g. Pending to InProgress)
- **AND** `SESSION_CAPTURE` is enabled
- **THEN** serial output includes a `#CAP,SAVE` line with phase name and rotate step
