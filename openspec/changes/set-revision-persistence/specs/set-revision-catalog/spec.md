## ADDED Requirements

### Requirement: Sets are immutable revision containers

The system SHALL persist Set catalog data under `MidiLooper/sets/` — **separate from**
`MidiLooper/current/`.

Each Set SHALL contain:

```text
set.bin
revisions/
```

Set SHALL NOT contain temp files, undo, current state, or chunk checkpoints.

Set IDs SHALL be monotonic and SHALL NOT be reused when a Set is deleted or corrupt.

Revision commits create new revision files only. No in-place mutation.

Layout:

- `MidiLooper/sets/index.bin` — global Set id allocator and catalog checksum
- `MidiLooper/sets/S####/set.bin` — per-Set metadata
- `MidiLooper/sets/S####/revisions/v####.bin` — immutable revision blobs (see `revision-packed-blob`)

#### Scenario: First save creates Set and v0001

- **WHEN** the user saves and Current has `derivedFromSetId == 0`
- **THEN** the system allocates `S0001` from `index.bin` and writes `revisions/v0001.bin` after VALIDATE
- **AND** `MidiLooper/current/` remains unchanged except provenance at COMPLETE

#### Scenario: Subsequent save appends revision on same Set

- **WHEN** the user saves and Current derives from `S0001` `v0002`
- **THEN** the system writes `S0001/revisions/v0003.bin` without allocating a new Set id

#### Scenario: Save does not move Current

- **WHEN** the user saves Current
- **THEN** the system writes `sets/S0003/revisions/v0004.bin`
- **AND** `MidiLooper/current/` remains the live mutable workspace

### Requirement: Catalog is separate from revision storage

`MidiLooper/sets/index.bin` SHALL use:

| Field | Type | Notes |
|-------|------|-------|
| `schemaVersion` | u16 | Major/minor compatibility |
| `reserved` | u16 | Alignment |
| `nextSetId` | u32 | Next Set id to allocate |
| `setCount` | u32 | Count of Sets in catalog |
| `catalogChecksum` | u32 | Integrity checksum |

Catalog SHALL NOT duplicate revision metadata stored in `set.bin` or revision files.

#### Scenario: Set id never recycled

- **WHEN** Set `S0003` is deleted or fails validation
- **THEN** `nextSetId` does not reuse id `3` for a new Set

### Requirement: set.bin fixed layout

Each `MidiLooper/sets/S####/set.bin` SHALL use:

| Field | Type | Notes |
|-------|------|-------|
| `schemaVersion` | u16 | |
| `setId` | u16 | Matches folder `S####` |
| `latestRevisionId` | u16 | Highest **validated** revision |
| `revisionCount` | u16 | Count of validated revisions |
| `createdUnix` | u64 | |
| `updatedUnix` | u64 | Latest revision time (list sort key) |
| `subtitle` | char[48] | Human-facing name; empty = hide in UI |
| `favorite` | u8 | Non-zero = favorite |
| `reserved` | u8[15] | |
| `crc32` | u32 | Record checksum |

#### Scenario: Favorite Set appears in Favorites section

- **WHEN** `set.bin` for `S0002` has `favorite != 0`
- **THEN** `S0002` appears in overlay **Favorites** sections

### Requirement: Revision ids allocate on completion

Revision ids (`v####`) SHALL become visible in catalog only after **WRITE** + **VALIDATE** +
**CATALOG_UPDATE**. Failed commits SHALL NOT consume ids — the next successful save reuses the
failed slot number.

#### Scenario: Failed commit reuses revision id

- **WHEN** `v0005.bin.tmp` fails validation
- **THEN** `set.bin` still shows `latestRevisionId == 4`
- **WHEN** the next Save completes successfully
- **THEN** the validated file is `v0005.bin` (not `v0006`)

### Requirement: Favorite toggle on Set row

A **double press** on a Set row in the workspace or loop overlay SHALL toggle `favorite` in
`set.bin` and persist the change without loading the Set.

#### Scenario: Double press toggles favorite

- **WHEN** the user double-presses Set row `S0004` in the workspace browser
- **THEN** `favorite` toggles in `S0004/set.bin`
- **AND** no revision load occurs
