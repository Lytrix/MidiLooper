## ADDED Requirements

Implementation staging: the final requirements below are umbrella acceptance, not every early slice's
exit gate. Boot admission uses `IMPLEMENTATION_CONTRACT.md` §1. Immutable window publication and
playback-root decoupling depend on the planned, uncreated `windowed-playback-generation` sibling;
canonical eviction depends on S6. Until those gates land, report retained roots/canonical bodies and
make no whole-law or 60-long-slot claim. Final archive requires both dependencies.

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

### Requirement: Working sets are window-sized so the derived sounding-slot envelope fits PSRAM
Working-set capacities SHALL be derived from the resolved window rather than a whole-loop event ceiling, so
a slot's resident cost is independent of its loop length, and the number of simultaneously resident leases
SHALL be an execution bound computed from PSRAM arithmetic, not a policy guess. The envelope to be served is **N sounding slots**, where the change **derives** the largest N the budget
supports and states it as a capability. Today N is 8 (8 tracks x 1 sounding slot); the design must not
preclude 16, 32, 48 or 64, because the target is that slots behave as virtual tracks and can sound together
whenever set active. Also budgeted: one live-capture lease **per track**, one deferred-finalization role
**per interval** (a second concurrent stop on the same track and slot is **accepted and scheduled**, never
denied — DEC-078), one interactive lease where present, and the measured transfer overlap. The budget is per
lease class: each per-holder cost SHALL be multiplied by its maximum simultaneous holder count.
Live capture and finalization SHALL NOT be budgeted as device-wide singletons. Transfer overlap,
retained roots and deferred undo SHALL remain charged until their last owner releases them.

#### Scenario: Concurrent owners are counted independently
- **WHEN** multiple tracks capture or finalize while slot preparations overlap
- **THEN** the budget includes every resident holder and incremental inbound occupancy
- **AND** shared backing is counted once and current whole-loop roots are reported until decoupled
- **AND** an envelope that exceeds capacity is reported as unproved rather than silently priced as one holder

#### Scenario: The lease budget closes
- **WHEN** fixed overhead, canonical allowance, transient and deferred headroom, and the worst-case lease
  size are summed for the declared active envelope
- **THEN** the total fits the safe PSRAM budget
- **AND** the derivation is recorded as the basis for the active cap

#### Scenario: Loop length does not change the resident cost
- **WHEN** sounding slots of very different loop lengths (for example four bars and sixty-four bars) are
  active -- at today's supported topology, across tracks
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

#### Scenario: A missed deadline keeps the previous generation authoritative
- **WHEN** an update is not ready when the playhead reaches its tick
- **THEN** the previous immutable generation remains authoritative for that pass and the new one takes effect
  on the next pass
- **AND** playback is never torn, never held silent, and never deferred in a way that disturbs timing

### Requirement: Resumable work owners conform to one contract
A resumable work owner SHALL declare an identity (`owner`, `generation`, `role`), a cost estimate, an admission
kind and accounting tag, a readiness deadline where its output must become immutable, and it SHALL use the shared
terminal vocabulary with non-destructive failure.

#### Scenario: A new work owner declares the contract
- **WHEN** a work owner is added (windowed readiness, standby, visual tier, or later)
- **THEN** it declares identity, cost, admission kind, accounting tag and deadline
- **AND** it uses the shared terminal vocabulary rather than introducing another variant

#### Scenario: The scheduler can treat owners uniformly
- **WHEN** the scheduler selects work for a turn
- **THEN** it can reason about cost and deadline without per-owner glue
- **AND** staleness is decided by generation rather than by owner-specific rules

#### Scenario: No framework is introduced
- **WHEN** the contract is adopted
- **THEN** owners keep concrete types with preallocated state and no virtual dispatch
- **AND** existing owners are aligned only when they are next touched

### Requirement: A compact visual envelope is retained for every slot, with a capped detail window
A compact per-bar visual envelope SHALL be retained for every recorded slot so that slot switching has immediate
visual feedback without loading canonical content. A rendered detail window SHALL be retained only within a byte
cap with eviction, and SHALL reuse the existing resumable display renderer and validity identity.

