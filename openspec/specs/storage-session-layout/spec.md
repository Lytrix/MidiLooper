# Storage session layout

Persistence FSM translation-unit layout for `StorageManager` (DEC-012 Tier 3).

## Requirements

### Requirement: Persistence FSM code lives in job translation units

The firmware SHALL place each persistence job FSM body in a dedicated translation unit under
`src/StorageManager/`:

- `WorkspaceSave.cpp` — `currentWorkspaceSave` deferred save FSM
- `RevisionCommit.cpp` — `revisionCommit` FSM
- `RevisionLoad.cpp` — `revisionLoad` FSM

`StorageManager.cpp` SHALL retain the public `StorageManager::` API and
`StorageManager::processDeferredSaveState` as the slice orchestrator only.

#### Scenario: Workspace save FSM is isolated

- **WHEN** a developer opens `src/StorageManager/WorkspaceSave.cpp`
- **THEN** `resetDeferredSaveJobState`, `beginDeferredSaveJob`, and `stepDeferredSaveJob` are defined there
- **AND** `stepDeferredSaveJob` is not defined in `StorageManager.cpp`

#### Scenario: Revision commit FSM is isolated

- **WHEN** a developer opens `src/StorageManager/RevisionCommit.cpp`
- **THEN** `resetRevisionCommitJobState` and `stepRevisionCommitJob` are defined there
- **AND** private commit stage helpers used only by that FSM are not in anonymous namespaces in `StorageManager.cpp`

#### Scenario: Revision load FSM is isolated

- **WHEN** a developer opens `src/StorageManager/RevisionLoad.cpp`
- **THEN** `resetRevisionLoadJobState`, `dispatchRequestedRevisionLoad`, `clearRevisionLoadRequestState`, and `stepRevisionLoadJob` are defined there

### Requirement: Shared persistence helpers are linkable across TUs

Any helper function called by more than one persistence TU, or by a new job TU from code previously
in an anonymous namespace in `StorageManager.cpp`, SHALL be declared in
`include/StorageManagerInternal.h` and defined in `StorageManagerInternal` namespace in an
appropriate `src/StorageManager/*.cpp` file.

#### Scenario: File I/O helpers are shared

- **WHEN** any persistence FSM TU writes or reads SD payload bytes
- **THEN** it calls `StorageManagerInternal::writeRaw` or `readRaw` from `FileIo.cpp`
- **AND** does not duplicate those implementations

#### Scenario: New job TU links without anonymous-namespace access

- **WHEN** `WorkspaceSave.cpp` is added to the firmware build
- **THEN** the linker resolves all symbols without pulling helpers from an anonymous namespace in `StorageManager.cpp`

### Requirement: FSM translation unit split preserves runtime behavior

Extracting persistence FSM bodies into job TUs SHALL NOT change deferred slice budgets, stage
transition order, SD file paths, or overlay coordination outcomes.

#### Scenario: Native regression gate after extract

- **WHEN** a job FSM TU is extracted
- **THEN** `pio test -e native` passes with no changes to test expectations in `test_set_revision_persistence`

#### Scenario: Firmware build after extract

- **WHEN** all three job FSM TUs exist
- **THEN** `pio run -e teensy41-capture-serial` succeeds

### Requirement: Overlay routing translation unit owns load request entry points

After the revision load FSM TU is extracted, `src/StorageManager/Overlay.cpp` SHALL own
`StorageManager::requestLoadRevision`, `StorageManager::requestLoadLatestRevisionForSet`, dirty-prompt
confirm/cancel methods, and `StorageManager::hasRevisionLoadWork` / `isRevisionLoadActive` if not
already present there.

#### Scenario: Load request uses exported dispatch

- **WHEN** `StorageManager::requestLoadRevision` runs on a clean workspace
- **THEN** it calls `StorageManagerInternal::dispatchRequestedRevisionLoad`
- **AND** the dispatch symbol is defined in `RevisionLoad.cpp` or `Overlay.cpp`, not in an anonymous namespace in `StorageManager.cpp`

### Requirement: HITL capture block remains in StorageManager.cpp

The `SESSION_CAPTURE` HITL backup block (`HitlRevisionCommitBackup`, `FLASHMEM`/`DMAMEM` symbols)
SHALL remain in `StorageManager.cpp` until a separate architecture reassessment.

#### Scenario: FSM extract does not move HITL backup

- **WHEN** job FSM TUs are created
- **THEN** `HitlRevisionCommitBackup` is not defined in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, or `RevisionLoad.cpp`
