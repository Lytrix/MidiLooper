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
SHALL be an execution bound computed from PSRAM arithmetic, not a policy guess. The envelope to be served
is **up to two sounding slots per track across up to sixteen tracks** -- up to thirty-two sounding leases --
plus the overlap a transfer requires.

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

#### Scenario: Simultaneous-playback cap is stated, not hidden
- **WHEN** window sizing cannot serve two sounding slots on each of sixteen tracks at once
- **THEN** the achievable simultaneous-playback capability is stated as a bound (`k` slots per track)
- **AND** it is not presented as a configurable number with no arithmetic behind it

### Requirement: Role transfer never requires stopping playback
Switching track or slot, starting record or overdub, and undo/redo SHALL all be performed while transport
continues, without MIDI timing degradation. The single complex role -- the resumable small-packet logic
inside MIDI boundaries that serves record, overdub, passes, undo/redo and display -- SHALL transfer to the
newly selected slot or track, and the switch SHALL be allocation-free at the point of transfer.

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
