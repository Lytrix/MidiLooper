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

Each `v####.bin` SHALL use this on-disk layout (task **1.4**):

**RevisionHeader (128 bytes, offset 0x0000)**

| Field | Type | Notes |
|-------|------|-------|
| `magic` | char[8] | `"REVPK01\0"` |
| `schemaVersion` | u16 | Major/minor compatibility |
| `headerSize` | u16 | `128` |
| `revisionId` | u16 | Assigned at VALIDATE (0 in `.tmp`) |
| `setId` | u16 | |
| `sourceEpoch` | u32 | Current epoch at SNAPSHOT |
| `createdUnix` | u64 | |
| `transportOffset` | u32 | |
| `globalOffset` | u32 | |
| `undoOffset` | u32 | |
| `loopIndexOffset` | u32 | |
| `loopIndexCount` | u16 | |
| `payloadSize` | u32 | |
| `headerCrc32` | u32 | |
| `workspaceFlags` | u32 | |
| `reserved` | u8[56] | |

**LoopIndexEntry (32 bytes × loopIndexCount)** — references pass/chunk blob offsets, not flat MIDI.

**Payload sections**: transport, global, undo, per-slot pass/chunk blobs (via existing `StorageLoopIo`
serialization shapes where applicable).

**Footer (12 bytes, end of file)**

| Field | Type | Notes |
|-------|------|-------|
| `completeMagic` | u32 | `0x53564F4B` |
| `payloadCrc32` | u32 | |
| `fileSize` | u32 | |

#### Scenario: Native parser reads index without heap

- **WHEN** a host test opens a valid `v0001.bin`
- **THEN** loop index entries are readable from fixed offsets without dynamic allocation

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

### Requirement: Revision files are never mutated

Once validated and committed, `v####.bin` SHALL NOT be modified.

#### Scenario: Load does not alter source revision

- **WHEN** the user loads revision `v0007` into Current
- **THEN** `v0007.bin` on SD is unchanged
