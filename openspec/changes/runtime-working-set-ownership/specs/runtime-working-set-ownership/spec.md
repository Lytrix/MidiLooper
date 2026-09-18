## ADDED Requirements

### Requirement: Inactive recorded slots retain only canonical descriptors and bounded metadata
A recorded slot that is not in an active role SHALL retain only canonical pass identities/descriptors
sufficient to locate or rehydrate its persisted content, loop and pass summary, persistence identity and
location, and bounded browser metadata. It SHALL NOT retain runtime view bundles, companion
reconstruction/apply/bridge-map capacity, canonical pass-input scratch, transfer horizon, capture staging
roles, stop-job capacity, a published snapshot root, or a whole-loop visual cache.

#### Scenario: Cold identity is a descriptor, not a resident chunk reference
- **WHEN** an inactive slot's canonical event body is not resident
- **THEN** the slot's cold identity still locates or rehydrates that content from persistent storage
- **AND** the cold contract does not depend on a chunk reference into a resident event store

#### Scenario: Library growth does not grow working memory
- **WHEN** recorded slots are added with a constant active set
- **THEN** working-set residence stays flat apart from canonical metadata and content
- **AND** growth is not proportional to a per-slot runtime working set

### Requirement: Selection is cheap and does not acquire a working set
Plain UI selection SHALL acquire only cold metadata plus a bounded index or preview. A playback or capture
working set SHALL be acquired at a cold admission boundary -- about to sound, arm, or explicit preload --
and never merely because a slot is selected.

#### Scenario: Browsing does not churn working sets
- **WHEN** selection moves across many recorded slots without playback, arming or preload
- **THEN** no playback or capture working set is acquired or released
- **AND** selection latency does not depend on working-set availability

#### Scenario: About-to-sound is the admission boundary
- **WHEN** a selected slot is about to sound, or is armed, or is explicitly preloaded
- **THEN** a bounded working-set lease is acquired before it is used
- **AND** the acquisition happens where allocation is permitted, not on a timing-critical edge

### Requirement: Working sets are window-sized so the two-slots-per-track envelope fits PSRAM
Working-set capacities SHALL be derived from the resolved window rather than a whole-loop event ceiling, so
a slot's resident cost is independent of its loop length, and the number of simultaneously resident leases
SHALL be an execution bound computed from PSRAM arithmetic, not a policy guess. The envelope to be served is **N sounding slots**, where the change **derives** the largest N the budget
supports and states it as a capability. Today N is 8 (8 tracks x 1 sounding slot); the design must not
preclude 16, 32, 48 or 64, because the target is that slots behave as virtual tracks and can sound together
whenever set active. Also budgeted: one live-capture lease, one deferred-finalization admission with a
serialized queue, one interactive lease where present, and the measured transfer overlap. The budget is per
lease class: only the playback term is multiplied by N, so only it must be small and length-independent.

#### Scenario: The lease budget closes
- **WHEN** fixed overhead, canonical allowance, transient and deferred headroom, and the worst-case lease
  size are summed for the declared active envelope
- **THEN** the total fits the safe PSRAM budget
- **AND** the derivation is recorded as the basis for the active cap

#### Scenario: Loop length does not change the resident cost
- **WHEN** two slots of very different loop lengths (for example four bars and sixty-four bars) are
  sounding on the same track
- **THEN** their resident working sets are of comparable size
- **AND** playback crosses window boundaries without an audible or timing discontinuity

#### Scenario: The sounding-slot capability is derived and stated
- **WHEN** the per-class budget is computed
- **THEN** the largest supportable N sounding slots is derived from it and stated as a capability
- **AND** N is limited by available PSRAM rather than by an arbitrary constant, so the count is a policy
  choice up to that bound

### Requirement: Role transfer never requires stopping playback
Switching track or slot, starting record or overdub, and undo/redo SHALL all be performed while transport
continues, without MIDI timing degradation. The relevant role -- the resumable small-packet logic inside MIDI boundaries that serves record, overdub,
passes, undo/redo and display -- SHALL transfer to the newly selected slot or track, and the switch SHALL be
allocation-free at the point of transfer. Transfer SHALL NOT steal resources from a still-live capture or
deferred-finalization owner.

#### Scenario: Switching while playing does not stop transport
- **WHEN** a different track or slot is selected while playback is running
- **THEN** transport continues and playback does not stop
- **AND** the inbound role is provisioned before the outbound role is released

#### Scenario: Complex role transfers rather than being rebuilt
- **WHEN** the complex role moves to another slot or track
- **THEN** the resumable boundary logic is transferred to the new holder
- **AND** the new holder can serve record, overdub, passes, undo/redo and display without a stop

#### Scenario: No timing degradation across a switch
- **WHEN** track/slot switching, record/overdub start, and undo/redo are exercised while playing
- **THEN** the MIDI-boundary timing evidence reports no degradation and no overflow
- **AND** the switch path performs no working-set allocation

