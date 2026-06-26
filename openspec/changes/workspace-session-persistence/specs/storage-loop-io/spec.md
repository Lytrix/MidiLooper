## MODIFIED Requirements

### Requirement: Persisted edit tail read fails hard

**readPersistedEditsTail** SHALL return **false** when any required **ioRead** fails. It SHALL NOT
return **true** with **editPasses** cleared after a partial read.

Per-file loop blobs (`Sets/_current/loop_TT_SS.bin`, SavedSet loop files) and monolith v5 inline
blobs SHALL use the same fail-hard tail semantics.

#### Scenario: Truncated edit tail fails load

- **WHEN** SD data ends before **editCount** **EditPass** rows are read
- **THEN** **readPersistedEditsTail** returns **false**
- **AND** **readLoopPersisted** fails
- **AND** CurrentSet or SavedSet load does not report success for that slot

#### Scenario: Valid v5 tail succeeds in per-file blob

- **WHEN** `Sets/_current/loop_00_00.bin` contains a complete **editPasses** tail
- **THEN** **readPersistedEditsTail** returns **true**
- **AND** **passes.editPasses** matches persisted content

### Requirement: Loop geometry fields apply on load

**applySnapshotToLoop** SHALL copy persisted loop geometry fields from **PersistedLoopSnapshot**
without discarding documented temporal fields. This applies when loading from CurrentSet files,
SavedSet files, and v5 monolith migration.

#### Scenario: applySnapshotToLoop sets startLoopTick from CurrentSet file

- **WHEN** **applySnapshotToLoop** runs after **readLoopPersisted** from `Sets/_current/loop_00_00.bin` with **startLoopTick** = N
- **THEN** **loop.startLoopTick** = N after apply

## ADDED Requirements

### Requirement: Per-file loop blob container (v6)

v6 storage SHALL persist each slot loop as an independent file using existing
`writeLoopPersisted` / `readLoopPersisted` wire format with per-file `STORAGE_COMPLETE_MAGIC`
footer. File names SHALL use 2-digit zero-padded track and slot: `loop_TT_SS.bin`.

#### Scenario: CurrentSet slot file round-trip

- **WHEN** a loop with capture passes and editPasses is written to `Sets/_current/loop_02_03.bin`
- **AND** the file is read back via **readLoopPersisted**
- **THEN** geometry and **passes** match the in-memory loop before write

#### Scenario: Missing completion marker fails slot load

- **WHEN** `Sets/_current/loop_00_00.bin` lacks `STORAGE_COMPLETE_MAGIC` footer
- **THEN** that slot load fails
- **AND** boot recovery chain may engage per **recovery-point** spec