#### Scenario: Switching shows the new slot immediately
- **WHEN** the displayed slot changes to one whose detail window is not resident
- **THEN** the envelope for that slot is available immediately and the display reflects the change
- **AND** the detail window is resolved through the existing resumable renderer rather than blocking the switch

#### Scenario: The envelope is affordable for the whole library
- **WHEN** every recorded slot retains an envelope
- **THEN** the total envelope cost is bounded by a small per-bar-per-slot figure and does not scale with loop
  length in a way that threatens the memory law
- **AND** the envelope is accounted as its own budget term

#### Scenario: The detail window is capped and evicted
- **WHEN** the rendered detail windows would exceed the visual byte cap
- **THEN** they are evicted by the declared policy
- **AND** inactive-slot visual memory stays flat as the library grows

#### Scenario: Envelopes are invalidated by mutation
- **WHEN** content covering an envelope's bars changes
- **THEN** the envelope is invalidated and re-rendered
- **AND** a stale envelope is never presented as current

#### Scenario: Existing renderer and identity are reused
- **WHEN** the visual tier is implemented
- **THEN** it reuses the existing idle-sliced renderer and the existing snapshot identity/revision validity
- **AND** adding a parallel renderer or validity counter is recorded as a failure mode

### Requirement: Standby excerpts are bounded, budgeted and immutable
A per-slot standby excerpt MAY be retained so that activation and queued switching need no preparation at the
boundary. Standby SHALL be byte-capped with eviction, accounted as its own budget term, immutable, and
invalidated by any mutation covering its ticks.

#### Scenario: A queued switch needs no preparation at the boundary
- **WHEN** a switch to a slot that holds a valid standby excerpt is executed
- **THEN** the inbound slot's immutable state is already available
- **AND** the switch performs no preparation or allocation at the boundary

#### Scenario: Preparation is scheduled against a known deadline
- **WHEN** a recording or overdub stops, or any mutation changes content at known ticks
- **THEN** the next arrival of those ticks is known and preparation is scheduled against that deadline
- **AND** preparation is performed as resumable deferred work rather than at the switch

#### Scenario: Standby is capped and evicted, not granted to every slot
- **WHEN** the number or density of slots requesting standby would exceed the standby byte cap
- **THEN** standby is evicted by the declared policy (LRU or candidate set)
- **AND** the total remains within the cap so inactive-slot memory stays flat as the library grows

#### Scenario: A mutation invalidates standby
- **WHEN** content covering a standby excerpt's ticks changes
- **THEN** the excerpt is invalidated and not used for playback
- **AND** the slot falls back to the previous immutable generation until re-prepared

#### Scenario: Cold-slot standby depends on descriptor rehydration
- **WHEN** standby is requested for a slot whose canonical content is not resident
- **THEN** the excerpt is derived through descriptor-based rehydration
- **AND** an all-slots standby policy is not claimed before that path exists

### Requirement: The readiness window size is configurable and traded deliberately
The immutable-readiness window SHALL be configurable rather than fixed to bars, and its size SHALL be chosen
from measurement because it trades resident bytes, achievable switch quantisation, and publication load.

#### Scenario: Shorter windows enable finer queued switching
- **WHEN** queued slot switching is requested at a finer quantisation (for example 1/16 or 1/32 beat)
- **THEN** the readiness window is short enough for the inbound slot's immutable state to be ready at that
  boundary
- **AND** the deadline for that quantisation is stated in ticks and in milliseconds

#### Scenario: Window size is measured, not assumed
- **WHEN** the window size is chosen
- **THEN** the smallest size that still meets the readiness deadline under load is recorded
- **AND** the resulting publication load is reported alongside the resident-bytes and granularity effects

### Requirement: Every user interaction is accepted
Every user interaction -- stop, record/overdub start, slot or track switch, undo/redo -- SHALL be **accepted** when
it is made. Acceptance SHALL NOT depend on whether the owning resources are free at that instant. **When** the
interaction executes SHALL be decided by the playhead-scheduled MVCC pipeline, by resolving it against the playhead
deadline and the earliest valid interval boundary. A busy owner SHALL delay execution; it SHALL NOT refuse the
interaction, and the user SHALL NOT be required to re-issue it.

