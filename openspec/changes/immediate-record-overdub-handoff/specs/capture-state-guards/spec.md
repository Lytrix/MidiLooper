## ADDED Requirements

### Requirement: First Record to overdub does not wait for canonical publication

Within the declared supported handoff envelope, the system SHALL end a Record interval at its recognized stop boundary and make a separate live Capture role available for the first overdub without waiting for whole-take normalization, checkpoint publication, display readiness or persistence. Overdub input SHALL retain its original acceptance tick, channel, event data and identity. A remembered command that begins capture only after predecessor publication SHALL NOT satisfy this requirement.

#### Scenario: Input arrives while the Record predecessor is still private

- **WHEN** Record A is stopped, its canonical publication is deliberately withheld, and the user starts overdub B
- **THEN** B accepts the next eligible input while A remains private
- **AND** A's sealed content and identity stay unchanged and B's acceptance does not require a second press

#### Scenario: Repeated start is idempotent

- **WHEN** B is already live and receives another explicit overdub-start command
- **THEN** its accepted content, interval identity and source binding remain unchanged
- **AND** no second Capture interval replaces B

### Requirement: A successor overdub can stop while its Record predecessor finalizes

Within the declared supported handoff envelope, an overdub stop SHALL end that interval's input ownership at the recognized command boundary even while Record finalization remains incomplete. Each stopped interval SHALL retain immutable content and its own boundary/identity. The system SHALL NOT continue appending input to a stopped interval merely because canonical workers are occupied. Subsequent start/stop commands SHALL remain usable within the measured admitted envelope.

#### Scenario: Early first-overdub stop and another interval

- **WHEN** Record A remains private, B accepts input and stops, and overdub C then starts and stops before A finalizes
- **THEN** A, B and C remain independently attributable to their accepted intervals and stop ticks
- **AND** input after B's stop cannot enter B, and input after C's stop cannot enter C
- **AND** shared worker completion later preserves all three intervals exactly once

#### Scenario: Custody capacity is established before the overlap

- **WHEN** the supported handoff workload is admitted
- **THEN** its sealed-interval capacity, live successor capacity, retirement overlap and recovery reserve are already available
- **AND** validation records the measured envelope and exhaustion behavior instead of treating descriptor count as storage capacity
- **AND** accepted content is never truncated, silently dropped or overwritten to fit the envelope

### Requirement: Source preparation does not block first-overdub input or lose overlap correctness

First-overdub source preparation SHALL preserve capture ingress and use the correct immutable predecessor content and geometry. Source-dependent Hide, Shorten, held-note and same-pitch decisions SHALL be resolved against that qualified predecessor and applicable prior accepted changes before their observable effect is required. An empty committed-pass set, stale root or partial display cache SHALL NOT substitute for the private predecessor source. Private or incomplete canonical state SHALL NOT be presented as committed currency.

#### Scenario: Same-pitch hold overlaps a still-private Record note

- **WHEN** B accepts a same-pitch hold while A's canonical state is private
- **THEN** B's events keep their acceptance coordinates and overlap result matches the independent sequential canonical oracle
- **AND** ingress does not wait for a whole-take source reconstruction on the command or input path

#### Scenario: Publication arrives during source-dependent resolution

- **WHEN** A publishes while B has accepted input or pending overlap work
- **THEN** publication cannot reinterpret B against a different source identity or apply an overlap change twice

### Requirement: Late Record finalization cannot mutate a live or sealed successor

Finalization SHALL update only the predecessor-owned content, receipts and permitted committed-base publication. It SHALL preserve successor Capture, sealed extents, source bindings, pending changes, timing geometry and causal undo/persistence history. Resource release SHALL reject stale identities and occur only after all relevant readers and continuations release custody.

#### Scenario: Late Record publication with live and sealed successors

- **WHEN** B is sealed, C is live, and A publishes, attaches undo, requests save and retires
- **THEN** B and C retain exact content and identity, and no successor entry is erased by predecessor undo pruning
- **AND** persistence/undo preserve accepted operation order without exposing partial canonical state

#### Scenario: Record becomes empty after filtering

- **WHEN** A normalizes to an empty result after B or C has already accepted input
- **THEN** empty-result cleanup does not clear successor content or reset the geometry/source state needed to retain it
- **AND** the final canonical result and undo/save behavior preserve the accepted successor independently of A's empty result

#### Scenario: Clear load switch and transport changes supersede by identity

- **WHEN** clear, replacement load, slot exit, transport Stop or a stale completion occurs during the handoff
- **THEN** each operation follows its existing acceptance/closure semantics without stale restart or cross-interval mutation
- **AND** changing UI track focus alone does not change an interval's owner

### Requirement: Handoff resource and timing acceptance is explicit

The handoff SHALL be qualified under an explicit measured workload and timing contract, including long loops, held notes, same-pitch edits and background display/persistence contention. Command-to-ingress and command-to-stop measurements SHALL distinguish gesture-recognition time from execution time. Supported capability SHALL NOT be inferred from a warm later-overdub run, an empty take or native logical ordering alone.

#### Scenario: First handoff compared with ordinary overdub

- **WHEN** first-Record handoff and later overdub cycles are evaluated with matched content and contention
- **THEN** both use the same declared acceptance criteria and record their own latency, input-service gaps and ordered content verdict
- **AND** the first handoff cannot pass by queuing Start until normalization completes or by extending B past its requested stop
