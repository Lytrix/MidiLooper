## ADDED Requirements

### Requirement: Tier-A rows are classified into families by full tag prefix
Every Tier-A capture row SHALL be assigned a family, and family assignment SHALL key on the row's full
tag prefix rather than its top-level token, so that heterogeneous tags are not merged.

#### Scenario: Two DIAG tags in different families
- **WHEN** a `DIAG,stop_phase,…` row and a `DIAG,midi_gap,…` row are captured
- **THEN** they are assigned to the stop-edge family and the MIDI-timing family respectively
- **AND** they are not assigned to a shared `DIAG` family

#### Scenario: Unrecognised Tier-A tag still has a family
- **WHEN** a Tier-A row's tag prefix is not in the family map
- **THEN** it is assigned the fallback family
- **AND** it is still treated as Tier-A evidence, never demoted

### Requirement: Families carry a behaviour class
Each family SHALL declare one behaviour: `edge`, `sample`, or `stream`.

#### Scenario: Edge rows are never cadence-sampled
- **WHEN** a family whose behaviour is `edge` produces multiple rows within one second
- **THEN** every occurrence is recorded, subject only to the pathological-rate guard
- **AND** no occurrence is dropped merely because the family's cadence budget was already spent

#### Scenario: Sample rows coalesce instead of dropping
- **WHEN** a family whose behaviour is `sample` produces more rows in one interval than its cadence
  allows
- **THEN** one row is emitted for that interval carrying the latest, minimum and maximum values observed
- **AND** the row records how many observations it coalesced

#### Scenario: Stream rows are budgeted with anomaly preference
- **WHEN** a family whose behaviour is `stream` has exhausted its byte budget for the interval
- **THEN** a routine row is dropped and counted
- **AND** a row marked as an anomaly is admitted before routine rows of the same family

### Requirement: Per-family byte budgets bound the Tier-A rate
Each family SHALL be limited by a byte budget expressed as bytes per second plus a bounded burst
allowance, and the sum of all burst allowances SHALL NOT exceed the Tier-A capacity multiplied by a
safety factor.

#### Scenario: Budget is enforced in bytes, not rows
- **WHEN** a family's rows are larger than its nominal size while its row count is within the configured
  cadence
- **THEN** the family is throttled by bytes
- **AND** the ring's Tier-A rate remains within the profile's total budget

#### Scenario: A quiet family may spend accumulated budget on a transition
- **WHEN** a family has produced no rows for several seconds and then a transition occurs
- **THEN** the family may emit its burst allowance of rows in that interval
- **AND** it may not emit more than its burst allowance

#### Scenario: The duration guarantee is arithmetic
- **WHEN** a profile's core budget and family budgets are summed
- **THEN** the total multiplied by the configured session duration does not exceed the ring's Tier-A
  capacity after reserves
- **AND** a profile that cannot satisfy the configured duration is rejected rather than silently thinned

### Requirement: A core evidence set is always captured
A small always-on core SHALL be captured under every profile, covering transport and state transitions,
record/overdub start, stop, admission and denial, resource-denial events, critical stop/commit markers,
the active capture profile, and the per-family coverage summary.

#### Scenario: Core is captured under a narrow profile
- **WHEN** a profile excludes most families
- **THEN** the core set is still captured
- **AND** the trace states the machine's transport state, the operation that ran, whether it failed, and
  how degraded the trace is

#### Scenario: Core is event-driven, not sampled
- **WHEN** a core event occurs once
- **THEN** it is recorded once
- **AND** periodic verbose subsystem state is not admitted to core merely because it is sometimes useful

### Requirement: A profile selects the captured family set and is recorded in the trace
The active profile SHALL select which families beyond core are captured, SHALL default to a
compile-time default, SHALL be selectable at runtime over the serial command surface, and SHALL be
recorded in the session header together with each family's budget.

#### Scenario: Runtime selection before capture
- **WHEN** a profile is selected over the serial command surface before transport starts
- **THEN** the selected profile governs the families captured for that session
- **AND** the session header names the profile and its budgets

#### Scenario: A trace is self-describing
- **WHEN** a session log is read without any external configuration
- **THEN** the profile and per-family budgets in effect are recoverable from the log itself

#### Scenario: Automatic behaviour never replaces the chosen profile
- **WHEN** firmware detects pressure or a suspected failure class
- **THEN** it may only *add* emergency evidence
- **AND** it does not substitute a different profile for the one the operator selected

### Requirement: Critical evidence has a physical reserve and is never sampled
Critical records SHALL have a reserved region of the ring that budgeted Tier-A traffic cannot consume,
and SHALL never be cadence-sampled, coalesced, or dropped by a budget.

#### Scenario: A Tier-A burst cannot starve critical evidence
- **WHEN** budgeted Tier-A traffic bursts to its maximum
- **THEN** the critical reserve remains available for critical records
- **AND** critical admission does not depend on eviction logic running correctly under pressure

#### Scenario: Critical rows are exempt from every budget
- **WHEN** a critical record is admitted
- **THEN** no family budget, cadence rule, or coalescing policy applies to it

### Requirement: Per-family coverage and drops are reported
The ring SHALL report, for each family, whether it was captured completely, sampled, budget-limited, or
excluded by the profile, together with its drop count, and the report SHALL be emitted before the
session's evidence is drained.

#### Scenario: Coverage summary survives the session
- **WHEN** a session ends with a budget-limited family
- **THEN** the report identifies the family and its drop count
- **AND** the report is emitted even when the ring was never under capacity pressure

#### Scenario: Tier-A eviction remains the regression signal
- **WHEN** a session runs a profile whose budgets are correct for its duration
- **THEN** the Tier-A eviction count is zero
- **AND** any non-zero count indicates a budget or cadence error in that profile

### Requirement: The host never reports an unconditional pass over degraded evidence
Each host assertion SHALL declare the families it requires and the minimum coverage it needs, and an
assertion whose required evidence was thinned below that contract SHALL resolve to a degraded state
rather than a pass.

#### Scenario: Required family thinned
- **WHEN** an assertion requires a family at full completeness
- **AND** that family was budget-limited during the session
- **THEN** the assertion does not report an unconditional pass
- **AND** the report names the family and the shortfall

#### Scenario: Excluded family is distinguishable from thinned family
- **WHEN** a family was excluded by the selected profile
- **THEN** the report says so explicitly
- **AND** it is not presented as evidence that was lost

### Requirement: The ring capacity and drain policy are unchanged
This capability SHALL NOT change the ring's total PSRAM capacity or its no-drain-during-transport
policy; it SHALL only partition the existing capacity for the critical reserve and bound what the
producer admits.

#### Scenario: Capacity is unchanged
- **WHEN** the capture ring is configured under this capability
- **THEN** its total capacity equals the pre-existing 1 MiB budget
- **AND** the critical reserve is a partition of that budget, not an addition to it

#### Scenario: No drain during active transport
- **WHEN** transport is `PLAYING` or `OVERDUBBING`
- **THEN** the ring is not flushed to USB serial
- **AND** the profile and budgets are what keep it within capacity
