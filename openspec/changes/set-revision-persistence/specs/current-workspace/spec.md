## ADDED Requirements

### Requirement: Current workspace is mutable runtime state

The system SHALL persist the live editable workspace only under `MidiLooper/current/`. Legacy
`Sets/_current/` and any Current path inside `Sets/` SHALL NOT be read or written after format bump.

Current SHALL contain:

- active recording state, playback state, chunk epochs, undo history, unsaved edits, transport state

Current SHALL support chunk-based persistence, deferred save, epoch validation, and recovery.

Current SHALL NOT contain revision history, catalog metadata, or archived revisions.

Current persistence SHALL extend the existing **CurrentSet** + **deferred save FSM** model — not
introduce synchronous save semantics.

Paths (epoch-scoped files under `MidiLooper/current/`):

```text
MidiLooper/current/workspace.bin
MidiLooper/current/transport.bin
MidiLooper/current/global.bin
MidiLooper/current/slots/slot_##.bin
MidiLooper/current/undo/slot_##.bin
MidiLooper/current/temp/
```

#### Scenario: Live edit writes to Current only

- **WHEN** the user records into a loop slot
- **THEN** deferred save updates `MidiLooper/current/slots/` via chunk-bounded writes
- **AND** no `MidiLooper/sets/` revision file is mutated

### Requirement: Current workspace uses epoch persistence

Current workspace SHALL persist using **epochs** — immutable snapshot boundaries aligned with the
existing deferred save FSM (`requestDeferredSaveState` / `processDeferredSaveState`).

Rules:

- Epoch increments when a persistence snapshot **begins** — not during active **RECORDING** or
  **OVERDUBBING** capture.
- Each persisted Current file SHALL include a file header: `epoch` (u32), `schemaVersion` (u16),
  `crc32` (u32).
- A Current epoch becomes valid only when **all** required files for that epoch are written,
  `workspace.bin` is written, and the completion marker validates.
- Partial epochs SHALL be ignored at boot (no merge, no journal replay, no repair).

Boot recovery for Current: load the **highest valid epoch**.

#### Scenario: Power loss during epoch write

- **WHEN** epoch 42 is being written and power is lost mid-write
- **THEN** on boot the system loads epoch 41 (last complete epoch)
- **AND** partial epoch 42 files are ignored

### Requirement: workspace.bin fixed layout

`MidiLooper/current/workspace.bin` SHALL use this fixed layout:

| Field | Type | Notes |
|-------|------|-------|
| `schemaVersion` | u16 | Major/minor per schema compatibility rules |
| `currentEpoch` | u32 | Live workspace epoch |
| `lastCommittedEpoch` | u32 | Epoch last captured in validated revision commit |
| `derivedFromSetId` | u16 | `0` = never saved to a Set |
| `derivedFromRevisionId` | u16 | Last loaded or committed revision |
| `lastCommittedRevisionId` | u16 | Last validated `commitRevision` |
| `slotCount` | u8 | Slots per track (8) |
| `slotSummary` | SlotSummary[8] | Overlay preview |
| `updatedUnix` | u64 | |
| `crc32` | u32 | |

`SlotSummary` (per slot): `occupied` u8, `noteCount` u16, `bars` u16, `muted` u8.

#### Scenario: Overlay preview from workspace.bin

- **WHEN** the Set or loop overlay opens
- **THEN** the right **Details** panel MAY render slot summary from `workspace.bin` without reading full slot blobs

### Requirement: Dirty state derives from persistence state

Workspace dirty (UI: uncommitted vs last revision) SHALL equal:

```text
lastCommittedEpoch != currentEpoch
```

Subsystems SHALL NOT write a separate dirty flag. Subsystems emit **change events** only; epoch
advancement is owned by the deferred save FSM. Dirty clears only after revision commit reaches
**COMPLETE** (catalog updated, `lastCommittedEpoch` synced).

#### Scenario: Record advances epoch via deferred save

- **WHEN** the user completes a record pass and deferred save finishes epoch 120
- **AND** `lastCommittedEpoch == 119`
- **THEN** the workspace is dirty (`120 != 119`)

#### Scenario: Commit clears dirty

- **WHEN** validated revision commit completes for epoch 120
- **THEN** `lastCommittedEpoch` becomes `120`
- **AND** workspace is clean until the next epoch completes

### Requirement: Provenance fields on workspace.bin

`derivedFromSetId` and `derivedFromRevisionId` SHALL update only after a validated full workspace
load or successful revision **COMPLETE** — not during incremental epoch writes.

#### Scenario: Record does not change derived provenance

- **WHEN** the user records new MIDI after load
- **THEN** `derivedFromSetId` / `derivedFromRevisionId` remain unchanged until next commit COMPLETE or load
