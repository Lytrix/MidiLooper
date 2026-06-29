# Revision load

DEC-012 request vocabulary for revision load on `StorageManager`.

## Requirements

### Requirement: Backend revision-load API uses request vocabulary

The `StorageManager` public and internal revision-load surfaces SHALL use DEC-012 request vocabulary.
Behavior and HITL serial wire strings (`rev_load_dirty_yes`, `rev_load_dirty_no`,
`rev_load_dirty_cancel`, etc.) SHALL remain unchanged.

| Legacy name | Target name |
|-------------|-------------|
| `confirmRevisionLoadDirtyPromptSaveThenLoad` | `confirmRevisionLoadAfterCommit` |
| `confirmRevisionLoadDirtyPromptDiscard` | `confirmRevisionLoadDiscardWorkspace` |
| `cancelRevisionLoadDirtyPrompt` | `cancelRevisionLoadRequest` |
| `isRevisionLoadDirtyPromptActive` | `isRevisionLoadHeldForWorkspaceDirty` |
| `dispatchStagedRevisionLoad` | `dispatchRequestedRevisionLoad` |
| `clearRevisionLoadPromptAndPipelineState` | `clearRevisionLoadRequestState` |

#### Scenario: Save-then-load confirm unchanged

- **WHEN** the user confirms save-then-load on a dirty workspace via the renamed API
- **THEN** `storageSession.revisionLoad.loadAfterRevisionCommit` becomes true
- **AND** `requestCommitRevision` is queued
- **AND** serial capture still emits `rev_load_dirty_yes`

#### Scenario: Discard confirm unchanged

- **WHEN** the user confirms discard via the renamed API
- **THEN** `dispatchRequestedRevisionLoad` runs
- **AND** serial capture still emits `rev_load_dirty_no`

#### Scenario: Cancel unchanged

- **WHEN** the user cancels a held load request via the renamed API
- **THEN** request and hold flags on `storageSession.revisionLoad` are cleared
- **AND** serial capture still emits `rev_load_dirty_cancel`

### Requirement: Revision load request policy uses hold vocabulary

`RevisionLoadPolicy` SHALL expose `shouldHoldRevisionLoadRequest(bool workspaceDirty)` instead of
`LoadRequestGate` / `resolveLoadRequestGate`. After revision commit during save-then-load,
`shouldDispatchRequestedLoadAfterCommitComplete` SHALL gate dispatch of the held load request.

#### Scenario: Dirty workspace holds load request

- **WHEN** `requestLoadRevision` runs and `shouldHoldRevisionLoadRequest(true)` is true
- **THEN** `storageSession.revisionLoad.heldForWorkspaceDirty` is set
- **AND** `dispatchRequestedRevisionLoad` does not run until confirm or discard

#### Scenario: Clean workspace dispatches immediately

- **WHEN** `requestLoadRevision` runs and `shouldHoldRevisionLoadRequest(false)` is false
- **THEN** `dispatchRequestedRevisionLoad` runs without showing the dirty prompt
