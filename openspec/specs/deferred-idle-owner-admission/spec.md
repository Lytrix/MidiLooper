# deferred-idle-owner-admission Specification

## Purpose
Defines what a deferred idle-owner turn may execute while the transport is timing-critical: only work
whose per-turn cost is declared and enforced, an explicitly named deferral for anything else, one
aggregate budget per turn rather than a sum of unmeasured children, and a separation between
production work and capture-only diagnostics so a capture run cannot hide whether the production lane
is clean.

## Requirements

### Requirement: A timing-critical turn admits only bounded work

While any track is timing-critical (recording, audible playback, overdubbing, or draining a deferred
stop), the deferred idle-owner turn SHALL execute a child only when that child has a **declared
per-turn execution bound**. A child without a declared bound SHALL return a **named deferral** and
execute no work in that turn.

The bound SHALL be a property of the child, declared in the maintained owner-bound inventory, not a
value chosen per call site. A child that cannot be split into bounded units SHALL be classified
unbounded and SHALL be denied on a timing-critical turn until it is split.

#### Scenario: Unbounded child defers on a timing-critical turn

- **WHEN** the transport is running and a track is overdubbing
- **AND** an unbounded child is owed service on the deferred idle-owner turn
- **THEN** the turn reports a named deferral for that child
- **AND** the child executes no work in that turn
- **AND** the turn's own span stays within the declared idle-maintenance bar

#### Scenario: Bounded child still runs

- **WHEN** the transport is running and a track is overdubbing
- **AND** a child whose per-turn bound is declared and enforced is owed service
- **THEN** the child executes its declared grain on that turn

#### Scenario: No timing-critical track keeps today's admission

- **WHEN** the transport is stopped and no track is recording, playing or draining a stop
- **THEN** every child that is eligible today is still eligible, and unbounded work may run as it
  does now

### Requirement: A declared bound is enforced, not merely documented

A child that declares a per-turn budget SHALL check that budget **before** the next indivisible work
unit and SHALL end its turn when the budget is spent. A budget checked only between coarse steps,
where one step alone may exceed it, SHALL NOT be reported as a bound on that child; such a child SHALL
be classified unbounded until its step granularity is named and bounded.

**The scheduling budget and the breach threshold are separate quantities.** The breach threshold
SHALL remain the declared idle-maintenance bar (50 ms) and SHALL be used only as the alarm and as an
acceptance criterion. The **owner work budget** SHALL be strictly smaller than that threshold so that
the turn's fixed overhead — owner scans, the MIDI-service polls around the turn, telemetry and the
breach record itself — cannot push a fully-spent turn past the threshold. The owner work budget SHALL
be a recorded tunable derived from measured residual cost, not a value asserted alongside the alarm.

**Admission SHALL reserve a child's declared maximum before dispatching it**, and the child SHALL run
only when that maximum fits the remaining owner work budget. A child MAY receive a smaller remaining
deadline only if it is resumable at a finer bounded grain and declares that grain. Dispatching a child
into a budget it can overshoot is not admission.

The breach record SHALL NOT be the enforcement mechanism: a turn's conformance SHALL be the product of
admission, with the breach record as the backstop that proves admission was correct.

#### Scenario: Coarse step exceeds the declared budget

- **WHEN** a child's declared budget is smaller than one of its indivisible steps
- **AND** that step runs on a timing-critical turn
- **THEN** the turn is reported as a breach against the declared bar
- **AND** that child is not treated as bounded for scheduling purposes

#### Scenario: A child that does not fit is not dispatched

- **WHEN** a child's declared maximum grain is larger than the remaining owner work budget
- **THEN** the child is deferred with a named reason and runs no work
- **AND** the turn does not spend the remaining budget on a child that can only overshoot it

#### Scenario: A finer-grained child may take a smaller deadline

- **WHEN** a child declares a resumable grain smaller than its maximum
- **AND** the remaining owner work budget is at least one such grain
- **THEN** the child may run for that grain

#### Scenario: Breach evidence survives the flush budget

- **WHEN** a timing-critical turn exceeds the declared bar
- **THEN** the breach record is delivered under the reduced flush budget rather than shed

### Requirement: One aggregate budget for the turn

The declared bound SHALL apply to the **whole deferred idle-owner turn**, aggregated across every
owner and every track that turn visits, not to each child independently. A turn that stacks multiple
bounded children whose sum exceeds the bar SHALL be treated as a contract breach even when each child
is individually within its own bound.

The existing owner rotation, the one-grain-per-admitted-turn discipline, and each owner's identity
SHALL be preserved: this requirement adds a budget to the turn, not a second scheduler.