Accepted requests SHALL persist until execution, explicit supersession as defined below, or session
reset. Stop-before-switch-before-start is execution order; it SHALL NOT discard distinct continuations.
The interval stop obligation remains Track-owned; switch/start destinations remain in their existing
manager-owned pending state, including the gap where no interval is live.

#### Scenario: Stop switch and start compose
- **WHEN** Stop(A), Switch(B), Start(B) are accepted before the worker can execute them
- **THEN** Stop binds to A before B becomes active, then Start executes on provisioned B once
- **AND** consuming Stop does not consume the switch or start request

#### Scenario: A later stop supersedes an unexecuted start
- **WHEN** Stop(A) and a following Start are pending and another Stop arrives before Start commits
- **THEN** the unexecuted Start is explicitly superseded and reported, and the original stop remains accepted
- **AND** no empty interval is manufactured; if Start already committed, Stop instead targets that new interval

#### Scenario: A replacement switch cannot misdirect a start
- **WHEN** Switch(C) replaces pending Switch(B) while a Start explicitly targets B
- **THEN** C replaces the selection request, B's unpublished preparation retires, and Start(B) is explicitly
  superseded and reported rather than silently retargeted to C
- **AND** a subsequent Start(C) is accepted independently

#### Scenario: Transport stop supersedes unexecuted starts
- **WHEN** transport stops while stop/switch/start continuations are pending
- **THEN** the live stop is retargeted to Stopped and unexecuted starts are explicitly superseded
- **AND** pending selection uses the stopped-selection path and a later epoch cannot resurrect canceled starts

#### Scenario: Runtime regeneration retains the capture owner
- **WHEN** a live deferred job loses its runtime and allocation remains unavailable
- **THEN** that same live ring entry retains its sealed extent, identity and ownership receipts while retrying
- **AND** load/release cannot destroy its owner; a diagnostic terminal never dequeues the job or discards content
- **AND** when capacity returns, bounded regeneration resumes without re-admission or replaying a completed
  commit, and normal retirement occurs after exactly-once publication and resource release

#### Scenario: Acceptance is unconditional
- **WHEN** an interaction is made while the owner it needs is busy
- **THEN** the interaction is accepted immediately
- **AND** it is scheduled to execute at the earliest valid interval boundary rather than rejected

#### Scenario: Execution timing is the pipeline's decision
- **WHEN** an accepted interaction cannot execute at the tick it was made
- **THEN** the playhead-scheduled MVCC pipeline resolves when it executes, against the playhead deadline and the
  interval boundary
- **AND** the user's request is not re-read at that point; the accepted request is what executes

#### Scenario: A busy owner delays but never rejects
- **WHEN** the finalization resources are busy (`finalizer_busy`, `transfer_denied`, `canonical_output_busy`,
  session-exit handoff)
- **THEN** these are **scheduling states**, not user-visible denials
- **AND** the interaction executes when the owner frees, with no second attempt by the user

#### Scenario: A park terminal never cancels an accepted interaction
- **WHEN** the accepted stop's park reaches its diagnostic terminal (`accepted_stop_park_terminal`) before the
  finalizer frees
- **THEN** the accepted interaction remains authoritative and executes exactly once when the owner frees
- **AND** the park is a fault/escalation state of the execution mechanism, never an abandonment of the stop, and
  the interval is not left recording in place of the accepted stop
- **AND** the user is never asked to press again

#### Scenario: A switch is accepted without moving the sounding slot
- **WHEN** a slot or track switch is requested while a capture interval is live
- **THEN** the switch is accepted and its target is carried until it executes
- **AND** the active sounding slot does not move until that interval's stop has been admitted and bound to the
  interval's own slot (no second request, no dropped target)

#### Scenario: A scheduled interaction needs no stored interval
- **WHEN** an accepted interaction waits for its boundary
- **THEN** it holds only its own identity (destination, slot, switch target where applicable, interval
  generation, transport epoch)
- **AND** it does not retain a stopped interval's sealed raw range, staged pass, session receipt or canonical
  transfer state, so no new capacity is required

#### Scenario: Deferrals are observable as scheduling, not refusal
- **WHEN** an accepted interaction executes after one or more busy-owner states
- **THEN** the waits are counted and reported as scheduling latencies, attributed to the holding owner
- **AND** no capacity-driven user-visible denial exists in the diagnostic vocabulary

