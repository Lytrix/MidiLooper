## ADDED Requirements

### Requirement: Slot-list metadata is index-first
CurrentSet and SavedSet metadata SHALL expose slot-summary rows sufficient for
slot-list browsing without opening loop payload files.

#### Scenario: CurrentSet slot browser reads metadata only
- **WHEN** UI renders slot list for CurrentSet
- **THEN** it reads slot-summary rows from `workspace.bin`
- **AND** it does not open per-slot payload files for basic list data

#### Scenario: SavedSet catalog detail reads metadata only
- **WHEN** UI renders slot summary for a SavedSet entry
- **THEN** it reads slot-summary rows from SavedSet `set.bin`
- **AND** it does not open `loops.bin` for basic list data

### Requirement: Slot summaries update on material slot mutations
Slot-summary rows SHALL be updated whenever material slot content changes.

#### Scenario: Overdub updates slot summary
- **WHEN** overdub publishes new content to slot S
- **THEN** slot S summary fields are updated in CurrentSet metadata
- **AND** unchanged slots keep their previous summary values

#### Scenario: Clear marks slot summary empty
- **WHEN** user clears slot S
- **THEN** slot S summary marks no payload data
- **AND** summary-derived filled-slot counters reflect the clear

### Requirement: Slot summary schema is deterministic and bounded
Slot-summary schema SHALL be fixed-width and bounded so metadata writes stay
predictable in deferred save slices.

#### Scenario: Summary row count is stable
- **WHEN** writing CurrentSet metadata
- **THEN** metadata contains a deterministic row set for all track/slot pairs
- **AND** parser can read rows without dynamic allocations

#### Scenario: Browser sort/filter uses summary fields
- **WHEN** browser applies filters or sorting by slot occupancy or bar count
- **THEN** results are derived from metadata summary fields
- **AND** results are available without payload scan