### Requirement: Immutable readiness is owed per affected tick before the playhead returns to it
After a recording, overdub or edit changes content at a tick, an updated immutable representation covering
that tick SHALL be available by the time the current tick moves over that tick again. Playback SHALL consume
only immutable events, and SHALL NOT require a resident whole-loop immutable root.

#### Scenario: An update is immutable before the playhead returns
- **WHEN** content at tick T changes while the playhead is elsewhere in the loop
- **THEN** an immutable representation covering T is ready before the playhead reaches T again
- **AND** playback consumes only that immutable representation

#### Scenario: Playback never reads a partially updated representation
- **WHEN** an update covering a tick is still being produced as resumable deferred work
- **THEN** playback uses an immutable generation rather than the in-progress one
- **AND** no torn or partially applied state reaches the MIDI output

#### Scenario: The readiness unit is an interval, not a whole loop
- **WHEN** a sounding slot needs playback for a long loop
- **THEN** its resident immutable working set is bounded by the lookahead and the affected intervals
- **AND** it does not require a whole-loop root to be resident

#### Scenario: Checkpoint authority is preserved without being resident
- **WHEN** undo or save requires an immutable state
- **THEN** the checkpoint authority is available
- **AND** playback does not depend on a whole-loop root being resident

#### Scenario: Existing deferred publication machinery is reused
- **WHEN** the readiness mechanism is implemented
- **THEN** it reuses the existing resumable deferred publication path rather than adding a second
  incremental-publication system
- **AND** what could not be reused is recorded explicitly

#### Scenario: Missed-deadline behaviour is defined
- **WHEN** an update is not ready when the playhead reaches its tick
- **THEN** the recorded rule applies (the previous immutable generation stays authoritative for that pass,
  the span is held, or the deadline is extended)
- **AND** the user-visible consequence of that rule is documented rather than incidental

### Requirement: Runtime roles are separate classes with declared cardinalities
Working-set resources SHALL be owned as separate role classes -- playback window, live capture, deferred
finalization, interactive edit/undo/display -- each with a declared cardinality, rather than as one
monolithic per-slot lease. A slot MAY hold several classes simultaneously.

#### Scenario: A playback lease carries no capture or deferred resources
- **WHEN** a slot is sounding as one of up to two playback slots on its track
- **THEN** its playback window lease carries no capture staging, stop-job, companion-history or
  deferred-finalization capacity
- **AND** those resources are held only by the role that needs them

#### Scenario: Deferred finalization runs alongside a live successor
- **WHEN** a predecessor slot is finalizing deferred work while a successor slot records live
- **THEN** both hold their own leases concurrently
- **AND** the successor-re-entry capability is preserved rather than serialized

#### Scenario: The finalization lease rotates between captures
- **WHEN** slot A stops and acquires the single finalization lease, slot B starts and records, then A finishes
- **THEN** B may acquire that same finalization lease when it stops
- **AND** the supported concurrency is one finalizing predecessor plus one live successor, not two
  simultaneously finalizing captures

#### Scenario: A stop queues behind a finalizing predecessor
- **WHEN** a stop is requested while the single finalization admission is held by a predecessor that has not
  finished
- **THEN** the stop is queued and serviced in series when the predecessor releases
- **AND** the requesting slot remains recording or overdubbing until its stop is serviced

#### Scenario: A full finalization queue is the denial case
- **WHEN** the finalization queue is full
- **THEN** the stop is denied non-destructively with the slot still recording/overdubbing
- **AND** the condition is counted and reported

### Requirement: Dirty canonical generations pin residency
Once persistence is the backing authority, canonical content SHALL be evicted only after persistence has
durably acknowledged the exact canonical generation being evicted. Unpersisted content SHALL pin residency,
and a persistence failure or delay SHALL reduce available residency capacity rather than discard accepted
input.

#### Scenario: Dirty content is not evicted under pressure
- **WHEN** a slot's canonical content is dirty (a deferred save has not durably completed)
- **AND** memory pressure requests eviction of that content
- **THEN** the content remains resident
- **AND** accepted input is never lost to memory pressure

#### Scenario: Acknowledgement is generation-specific
- **WHEN** persistence durably acknowledges generation G
- **THEN** generation G becomes eviction-eligible
- **AND** a later mutation G+1 pins residency again

#### Scenario: Reload after eviction is exact
- **WHEN** an acknowledged generation is evicted and later reloaded
- **THEN** the reloaded content matches byte-for-byte

### Requirement: Failed admission is non-destructive and preserves the armed state
A required resource set SHALL be derived and acquired as a complete set. On failure there SHALL be no
partial authority, the slot SHALL remain armed, and the failure SHALL be reported as an explicit
resource wait or denial. Partial capacity SHALL be legal only where an individual owner explicitly supports
a smaller bounded grant.