#### Scenario: A denial or deferral names the identity it applies to
- **WHEN** a stop, admission or deferral outcome is reported for an interval
- **THEN** the row carries the **track** and slot that interval belongs to, so a multi-track run can attribute
  it without inferring from position
- **AND** a family that cannot carry that identity is not used as the evidence for attribution, because a
  slot index alone is ambiguous when two tracks use the same slot

#### Scenario: Latency is measured against a declared per-action budget
- **WHEN** an interaction class is characterized
- **THEN** its acceptance-to-execution latency is measured against a declared expectation (stop ~microseconds,
  undo/redo ~1-2 s when cold, load ~seconds)
- **AND** exceeding the expectation is a reported event, not a silent wait
- **AND** presenting the wait in the UI is **out of scope** for this change: it is gated on that measurement plus
  a mitigation review showing the budget cannot be met

#### Scenario: Inapplicable interactions are not refusals
- **WHEN** an interaction has nothing to act on (an undo with no history, a switch to the already-selected slot)
- **THEN** it is an empty operation, not a resource-driven refusal
- **AND** it does not violate this requirement

#### Scenario: A hard bound serializes, never refuses
- **WHEN** the accepted interactions reach a hard descriptor bound
- **THEN** they are serialized by the pipeline in order
- **AND** the bound limits how many deferred intervals exist concurrently, never whether the user's interaction
  is accepted

### Requirement: Finalization preserves the single-active-finalizer capacity contract
The change SHALL preserve DEC-067's capacity model -- one **active** finalizer **per interval**, with that track
and slot's single-instance finalization resources bounding ordinary concurrency -- while removing the
user-visible refusal that model used to
produce. A stop meeting a busy owner SHALL be accepted and scheduled, and SHALL execute when the owner frees
without the user re-issuing it. The descriptor ring SHALL be treated as descriptor depth whose tail must be free,
so pending stop *intervals* are never stacked.

#### Scenario: The capacity model is preserved, the refusal is removed
- **WHEN** the change is implemented
- **THEN** one active finalizer and the single-instance resources still bound ordinary concurrency, and no queue
  of stopped intervals or new arbitration is introduced
- **AND** the busy-owner states (`finalizer_busy`, `transfer_denied`, `canonical_output_busy`, session-exit
  handoff) delay an accepted interaction instead of refusing it, verified by stage-name evidence
- **AND** `queue_full` remains exceptional descriptor exhaustion (a descriptor-lifetime defect), not a normal wait

#### Scenario: The fix extends the existing re-drive path
- **WHEN** an interaction is accepted while an owner is busy
- **THEN** it is carried by the existing pending-stop re-drive path (`hasPendingDeferredOverdubStop()` /
  `requestDeferredOverdubStopToStopped()`), extended to cover the accepted-but-not-yet-executed request
- **AND** no second finalization mechanism is introduced

### Requirement: Runtime roles are separate classes with declared cardinalities
Working-set resources SHALL be owned as separate role classes -- playback window, live capture, deferred
finalization, interactive edit/undo/display -- each with a declared cardinality, rather than as one
monolithic per-slot lease. A slot MAY hold several classes simultaneously.

#### Scenario: A playback lease carries no capture or deferred resources
- **WHEN** a slot is sounding as one of the playback slots (today at most one per track)
- **THEN** its playback window lease carries no capture staging, stop-job, companion-history or
  deferred-finalization capacity
- **AND** those resources are held only by the role that needs them

#### Scenario: Deferred finalization runs alongside a live successor
- **WHEN** a predecessor slot is finalizing deferred work while a successor slot records live
- **THEN** both hold their own leases concurrently
- **AND** the successor-re-entry capability is preserved rather than serialized

#### Scenario: The finalization lease rotates between captures
- **WHEN** A and B share a track's existing single-instance finalization resources, A stops, B starts,
  and then A finishes
- **THEN** B may acquire those released resources when it stops
- **AND** this shared-resource rotation is not a device-wide singleton: other tracks' finalizers remain
  independent and their simultaneous residency is included in the budget

