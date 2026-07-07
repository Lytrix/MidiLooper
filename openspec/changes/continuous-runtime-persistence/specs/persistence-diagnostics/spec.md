# persistence-diagnostics

Phase 0 telemetry for save starvation and backlog — no persistence behavior change.

## ADDED Requirements

### Requirement: Phase 0 diagnostics without behavior change

Before Phase 1 ships, firmware SHALL emit persistence diagnostics via `#CAP,PERS,...` or `SESSION_CAPTURE` extensions without altering transport gate, seal triggers, or scheduler behavior.

#### Scenario: Starvation visible during 64+64 overdub

- **WHEN** a 64+64 HITL run executes with Phase 0 firmware
- **THEN** serial capture SHALL include queue depth and transport-block counts during overdub
- **AND** `oldestDirtyChunkAge` SHALL be present when sealed chunks exist

### Requirement: Core diagnostic metrics

Phase 0 diagnostics SHALL include at minimum:

- `freeChunkCount` / `usedChunkCount`
- persistence queue depth
- chunks in `Writing`
- maximum deferred backlog observed
- peak writer latency (microseconds)
- slices blocked by budget vs capture
- **`oldestDirtyChunkAge`**
- mean and peak chunk seal → persist latency

#### Scenario: Keeping-up signal

- **WHEN** persistence falls behind capture during a long overdub
- **THEN** `oldestDirtyChunkAge` SHALL increase monotonically until the writer catches up
- **AND** HITL analysis SHALL use this metric as the primary backlog indicator

### Requirement: Diagnostic emission on pressure

When `freeChunkCount` approaches `CHUNK_RESERVE` or queue depth exceeds a configured threshold, firmware SHALL emit a diagnostic alarm line without silently dropping events.

#### Scenario: Pool pressure telemetry

- **WHEN** `freeChunkCount` drops below a configured warn threshold during capture
- **THEN** serial SHALL emit a `#CAP,PERS,...` pressure line with current pool and queue metrics
