## ADDED Requirements

### Requirement: StorageSession owns active persistence job RAM

`StorageManager` SHALL own a single `StorageSession storageSession` instance containing job structs
for active persistence work:

- `currentWorkspaceSave` — deferred workspace save FSM state
- `revisionCommit` — revision commit FSM state (including `overlayBackgroundCommit`)
- `revisionLoad` — revision load request/hold/dispatch and FSM state
- `bootRecovery` — boot revision recovery pending flags
- `setBrowserNavigation` — overlay drill navigation state

#### Scenario: Revision load request flags live on session

- **WHEN** the user requests a revision load
- **THEN** `storageSession.revisionLoad.requested`, `requestedSetId`, and `requestedRevisionId` are set
- **AND** those fields are not parallel file-scope duplicates

#### Scenario: Durable workspace facts stay outside session

- **WHEN** `currentWorkspaceEpoch` or `lastCommittedWorkspaceEpoch` are read or written
- **THEN** they remain file-scope workspace facts in `Internal.cpp`
- **AND** are not fields on `StorageSession`

### Requirement: Job struct migration is incremental per FSM owner

When a job FSM moves to its TU, the corresponding `extern` statics for that job SHALL migrate into
the matching `StorageSession` job struct member in the same or immediately following PR.

#### Scenario: Workspace save fields colocated

- **WHEN** `WorkspaceSave.cpp` is extracted
- **THEN** `deferredSavePending`, `deferredSaveStage`, and related cursors are fields on
  `storageSession.currentWorkspaceSave` (or accessors with no duplicate externs)

#### Scenario: Boot recovery fields colocated

- **WHEN** boot recovery migration completes
- **THEN** `bootRevisionRecoveryPending`, `bootRevisionRecoverySetId`, and `bootRevisionRecoveryRevisionId` are fields on `storageSession.bootRecovery`

### Requirement: Job reset functions clear struct members

Each `reset*JobState` function SHALL reset only its job's `StorageSession` member (and nested stage
enums/files) without clearing durable workspace epoch facts.

#### Scenario: Revision load reset clears request state

- **WHEN** `resetRevisionLoadJobState` runs
- **THEN** in-progress load files and stages are closed/reset
- **AND** `clearRevisionLoadPromptAndPipelineState` clears request/hold flags on `storageSession.revisionLoad`

### Requirement: Activity snapshot reads session job flags

`buildStorageActivitySnapshot()` SHALL populate revision load and commit overlay flags from
`storageSession` job members, not from removed duplicate externs.

#### Scenario: Snapshot after struct migration

- **WHEN** `storageSession.revisionLoad.pending` is true
- **THEN** `buildStorageActivitySnapshot().revisionLoadPending` is true