#### Scenario: A stop meeting a busy finalizer is accepted, non-destructive and deferred
- **WHEN** a stop is requested while a predecessor still owns resources needed by that stop
- **THEN** the stop is **accepted** with no partial stop and no descriptor taken, and the requesting slot remains
  recording or overdubbing
- **AND** the busy-owner state is reported as a stage (`finalizer_busy`, or `transfer_denied` /
  `canonical_output_busy` / session-exit handoff for the other single-instance resources)
- **AND** the accepted stop executes once the resource frees, without the user re-issuing it

#### Scenario: A finalizer on another track does not block a session on this track
- **WHEN** a predecessor capture on one track is still finalizing and a capture session starts on another track
- **THEN** the second track's session and its stop execute independently, without waiting on the first track's
  finalization
- **AND** the first track's finalization completes exactly once with its content exact, and neither track's
  stages release the other's storage

#### Scenario: The descriptor ring does not imply concurrent finalization
- **WHEN** finalization capacity is reasoned about
- **THEN** `CaptureStopJobs::kCapacity` is treated as descriptor-ring depth whose tail must be **free** before a
  stop is admitted, so pending stop intervals are never stacked
- **AND** `queue_full` is **exceptional descriptor exhaustion**, categorically separate from the ordinary
  directional-capacity denials, never used to explain ordinary finalization wait
- **AND** reaching `queue_full` indicates a descriptor-lifetime defect rather than capacity pressure, and is
  reported as such
- **AND** concurrent finalization is only claimed once a capacity/ownership decision shows where each
  interval's sealed raw range, pending closures, staged pass, session receipt and canonical transfer state live

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

### Requirement: Undo is two-tier -- hot roots, cold logical descriptors
Hot slots MAY retain snapshot-backed undo/redo roots within a bounded hot depth. On cold demotion, entries
SHALL be compacted to logical descriptors that carry no `LoopSnapshot` reference and own no snapshot slab, and
every undo/redo root belonging only to that slot SHALL be released.

#### Scenario: Compaction on demotion releases snapshot residency
- **WHEN** a slot with undo history leaves the active set
- **THEN** its snapshot-backed undo roots are released once no active or deferred owner requires them
- **AND** it retains compact descriptors describing the same logical history

#### Scenario: Reactivation does not eagerly rebuild the stack
- **WHEN** a cold slot becomes active again
- **THEN** only its current musical state is prepared for playback
- **AND** historical states remain compact until an undo or redo requests one

#### Scenario: Undo of a cold-derived entry reconstructs off-clock
- **WHEN** undo is requested on a state held only as a cold descriptor
- **THEN** the historical state is reconstructed or selected off-clock and then published
- **AND** the reconstructed root carries a truthful `coveredPassRevision` (DEC-073) rather than inheriting the
  superseded root's authority

#### Scenario: A descriptor describes authority, not a cached root
- **WHEN** a cold undo descriptor is created
- **THEN** it identifies the canonical state to select or reconstruct (committed pass identity/set, pass
  ordering or selection revision, note-edit commit identity where applicable, capture/session generation)
- **AND** it holds no snapshot pointer, slab identity or revision stamp as a claim to be authoritative

### Requirement: Cold undo is globally bounded and redo survives demotion
Cold undo SHALL be bounded by a per-slot depth and a device-wide byte budget, with the oldest cold entries
evicted first; the budget SHALL charge both descriptor bytes and any canonical bytes a descriptor pins. ClearSlot
roots, which are excluded from the cold tier, SHALL be bounded by their own explicit cap. Redo history SHALL be
preserved compactly across demotion and invalidated only by a new committed edit or capture, apart from the
declared ClearSlot cap eviction, which is reported.

#### Scenario: The oldest cold undo is the pressure victim
- **WHEN** cold-undo residency exceeds the device-wide budget
- **THEN** the oldest **prefix** is evicted — the oldest prefix that can be removed while leaving the oldest
  retained descriptor independently reconstructable from canonical state (transaction-aware, not merely
  oldest-entry-first)
- **AND** the current musical state, dirty canonical content and active hot undo are never evicted by undo
  pressure

#### Scenario: ClearSlot hot roots are capped
- **WHEN** clear-slot undo entries are retained outside the cold tier
- **THEN** they are bounded by an explicit count/byte cap with a declared eviction point, independent of memory
  pressure
