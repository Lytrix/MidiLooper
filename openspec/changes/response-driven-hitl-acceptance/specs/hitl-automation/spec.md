## ADDED Requirements

### Requirement: Response-driven acceptance gestures

The acceptance runner SHALL distinguish sent input, recognized gesture, accepted intent and executed operation. It MUST correlate responses with the current run, track, slot, interval and expected gesture. Elapsed time or unqualified human log messages MUST NOT establish completion. Timed MIDI pulse generation SHALL follow the production gesture classification contract.

#### Scenario: Pending switch prevents a follow-up press
- **WHEN** a switch is pending and no matching completion is observed
- **THEN** the runner sends no dependent record or slot gesture
- **AND** deadline expiry produces a non-success result without pressing again

#### Scenario: Unrelated or stale response
- **WHEN** a response belongs to another slot, interval, boot or prior run
- **THEN** it does not satisfy the action predicate

#### Scenario: Recognized single gesture
- **WHEN** one intended short gesture is sent
- **THEN** its recognition is attributed independently of the resulting operation count
- **AND** unexpected double, triple, long or external input prevents successful attribution

### Requirement: Continuously playing acceptance window

The runner SHALL provide distinct record and overdub accepted-sequence scenarios using production DROID-equivalent MIDI controls. Transport MUST remain playing throughout each measured window. Preparation, clearing, seeding and final stopped-only verification SHALL have explicit boundaries outside that window. A manual reference pass MUST NOT be required.

#### Scenario: Record successor
- **WHEN** the record scenario switches from capturing A to prepared record target B
- **THEN** one slot gesture accepts A's stop and starts the intended record successor through the production route
- **AND** B's actual record interval, its single stop acceptance during A's ownership window, and eventual execution are independently evidenced
- **AND** global transport does not stop

#### Scenario: Overdub successor
- **WHEN** the overdub scenario switches from capturing seeded A to seeded B
- **THEN** one slot gesture accepts A's stop and switches to B, followed by a recognized start of B's overdub and one B stop gesture
- **AND** B's stop acceptance during A's ownership window and eventual execution are independently evidenced
- **AND** global transport does not stop

#### Scenario: Mixed record predecessor and overdub successor
- **WHEN** a record interval A stops and seeded B begins overdubbing while A finalizes
- **THEN** one B stop is accepted during A's identified ownership window and eventually executes once
- **AND** the same continuous-transport, content, timing and identity gates apply

#### Scenario: Natural overlap missed
- **WHEN** predecessor ownership retires before the required successor stop acceptance
- **THEN** the attempt reports NOT_EXERCISED with a nonzero exit
- **AND** no artificial hardware pause or delay is introduced to manufacture acceptance

### Requirement: Scoped same-boot fixture preparation

The runner SHALL prepare only explicitly authorized track/slot targets and SHALL support clearing them through production MIDI controls without rebooting. Preparation MUST verify target identity, clear completion, RAM content, successful persistence and the SD-aware content state before use. Record and overdub fixtures MUST have different verified content preconditions. An empty in-memory slot alone MUST NOT establish an empty record target.

#### Scenario: Clear and repeat
- **WHEN** the operator authorizes particular targets for a repeat campaign
- **THEN** their clear and persistence completion are verified before new seeding or capture
- **AND** unrelated content remains unchanged and boot identity is preserved

#### Scenario: SD residue or persistence failure
- **WHEN** a cleared target still has SD content or persistence did not complete successfully
- **THEN** preparation reports SETUP_INCOMPLETE
- **AND** the runner does not silently reboot, clear again, quarantine data or select a replacement target

### Requirement: Fresh bounded live observations

Live observations SHALL be read-only, capture-gated, bounded in work and storage, and correlated to a fresh request and device identity. Observation delivery MUST NOT block MIDI service. Required transient edges SHALL be distinguishable from state snapshots, and missing required evidence MUST prevent PASS. Repeated runs on the same boot SHALL use fresh run boundaries rather than require another boot banner.

#### Scenario: Same-boot freshness
- **WHEN** a new run begins on an already running device
- **THEN** a fresh correlated response establishes its observation boundary
- **AND** prior-run events cannot satisfy the new run's predicates

#### Scenario: Slow host or missing event
- **WHEN** the host cannot consume observations promptly or required events are lost
- **THEN** firmware observation work stays bounded
- **AND** the runner reports an evidence or timeout failure instead of assuming completion

### Requirement: Identity-based acceptance verdict

PASS SHALL require the complete correlated input-to-execution chain, the intended overlap, continuous transport, content parity, timing gates and successful shutdown. Seed actions MUST NOT count as acceptance actions. Legacy diagnostic wording alone MUST NOT establish accepted or rejected intent. Actual rejected input or duplicate execution SHALL remain a contract failure.

#### Scenario: Accepted deferred stop
- **WHEN** a compatibility diagnostic uses denial wording but the same interval has a proven accepted obligation and eventual single execution
- **THEN** the oracle evaluates that accepted chain against the complete requirements
- **AND** the diagnostic alone neither fails nor passes the interaction

#### Scenario: Incomplete or violated contract
- **WHEN** required identity evidence is missing or an actual rejection, duplicate execution or transport interruption is observed
- **THEN** the oracle returns INSUFFICIENT_EVIDENCE or ACCEPTED_SEQUENCE_FAIL as appropriate
- **AND** the process exits nonzero

### Requirement: Deterministic gesture and failure regression coverage

Acceptance automation SHALL have offline regression coverage for production gesture classification and response ordering, delay, loss, identity mismatch and cancellation. Classification coverage MUST exercise the actual production button processor and mapping with controllable time. Preserved device failures SHALL be replayable without ports.

#### Scenario: Tap threshold and interleaved controls
- **WHEN** gestures occur on either side of a configured classification threshold with another control interleaved
- **THEN** tests verify the actual recognized gesture and ensure the driver does not produce a merged follow-up press

#### Scenario: Replayed former failure
- **WHEN** a preserved queued/reaffirmed-switch or misattributed-interval trace is replayed
- **THEN** it cannot receive PASS from the acceptance oracle

### Requirement: Bounded cancellation and immutable evidence

Every attempt SHALL have a bounded managed lifecycle and SHALL preserve a report on setup failure, exception, cancellation or timeout. Cancellation MUST prevent subsequent test gestures. Capture SHALL remain available through persistence-aware shutdown, and shutdown failure SHALL prevent overall success. Reports SHALL pin firmware/configuration/stimulus/host identities, target authorization, action and observation traces, predicate evidence, inventories, timing, content and teardown results.

#### Scenario: Timeout during a wait
- **WHEN** an action or managed-run deadline expires
- **THEN** pending work cannot send another test gesture
- **AND** a partial non-success report survives safe shutdown

#### Scenario: Verify-only replay
- **WHEN** an immutable bundle is verified again without device ports
- **THEN** the same versioned predicates produce the same verdict

### Requirement: Repeated hardware qualification

Qualification SHALL require three consecutive complete PASS results for each record and overdub leg on the same boot and pinned configuration, without manual correction. All attempts SHALL be retained. A failed attempt SHALL reset the affected consecutive count. Automatic fixture-profile changes or unbounded retry-until-pass behavior MUST NOT hide failures.

#### Scenario: Repeat campaign
- **WHEN** each leg completes three consecutive passing attempts with scoped same-boot preparation
- **THEN** the report identifies all six qualifying bundles, mixed-mode evidence and the complete attempt history
- **AND** parent runtime acceptance remains subject to its existing content and timing gates
