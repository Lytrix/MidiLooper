## ADDED Requirements

### Requirement: Persisted edit tail read fails hard

**readPersistedEditsTail** SHALL return **false** when any required **ioRead** fails. It SHALL NOT
return **true** with **editPasses** cleared after a partial read.

#### Scenario: Truncated edit tail fails load

- **WHEN** SD data ends before **editCount** **EditPass** rows are read
- **THEN** **readPersistedEditsTail** returns **false**
- **AND** **readLoopPersisted** fails
- **AND** **loadState** does not report success

#### Scenario: Valid v4 tail succeeds

- **WHEN** SD contains a complete **editPasses** tail
- **THEN** **readPersistedEditsTail** returns **true**
- **AND** **passes.editPasses** matches persisted content

### Requirement: Loop geometry fields apply on load

**applySnapshotToLoop** SHALL copy persisted loop geometry fields from **PersistedLoopSnapshot**
without discarding documented temporal fields.

#### Scenario: applySnapshotToLoop sets startLoopTick

- **WHEN** **applySnapshotToLoop** runs with snapshot **startLoopTick** = N
- **THEN** **loop.startLoopTick** = N after apply