- **AND** eviction drops the whole **entry** (cursor repaired, protected reserve preserved, `nextEntryId` reset
  when the stack empties, loss reported) — never the before-root alone, which would silently wipe the slot on a
  later undo

#### Scenario: Demotion does not change editing semantics
- **WHEN** a slot is demoted and later re-activated
- **THEN** its redo history is still available
- **AND** redo is invalidated only when a new edit or capture commits

#### Scenario: Reconstruction failure defers the undo, it does not discard it
- **WHEN** reconstruction of a cold undo state cannot be admitted for memory reasons
- **THEN** the undo is **accepted** and the current musical state is unchanged while it waits
- **AND** the wait is reported as `undo_resource_denied` -- a scheduling state named for the resource, not a
  user-visible refusal (invariant 31)
- **AND** the cold descriptor is not consumed, so the accepted undo executes once preparation is admitted and
  never requires a second attempt

#### Scenario: A job-owned root is not released by demotion
- **WHEN** a slot is demoted while a deferred owner still holds an undo root it owns
- **THEN** that root is not released
- **AND** it is released when that owner retires

#### Scenario: Undo history is not persisted
- **WHEN** the device is saved and rebooted
- **THEN** the restored slot derives undo units from its saved active pass records
- **AND** redo starts empty; in-session Undo makes its action available for Redo
- **AND** another slot loading does not erase that runtime Redo
- **AND** cold descriptors are not part of the persisted contract

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

#### Scenario: The envelope claim is measured against the pool and the id space
- **WHEN** a resident-envelope claim is made (for example all eight tracks by eight slots each holding
  sixty-four bars at three notes per sixteenth note)
- **THEN** the claim is stated as a count of events and tested against **both** the external pool and the
  committed chunk-id ceiling, and a blocker in either is reported as a blocker
- **AND** a density that exceeds the committed chunk-id ceiling is reported as a **structural** limit, not as
  a byte budget to be re-based

#### Scenario: The current layout is characterized as failing the stated envelope
- **WHEN** a stated envelope is compared against the current data layout
- **THEN** the failing characterization is recorded with its arithmetic and its **independent** blockers, each
  of which is fatal on its own
- **AND** the levers that would close the gap are named with measured deltas, so the gap is a decision rather
  than an open question

#### Scenario: A representation that duplicates committed content is charged for it
- **WHEN** a derived representation stores a copy of a committed event rather than a reference to it
- **THEN** that duplication is charged in the per-event budget
- **AND** removing it is treated as a first-class capacity lever rather than an optimization

### Requirement: Residency evidence distinguishes physical pool accounting from heap admission
Wrapped external-pool live payload accounting SHALL exclude internal fallback allocations,
retain the allocation's original tag until retirement, and reuse available shadow entries.
Heap-tail headroom SHALL NOT be reported as total reusable malloc capacity. Allocation failure
attribution SHALL preserve allocation routing and avoid pool walks on the interval path.

#### Scenario: Core fallback is freed directly
- **WHEN** extmem_malloc returns an internal pointer and its owner later uses free
- **THEN** the pointer contributes no physical external-pool live bytes or tag bytes

#### Scenario: Shadow address is reused
- **WHEN** an address is replaced under a different tag or reused after table saturation
- **THEN** old class and tag attribution are retired and available tombstones remain reusable

#### Scenario: Capture build exhausts an allocation
- **WHEN** the wrapped allocation returns null
- **THEN** the refusal includes a caller address resolvable against the exact firmware ELF
- **AND** this diagnostic does not alter admission thresholds or infer an overdub refusal from a recording failure

### Requirement: Allocator inspection is explicit and stopped-only
Session-capture heap inspection SHALL run only on explicit request when transport, track
capture/playback/finalization, external clock and storage work are inactive. It SHALL not
allocate or change allocator state, SHALL bound traversal, and SHALL distinguish incomplete
inspection from measured free space. Live telemetry SHALL continue using bookkeeping only.

#### Scenario: Active request refuses without reading allocator metadata
- **WHEN** heap inspection is requested while transport or another guarded activity is active
- **THEN** it returns a named refusal without calling either allocator scanner