#### Scenario: Stacked bounded children still breach

- **WHEN** a timing-critical turn runs two children that are each individually within their declared
  bounds but whose combined cost exceeds the idle-maintenance bar
- **THEN** the turn is reported as a breach
- **AND** the second child's remaining work is deferred with a named reason

#### Scenario: Owner rotation unchanged

- **WHEN** the aggregate budget defers a child
- **THEN** the deferred-owner rotation still advances and the child remains owed for a later turn

### Requirement: Deferral is named, observable and lossless

Every deferral produced under this contract SHALL name the deferred child and the reason it was
deferred. A deferral SHALL NOT drop, truncate, reorder or partially adopt work: the child retains its
cursor, request or obligation.

**The two deferral kinds have different eligibility, and they SHALL NOT be conflated:**

- a **bounded** child deferred because its declared grain did not fit the remaining budget of this turn
  SHALL remain owed and MAY run on **any** later eligible turn — **including a timing-critical turn on
  which its declared grain does fit**. Being deferred once SHALL NOT demote it to the stopped lane;
- a child that is **genuinely unbounded** SHALL NOT run while a track is timing-critical and SHALL wait
  for a non-timing-critical opportunity.

The bounded case is what keeps admission from starving preparation: the budget decides *this turn*, not
the child's class.

A deferral SHALL NOT be reported as a successful service, and an absent record SHALL NOT be the only
evidence that a deferral happened.

#### Scenario: A bounded child retries on the next timing-critical turn

- **WHEN** a bounded child is deferred because its declared grain did not fit the remaining budget
- **THEN** it is retried on the next turn whose budget admits that grain
- **AND** that turn may itself be timing-critical

#### Scenario: An unbounded child waits for a stopped turn

- **WHEN** an unbounded child is owed while a track is timing-critical
- **THEN** it does not run, and it is retried when no track is timing-critical

#### Scenario: Deferral names the child and resumes

- **WHEN** a child is deferred on a timing-critical turn
- **THEN** the row names the child and its deferral reason
- **AND** the child's progress state is unchanged
- **AND** the same child completes on a later eligible turn

#### Scenario: Deferral is not silent success

- **WHEN** a consumer reads a turn in which a child was deferred
- **THEN** the deferral is distinguishable from the child having been served and having had no work

### Requirement: Capture-only diagnostics do not define production correctness

Work that exists only in a capture build SHALL be subject to the same turn rule as production work:
bounded, or deferred while timing-critical. Enabling capture SHALL NOT change whether the production
lane is clean, and a capture-only child SHALL NOT be able to consume a timing-critical turn that
production work needs.

#### Scenario: Capture-only dump defers

- **WHEN** a capture-only diagnostic that resolves whole-loop content is owed on a timing-critical turn
- **THEN** it defers with a named reason
- **AND** the production children on that turn are unaffected

#### Scenario: Production acceptance is readable in a capture run

- **WHEN** the acceptance run is performed on a capture build
- **THEN** the production lane's breach record is attributable without separating capture-only work by
  inspection

### Requirement: The overdub acceptance oracle

For an overdub chain with the transport running, the change SHALL be accepted only when a device run
of the same shape shows:

- zero idle-maintenance turn breaches against the declared bar,
- zero playback-window service overruns,
- the **maximum MIDI-service gap** and the **maximum clock-dispatch gap** within their declared
  bounds and collapsed relative to the pre-change run, and
- any remaining persistence breach identified by a named step.

"Every turn under the bar" alone SHALL NOT satisfy the oracle: a turn can be short while the clock is
still late, so the oracle carries both the breach count and the service-gap maxima.

A run that is merely "quieter on average" SHALL NOT satisfy the oracle; the oracle is a count of
breaches and a maximum, not a mean.

#### Scenario: Oracle fails on a single breach

- **WHEN** an overdub acceptance run shows one idle-maintenance turn over the declared bar
- **THEN** acceptance fails, and the turn is attributed to a named child before the next slice

#### Scenario: Oracle fails on a service gap despite no breach

- **WHEN** every idle-maintenance turn is under the declared bar
- **AND** the maximum MIDI-service or clock-dispatch gap is not within its declared bound
- **THEN** acceptance fails

#### Scenario: Attributed persistence breach is acceptable at this stage

- **WHEN** the idle-maintenance and window oracles pass
- **AND** a persistence turn breaches the bar but the row names the indivisible step
- **THEN** the change may proceed to the persistence slice with that step as the target, without
  treating the persistence breach as unmeasured
