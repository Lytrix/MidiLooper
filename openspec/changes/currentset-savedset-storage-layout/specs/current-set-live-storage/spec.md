## ADDED Requirements

### Requirement: CurrentSet runtime payload writes are slot-incremental
CurrentSet runtime persistence SHALL keep loop payloads in per-slot files and
SHALL rewrite only dirty slots during deferred save execution.

#### Scenario: Record stop rewrites only active capture slot
- **WHEN** a record stop publishes a pass in track T slot S
- **THEN** deferred save rewrites `loop_TT_SS.bin` for that slot
- **AND** clean slots in other track/slot positions are skipped

#### Scenario: Idle save with no dirty slots skips payload writes
- **WHEN** deferred save runs and no slot is dirty
- **THEN** no `loop_TT_SS.bin` payload file is rewritten
- **AND** payload write counters report only skipped slots

### Requirement: Transport stop does not force full-slot dirty by default
Transport stop persistence SHALL NOT mark all slot payloads dirty in normal
operation.

#### Scenario: Stop after playback with no material slot changes
- **WHEN** transport is stopped after playback without slot mutations
- **THEN** CurrentSet save does not enqueue full-slot payload rewrites
- **AND** save work remains limited to metadata updates, if any

#### Scenario: Explicit maintenance mode can request full rewrite
- **WHEN** migration or repair mode explicitly requests full CurrentSet rewrite
- **THEN** all slots MAY be marked dirty for one full payload flush
- **AND** normal stop behavior remains incremental outside that mode

### Requirement: CurrentSet payload commits remain atomic per slot
Each slot payload commit SHALL use staged temp write and atomic rename with
completion marker verification.

#### Scenario: Power loss during temp write
- **WHEN** power is lost while writing `loop_TT_SS.bin.tmp`
- **THEN** previous final `loop_TT_SS.bin` remains the last committed payload
- **AND** unrelated slot payload files remain unaffected

#### Scenario: Successful payload commit
- **WHEN** a dirty slot payload write completes
- **THEN** temp payload is verified and renamed to final path
- **AND** dirty state for that slot is cleared only after successful commit

### Requirement: CurrentSet existing v6 trees stay load-compatible
CurrentSet loader SHALL continue to load existing v6 per-slot trees without
requiring SavedSet packing support.

#### Scenario: Existing v6 tree loads after layout change
- **WHEN** boot finds `MidiLooper/current/workspace.bin` and `loop_TT_SS.bin` payload files
- **THEN** load succeeds using per-slot payload reads
- **AND** runtime state is restored from the same CurrentSet layout