#### Scenario: Settled stopped inspection exposes fragmented space
- **WHEN** an explicit request is admitted at a settled stopped checkpoint
- **THEN** total free space and largest free extent are reported separately per allocator
- **AND** unused internal arena tail and allocator top chunk are reported separately
- **AND** no single observation is reported as proof of a leak

### Requirement: A diagnostic is bounded so it cannot evict the evidence it explains
A capture diagnostic that a repeating owner can emit SHALL be bounded, in cadence **and** in total per episode,
so that a condition which does not advance cannot flood the capture ring and displace the rows the run is read
for. A bounded family SHALL still name the condition it bounds, at least once, using the identity available at
that point. Bounding the diagnostic SHALL NOT bound, shorten, retire or cancel the work it reports.

#### Scenario: A stalled episode is bounded per phase
- **WHEN** a phase or job keeps being serviced without advancing
- **THEN** its per-grain rows stop after a bounded number for that phase and fall to a slow heartbeat
- **AND** the ring still carries the rows the run is read for, instead of a per-grain flood
- **AND** the serviced work is unchanged: the bounded diagnostics never cancel, retire or shorten what the
  episode owes

#### Scenario: A stalled episode names itself
- **WHEN** a phase's row budget is exhausted and the phase still has not advanced
- **THEN** exactly one aggregate row is emitted naming that phase, with its call count and its span
- **AND** the condition is attributable without needing a second run

#### Scenario: Dropping a diagnostic family is a build option
- **WHEN** an acceptance run's evidence is carried by other rows
- **THEN** the per-grain family can be dropped entirely by configuration
- **AND** dropping it changes only the diagnostics, never the serviced work

### Requirement: Released view reservations retire owned side arrays
When an unselected whole-take playback reservation is released, the implementation SHALL release its origin and canonical-rank arrays along with event storage and SHALL permit later re-admission. Selected authoritative bridges SHALL prevent release without mutation.

#### Scenario: Release and re-admit whole-take views
- **WHEN** an unselected whole-take reservation is released and later admitted again
- **THEN** released storage and side-array pointers are null before re-admission, and newly admitted side arrays support the full declared capacity

### Requirement: Display compose reuses owned frame storage
Draw-source composition SHALL reuse the existing pending-paint backing for synchronous scratch while retaining the live composed frame until its replacement is ready. Paint transforms SHALL remain isolated from the composed representation.

#### Scenario: Stable-size repeated paint
- **WHEN** repeated draw-source frames fit the two existing frame capacities
- **THEN** compose scratch and paint copy require no new frame allocation, and paint transforms leave composed rows unchanged

#### Scenario: Retained publication handoff
- **WHEN** a gather is not adopted because the existing handoff frame remains authoritative
- **THEN** that live frame remains unchanged and scratch storage returns to the paint owner

#### Scenario: Pending paint needs larger backing
- **WHEN** the composed frame exceeds pending-paint capacity
- **THEN** obsolete paint backing is retired before allocating its replacement, while the composed source remains intact

### Requirement: Snapshot construction reuses private index nodes
A snapshot construction SHALL reuse index nodes it has privately copied or created across changes in that construction. Reuse SHALL preserve published parents, independent sibling constructions, membership limits and transactional failure. Completed constructions SHALL NOT mutate their frozen result on further step calls.

#### Scenario: Batched changes share an affected membership
- **WHEN** multiple changes affect the same private pitch/bar membership
- **THEN** subsequent changes reuse its directory, page and membership chain, preserving all references and the chain's tail and page count

#### Scenario: Failure or capacity exhaustion during reuse
- **WHEN** a construction fails after partial private edits or exceeds the membership capacity
- **THEN** it cannot freeze a result and the published parent remains unchanged

#### Scenario: Concurrent private constructions
- **WHEN** two constructions from one published parent advance in interleaved bounded steps
- **THEN** neither construction changes the parent or the other's result

### Requirement: No-clear capacity fixtures verify the selected slot
A capacity fixture with an explicit slot and clearing disabled SHALL refuse record arm when post-track-selection device evidence is absent or identifies another slot. Configured target metadata SHALL NOT substitute for observed selection.

