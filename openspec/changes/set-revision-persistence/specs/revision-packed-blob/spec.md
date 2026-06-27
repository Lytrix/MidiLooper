## ADDED Requirements

### Requirement: Revision blob contains full workspace snapshot

Each packed `v####.bin` SHALL contain a **full workspace snapshot** at the persisted epoch boundary:
per-slot canonical pass storage, transport/global state, and undo stacks — not loop payloads alone.

#### Scenario: Revision round-trip preserves transport and undo

- **WHEN** a revision is committed from Current with non-default transport and undo history
- **THEN** loading that revision restores loop content, transport fields, and undo stacks to match
  the committed snapshot

### Requirement: Revision persistence streams canonical pass storage

Revision persistence SHALL serialize **canonical pass storage** from the existing loop model:

- `recordPass`
- `overdubPasses[]`
- `editPasses[]`
- capture pass state and chunk references

Revision save SHALL **NOT**:

- materialize full MIDI event vectors on the stop or save path
- reconstruct display notes for persistence
- flatten chunk pools into monolithic MIDI blobs

Materialized MIDI (e.g. `LoopPasses::materialize`) remains **runtime cache only**.

#### Scenario: Large loop save stays memory-bounded

- **WHEN** a loop with many passes and chunk refs is saved to revision
- **THEN** peak memory during **WRITE** remains bounded (streaming/chunk refs)
- **AND** save does not trigger full-loop materialize on the persistence path

### Requirement: Revision packed file byte layout

Each `v####.bin` SHALL use **REVPK02** on-disk layout (task **1.4**):

**RevisionHeader (128 bytes, offset 0x0000)**

| Field | Type | Notes |
|-------|------|-------|
| `magic` | char[8] | `"REVPK02\0"` |
| `schemaVersion` | u16 | Major/minor compatibility |
| `headerSize` | u16 | `128` |
| `revisionId` | u16 | Assigned at VALIDATE (0 in `.tmp`) |
| `setId` | u16 | |
| `sourceEpoch` | u32 | Current epoch at SNAPSHOT |
| `createdUnix` | u64 | |
| `chunkCount` | u16 | Typed chunks in payload stream |
| `reserved0` | u16 | |
| `payloadSize` | u32 | Bytes between header and footer |
| `headerCrc32` | u32 | |
| `workspaceFlags` | u32 | |
| `reserved` | u8[84] | |

**Payload — append-only chunk stream**

Each chunk:

| Field | Type | Notes |
|-------|------|-------|
| `type` | u8 | `Transport` \| `Global` \| `Undo` \| `SlotIndex` \| `LoopSlot` |
| `trackIndex` | u8 | |
| `slotIndex` | u8 | |
| `reserved` | u8 | |
| `bodyLength` | u32 | |
| `body` | u8[bodyLength] | |

- **`LoopSlot` body** — `StorageLoopIo` v5 wire (`recordPass`, `overdubPasses[]`, `editPasses[]`).
- **`SlotIndex` body** — `entryCount` (u16), reserved (u16), then **SlotIndexEntry** (32 B × N):
  `trackIndex`, `slotIndex`, `occupied`, `chunkOffset` (relative to payload start), `bodyLength`,
  `loopLengthTicks`, `noteCount`, `bars`, reserved.
- Commit writes **SlotIndex last** so offsets are final before footer CRC.

**Footer (12 bytes, end of file)**

| Field | Type | Notes |
|-------|------|-------|
| `completeMagic` | u32 | `0x53564F4B` |
| `payloadCrc32` | u32 | Over full payload stream |
| `fileSize` | u32 | |

#### Scenario: Native parser reads SlotIndex without heap

- **WHEN** a host test opens a valid `v0001.bin`
- **THEN** SlotIndex entries are readable by scanning the chunk stream without dynamic allocation

### Requirement: Single schema compatibility

All persistence records (`index.bin`, `set.bin`, `workspace.bin`, `v####.bin`) SHALL use
**schemaVersion** (u16) only — no separate `formatVersion` or `footerVersion`.

Compatibility:

- **Major** mismatch → reject load
- **Minor** mismatch → ignore unknown trailing fields

#### Scenario: Major schema mismatch rejected

- **WHEN** `workspace.bin` major schema differs from firmware
- **THEN** boot falls through to revision recovery chain

### Requirement: Revision temp write validate rename

Commit SHALL write to `v####.bin.tmp`, fsync, **VALIDATE**, then rename to `v####.bin`. Validation
SHALL require header CRC, payload CRC, and `completeMagic`. Invalid `.tmp` files SHALL be deleted
at boot.

#### Scenario: Power loss during commit leaves Current intact

- **WHEN** power is lost while writing `v0004.bin.tmp`
- **THEN** on next boot `MidiLooper/current/` highest valid epoch remains live
- **AND** incomplete `.tmp` is removed
- **AND** `set.bin` reflects only prior validated revisions

### Requirement: Revision commit reads from Current incrementally

`commitRevision` **WRITE** SHALL assemble the packed blob by reading completed `MidiLooper/current/` epoch
files in chunk-bounded slices — not a synchronous RAM snapshot.

#### Scenario: Commit interleaves with MIDI during PLAYING

- **WHEN** revision **WRITE** runs during **PLAYING**
- **THEN** progress uses `maxPersistenceMicros` budget per slice
- **AND** USB MIDI clock and note output continue without multi-second contiguous SD blocks

#### Scenario: Transport chunk ordering on disk

- **WHEN** commit **WRITE** assembles a revision with a runtime bundle and at least one LoopSlot
- **THEN** the Transport chunk appears in the payload stream with a valid chunk header before its body
- **AND** SlotIndex is written last with final chunk offsets

### Requirement: Load may proceed without Transport chunk

A revision missing Transport data is **invalid for full workspace round-trip** but SHALL remain
loadable: load restores LoopSlot bodies and applies default transport (see `revision-load` spec).
New commits SHALL always include a valid Transport chunk when `runtime.bundle.bin` is present.

#### Scenario: Pre-fix revision without Transport header

- **WHEN** an older dev revision was committed without a Transport chunk header
- **AND** the user loads that revision
- **THEN** load uses default transport and still restores LoopSlot chunks

### Requirement: Revision files are never mutated

Once validated and committed, `v####.bin` SHALL NOT be modified.

#### Scenario: Load does not alter source revision

- **WHEN** the user loads revision `v0007` into Current
- **THEN** `v0007.bin` on SD is unchanged
