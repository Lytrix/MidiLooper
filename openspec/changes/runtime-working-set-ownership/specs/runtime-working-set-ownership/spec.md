## ADDED Requirements

### Requirement: Inactive recorded slots retain only canonical and bounded metadata
A recorded slot that is not active SHALL retain only its canonical pass metadata and chunk references,
loop length and counts sufficient to select and plan a load, persistence identity and location, and
bounded selection or preview metadata. It SHALL NOT retain runtime view bundles, companion
reconstruction/apply/bridge-map capacity, canonical pass-input scratch, transfer horizon, capture
staging roles, stop-job capacity, a published snapshot root, or a whole-loop visual cache.

#### Scenario: Library growth does not grow working memory
- **WHEN** recorded slots are added until the library holds at least sixty slots of up to sixty-four bars
- **THEN** boot and steady-state memory remain approximately flat apart from canonical metadata and
  canonical content
- **AND** the growth is not proportional to a per-slot runtime working set

#### Scenario: A slot that was never activated holds no working set
- **WHEN** a slot has recorded content and has never been selected, armed, played or admitted
- **THEN** it holds no runtime working set
- **AND** it can still be selected, described and loaded

### Requirement: Working sets are acquired on activation
A slot SHALL acquire a bounded runtime working set when it becomes active: selected for playback, about
to play, armed, recording, overdubbing, or holding a live deferred owner. Acquisition SHALL occur at a
cold boundary where allocation is permitted, never on a timing-critical MIDI edge.

#### Scenario: Activation acquires exactly one working set
- **WHEN** an inactive slot with recorded content becomes active
- **THEN** one bounded working set is acquired for it
- **AND** the total number of working-set holders increases by exactly one

#### Scenario: Activation never allocates on a timing edge
- **WHEN** a slot becomes active as a consequence of a transport or note event
- **THEN** any required allocation happens before that event is serviced
- **AND** the serving path performs no working-set allocation

### Requirement: Working sets are reusable across slots
The working-set pool SHALL be provisioned for the active envelope rather than for the recorded library,
and capacity released by one slot SHALL be reusable by another.

#### Scenario: A deactivated slot's capacity serves the next activation
- **WHEN** slot A's working set is released and slot C then becomes active
- **THEN** C's working set reuses A's released capacity
- **AND** pool live bytes do not grow compared with the single-active-slot steady state

#### Scenario: Active envelope has a ceiling
- **WHEN** the number of simultaneously active slots reaches the configured cap
- **THEN** further activation is refused or queued according to the declared policy
- **AND** the refusal is reported rather than resolved by allocating beyond the envelope

### Requirement: Working sets are released when no live owner remains
A working set SHALL be released when its slot leaves the active set and no live playback, capture or
deferred owner still requires it. Owner liveness SHALL be an explicit predicate per owner kind, not a
heuristic.

#### Scenario: Release waits for a deferred owner
- **WHEN** a slot leaves the active set while a capture stop job, deferred stop commit, undo root or
  prepared playback handover still owns its working set
- **THEN** the working set is not released
- **AND** it is released when the last such owner retires

#### Scenario: Release frees the whole set, not part of it
- **WHEN** the last live owner of a slot's working set retires
- **THEN** every part of that working set returns to the pool
- **AND** no part remains reserved for the inactive slot

### Requirement: Whole-loop derived state is not retained by inactive slots
An inactive slot SHALL NOT retain a published snapshot root or a whole-loop visual cache. Re-activation
SHALL re-derive or re-hydrate that state on the existing lazy-hydration path.

#### Scenario: Demoted slot re-activates with identical content
- **WHEN** a slot is demoted while inactive and later re-activated
- **THEN** its playback content is identical to before the demotion
- **AND** the re-derivation does not occur on a timing-critical edge

#### Scenario: Snapshot storage tracks the active set
- **WHEN** the recorded library grows while the active set does not
- **THEN** snapshot slab bytes do not grow proportionally to the recorded library

### Requirement: Resident canonical event content is bounded and streamed
Canonical event content that does not fit the resident event store SHALL be loaded on demand from
storage, so that resident canonical capacity is a bounded working set rather than a function of the
recorded library.

#### Scenario: More long loops than the resident store can hold
- **WHEN** the library holds more long loops than the resident event store has capacity for
- **THEN** inactive loops' events are not held resident
- **AND** loading an active loop's events succeeds within the existing admission contract

#### Scenario: Canonical content is not lost by non-residency
- **WHEN** a loop's events are not resident because it is inactive
- **THEN** its canonical content is recoverable from persistent storage
- **AND** a reload produces byte-identical content

### Requirement: The scaling law is measured, not assumed
The change SHALL provide instrumentation that reports working-set bytes by owner, working-set holder
count, and per-loop derived bytes, and the acceptance gate SHALL be a measured scaling curve.

#### Scenario: Curve is flat in recorded slots and stepped in active slots
- **WHEN** the library grows to sixty long slots with a constant active set
- **THEN** the reported working-set curve is flat apart from canonical metadata and content
- **AND** each activation adds one step and each deactivation removes one

#### Scenario: Oversized residency is detectable
- **WHEN** a slot retains a working set while inactive
- **THEN** the instrumentation reports it with the owning slot and owner kind
- **AND** the condition is testable without a device run
