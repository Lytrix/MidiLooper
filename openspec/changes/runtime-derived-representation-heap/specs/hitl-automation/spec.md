## MODIFIED Requirements

### Requirement: Long-run record-stop heap floor is telemetry-first

For long runs (`--record-bars` ≥ long-run threshold), the HITL runner's record-stop internal-heap
floor check SHALL default to **telemetry only** and SHALL NOT fail the run solely because stop-path
heap is below 12 KB when playback and persistence complete successfully.

#### Scenario: Default floor does not fail 64-bar record

- **WHEN** the operator runs a 64-bar record-only baseline without overriding floor args
- **THEN** `--record-stop-min-free-ram2-bytes` defaults to **0**
- **AND** the run PASS/FAIL is determined by heartbeat, core ST transitions, and persistence
  `result ok` within timeout — not stop-entry heap floor

#### Scenario: Optional warn threshold for regression detection

- **WHEN** the operator passes `--record-stop-min-free-ram2-warn-bytes 12288`
- **AND** record-stop sampled heap is below 12288
- **THEN** the runner SHALL emit a JSON warning field
- **AND** SHALL NOT fail the run unless `--record-stop-min-free-ram2-bytes` is set above 0

#### Scenario: 64+64 long-run transition gates

- **WHEN** the operator runs 64+64 with `--record-stop-min-free-ram2-bytes 0`
- **THEN** PASS requires `OVERDUBBING → PLAYING` in serial or firmware text
- **AND** serial heartbeat is not lost for the configured timeout
