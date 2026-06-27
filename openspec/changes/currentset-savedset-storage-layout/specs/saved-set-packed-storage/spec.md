## ADDED Requirements

### Requirement: SavedSet snapshot uses two-file packed layout
SavedSet snapshots SHALL be written as immutable packed containers using
`set.bin` plus `loops.bin`.

#### Scenario: saveNewSet writes packed files
- **WHEN** user invokes SavedSet snapshot creation
- **THEN** SavedSet folder contains `set.bin` and `loops.bin`
- **AND** loop payload blobs are stored in `loops.bin` for non-empty slots

#### Scenario: SavedSet remains immutable after commit
- **WHEN** a SavedSet snapshot is completed
- **THEN** runtime persistence does not mutate that SavedSet folder
- **AND** later live edits apply only to CurrentSet

### Requirement: SavedSet meta includes blob index table
SavedSet `set.bin` SHALL include a deterministic blob index table mapping each
persisted slot to payload offset and length in `loops.bin`.

#### Scenario: Slot blob lookup during load
- **WHEN** loading SavedSet into CurrentSet
- **THEN** loader resolves slot payload location from meta index
- **AND** payload reads are bounded to indexed offset and length

#### Scenario: Empty slots are represented without blob payload
- **WHEN** a slot has no saved loop payload
- **THEN** meta index marks it as empty
- **AND** no blob range is allocated in `loops.bin` for that slot

### Requirement: SavedSet packed files commit atomically
SavedSet packed writes SHALL use staged temp files and final atomic commit.

#### Scenario: Snapshot interrupted before commit
- **WHEN** power loss occurs while writing `set.bin.tmp` or `loops.bin.tmp`
- **THEN** previous SavedSet folders remain valid and unchanged
- **AND** incomplete temp files are not treated as committed snapshots

#### Scenario: Snapshot commit success
- **WHEN** packed snapshot write passes integrity checks
- **THEN** temp files are renamed to final `set.bin` and `loops.bin`
- **AND** SavedSet appears in catalog as a committed entry

### Requirement: SavedSet loader supports packed format discriminator
SavedSet load path SHALL detect packed format from SavedSet metadata and dispatch
to packed payload reader.

#### Scenario: Packed SavedSet is loaded into CurrentSet
- **WHEN** user loads a packed SavedSet
- **THEN** loader reads payload blobs from `loops.bin` using meta index
- **AND** reconstructed CurrentSet loop slots match saved slot content
