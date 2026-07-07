# persistence-failure-policy

Defined behavior under sustained memory and persistence pressure — not emergent.

## ADDED Requirements

### Requirement: Defined pressure behavior

When persistence queue depth grows or `freeChunkCount` approaches `CHUNK_RESERVE`, firmware behavior SHALL be defined, documented, and emitted on serial — recording SHALL NOT fail silently.

#### Scenario: Queue growth alarm

- **WHEN** persistence queue depth exceeds a configured threshold while capture is active
- **THEN** firmware SHALL emit `#CAP,PERS,...` with queue depth and `oldestDirtyChunkAge`
- **AND** behavior SHALL follow the configured policy (telemetry-only in Phase 0; backpressure in later phases)

#### Scenario: Reserve threshold approach

- **WHEN** `freeChunkCount` approaches `CHUNK_RESERVE` during an open capture pass
- **THEN** firmware SHALL emit pressure telemetry
- **AND** SHALL apply an explicit policy for whether recording continues, overdub is rejected, or persistence is prioritized — normative choice recorded in implementation tasks before Phase 4 gate

### Requirement: Runtime never compromised for throughput

Under memory pressure, firmware SHALL NOT block recording correctness or playback timing solely to increase SD write throughput.

#### Scenario: Persistence delayed not dropped

- **WHEN** internal heap is below the safety floor at dispatch
- **THEN** persistence SHALL defer per existing `PERS,defer,...,heap_floor` rules
- **AND** MIDI clock, note output, and playback SHALL continue

### Requirement: Failure policy precedes mid-pass persistence

Phase 4 (mid-pass persistence) SHALL NOT ship until failure-policy scenarios are specified and covered by native or HITL validation.

#### Scenario: Phase 4 gate

- **WHEN** enabling writer drain during capture
- **THEN** pressure policy for queue growth and `CHUNK_RESERVE` SHALL be implemented and testable