#### Scenario: No-clear recipe leaves another slot selected
- **WHEN** the latest device-time display observation identifies a slot different from the configured target
- **THEN** the fixture fails before sending record-arm or note gestures

#### Scenario: Slot evidence is unavailable
- **WHEN** no valid display observation exists after selecting the track
- **THEN** the no-clear fixture fails before record arm

### Requirement: Boot repair preserves the current library
Before boot cleanup, StorageManager SHALL inspect current runtime metadata. Missing
or corrupt metadata SHALL be reconstructed only after workspace metadata and the
complete 64-slot matrix pass epoch, checksum, format and slot-identity validation.
Repair SHALL preserve existing loop/workspace bytes and unrelated files, use unique
candidate/backup paths, validate before promotion, and use the normal loader after
publication. Replacement runtime settings SHALL use the documented recovery defaults.
Failure with current-workspace evidence SHALL block normal operation and persistence
for the boot rather than saving an empty fallback. Absent workspace evidence SHALL
retain the existing fresh/legacy boot policy.

#### Scenario: Missing metadata with intact saved passes
- **WHEN** runtime metadata is missing and all workspace recovery prerequisites hold
- **THEN** boot publishes a checked metadata candidate, loads the original slots,
  and derives undo from their passes without changing those source files

#### Scenario: Interrupted repair or invalid source
- **WHEN** validation, writing or publication fails
- **THEN** originals remain preserved, persistence is blocked, and a later boot can
  retry from preserved sources without overwriting earlier repair evidence

#### Scenario: Healthy second boot
- **WHEN** the repaired runtime bundle is valid on the next boot
- **THEN** the normal load path retains its settings without reconstructing metadata

### Requirement: Record and overdub share deferred capture finalization

Record and overdub SHALL use the same accepted-interaction and deferred-finalization lifecycle.
Both SHALL allow a live successor alongside an admitted predecessor and accept the successor's
stop while shared resources are occupied. Record SHALL NOT satisfy this requirement through
synchronous full-take publication or a weaker acceptance oracle. Mode-specific record geometry,
record-pass undo and overdub-session undo semantics SHALL be preserved.

#### Scenario: Record predecessor with a live successor
- **WHEN** a record interval A stops and a provisioned successor B starts while A finalizes
- **THEN** A's captured content remains owned by A's identified job, independently of B's live input
- **AND** B's single stop is accepted while A owns the required shared capacity and executes once
  that capacity becomes available
- **AND** transport and sounding playback continue without a second press

#### Scenario: All capture-mode transitions use the same lifecycle
- **WHEN** record-to-record, record-to-overdub, overdub-to-record or overdub-to-overdub transitions occur
- **THEN** each interval has a distinct identity and follows acceptance, admission, bounded preparation,
  exactly-once publication and retirement under the existing capacity model
- **AND** no second finalizer or queue of stopped captures is introduced

#### Scenario: Prepared first bar survives deferred record publication
- **WHEN** record-stop launches playback before canonical finalization finishes
- **THEN** its prepared first-bar view matches the frozen interval's final length and normalized content
- **AND** later playback coverage is supplied before its deadlines from valid owned sources
- **AND** stale generations, held-note closure, short takes and wrap boundaries cause no missing or duplicate notes

#### Scenario: A record stop waits for resources
- **WHEN** record-stop cannot bind because finalization capacity or valid playback preparation is unavailable
- **THEN** the accepted obligation and its live capture remain authoritative until the earliest valid execution boundary
- **AND** no partial freeze, synchronous full-stop fallback, input loss or misleading completed switch occurs
- **AND** execution deadlines remain binding and missed budgets are reported as failures

#### Scenario: Record publication retains record semantics
- **WHEN** a deferred record job publishes after selection or active capture changed
- **THEN** the record pass, record undo with its geometry and matching source handoff become visible consistently
- **AND** persistence and deferred snapshot attachment use that job's committed identity
- **AND** geometry updates and resource release occur once without altering the successor's interval

#### Scenario: Transport stops during either finalizer
- **WHEN** global transport Stop arrives during record or overdub finalization
- **THEN** the existing job retargets to Stopped without losing captured notes, replaying publication or releasing live readers
- **AND** pending starts are superseded according to the existing accepted-interaction rules