#### Scenario: Short admission leaves the slot armed
- **WHEN** the required resource set for starting a recording cannot be acquired
- **THEN** no partial authority is created
- **AND** the slot remains armed and reports a resource wait or denial rather than leaving the armed state

### Requirement: Working-set leases are generation-qualified
A lease SHALL be identified by slot, generation and role, and SHALL be transferred and released by that
identity. A release or mutation whose generation does not match the current lease SHALL be rejected and
reported rather than applied.

#### Scenario: Stale cleanup cannot touch recycled storage
- **WHEN** slot A releases its lease and a later generation of the same storage is leased to slot C
- **THEN** a late cleanup originating from A cannot release or mutate C's lease
- **AND** the rejected release is observable

#### Scenario: Roles pin their own lease only
- **WHEN** a deferred owner holds a lease
- **THEN** it pins the lease it owns for its lifetime
- **AND** it cannot pin or release a lease owned by a different role or generation

### Requirement: Working sets are released when no live owner remains
A lease SHALL be released when its slot leaves the active set and no live playback, capture or deferred
owner still requires it. Owner liveness SHALL be an explicit predicate per owner kind.

#### Scenario: Release waits for a deferred owner
- **WHEN** a slot leaves the active set while a capture stop job, deferred stop commit, undo root or
  prepared playback handover still owns its lease
- **THEN** the lease is not released
- **AND** it is released when the last such owner retires

#### Scenario: Release is total
- **WHEN** the last live owner retires
- **THEN** every part of that lease returns to the pool
- **AND** no part remains reserved for the inactive slot

### Requirement: Undo history residency is governed by an explicit cold policy
The behaviour of undo/redo history when a slot goes cold SHALL be an explicit, recorded policy, so that
snapshot roots cannot keep cold slots resident against the scaling law.

#### Scenario: Cold undo cannot defeat the scaling law
- **WHEN** a slot goes cold with undo history
- **THEN** its undo residency is bounded by the declared policy (a global history budget, compaction to
  pass identities, sealing on demotion, or eviction with reconstruction)
- **AND** cold-slot memory remains flat as the library grows

#### Scenario: The policy is a product decision, recorded
- **WHEN** the cold undo policy is chosen
- **THEN** the choice and its user-visible consequence are recorded as a decision
- **AND** it is not introduced as a side effect of memory cleanup

### Requirement: Inactive slots do not retain whole-loop derived state
An inactive slot SHALL NOT retain a published snapshot root or a whole-loop visual cache. Reactivation
SHALL derive only the bounded runtime representation required by the active consumer; a full published
root SHALL be built only where an authority requires one.

#### Scenario: Reactivation does not rebuild a whole root by default
- **WHEN** a demoted slot becomes active for playback
- **THEN** only the bounded runtime representation needed by that consumer is derived
- **AND** a full published root is not required merely because the slot became active

#### Scenario: Demoted slot re-activates with identical content
- **WHEN** a slot is demoted while inactive and later re-activated
- **THEN** its playback content is identical to before the demotion
- **AND** re-derivation does not occur on a timing-critical edge

#### Scenario: Snapshot storage tracks the active set
- **WHEN** the recorded library grows while the active set does not
- **THEN** snapshot slab bytes do not grow proportionally to the recorded library

### Requirement: Resident canonical content is bounded behind its own architecture gate
Unloading cold canonical event bodies and rehydrating them from storage SHALL be treated as a distinct
architecture phase with its own decision record, because it changes the cold-slot contract from
resident-content to persistence-backed.

#### Scenario: Canonical residency is gated
- **WHEN** the canonical residency phase begins
- **THEN** it has its own architecture gate and decision amendment
- **AND** it is not implemented as an ordinary task of the ownership migration

#### Scenario: Non-resident canonical content is recoverable
- **WHEN** a loop's events are not resident because it is cold
- **THEN** its content is recoverable from persistent storage
- **AND** a reload produces byte-identical content

### Requirement: The scaling law is measured, and the current behaviour is characterized as failing
The change SHALL provide instrumentation reporting working-set bytes by owner, lease holder count, and
per-loop derived bytes, and SHALL first characterize the current architecture as failing the scaling
assertion before implementing the target behaviour.

#### Scenario: Red characterization first
- **WHEN** recorded slot count grows with a constant active set on the current architecture
- **THEN** working-set residence grows, and this is recorded as the failing baseline
- **AND** the same fixture is reused as the acceptance test after the migration

#### Scenario: Curve is flat in recorded slots and stepped in active slots
- **WHEN** the library holds a genuine sixty-plus long slots with a constant active set
- **THEN** the reported working-set curve is flat apart from canonical metadata and content
- **AND** each activation adds one step and each deactivation removes one

#### Scenario: Oversized residency is detectable
- **WHEN** a slot retains a lease or whole-loop derived state while inactive
- **THEN** the instrumentation reports the slot, role and owner kind
- **AND** the condition is testable without a device run
