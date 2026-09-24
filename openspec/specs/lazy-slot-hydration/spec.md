## Purpose

Boot-playback-first Commit and on-demand deferred slot hydration so interactive ready does not
require full-set SD drain. Shipped in **unified-commit-lazy-slot-load** (device gate
`session_20260718_224607.log`; A.7 confirm `session_20260718_230145.log`).

## Requirements

### Requirement: Slot hydration lifecycle

The system SHALL model slot hydration as the architectural lifecycle:

`UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY`

- **UNLOADED** — no committed passes; may still need SD (`needsSlotLoad`)
- **HEADER_READY** — metadata (length, bars, …) without committed passes
- **COMMITTED** — immutable passes are canonical; playback, editor, and display MAY use the slot
- **DERIVED_READY** — optional derived reconstructions finished

The system SHALL NOT require a dedicated stored enum field for this lifecycle; implementation MAY use flags, computed properties, or explicit state.

#### Scenario: Interactive ready does not require DERIVED_READY

- **WHEN** boot playback slots reach COMMITTED
- **THEN** interactive UI (including piano roll) MAY proceed
- **AND** DERIVED_READY MAY still be pending

### Requirement: Boot-playback-first Commit

At cold boot, the system SHALL sync-commit the boot playback set (`isBootPlaybackSlot`) before declaring interactive ready. The system SHALL NOT require every SD payload slot to reach COMMITTED before interactive ready. Boot SHALL NOT use the runtime deferred load scheduler for boot playback restore.

MVP MAY perform boot playback sync Commit via the existing `loadLoopSlotFromCurrentSetSd` path.

#### Scenario: Non-playback slots stay unloaded after boot ready

- **WHEN** boot completes interactive ready for a set with SD payloads beyond the boot playback set
- **THEN** boot playback slots are COMMITTED
- **AND** non-playback payload slots MAY remain UNLOADED or HEADER_READY
- **AND** those slots are NOT auto-enqueued for background full-set drain

#### Scenario: Boot ready faster than full-set drain baseline

- **WHEN** a representative multi-slot set boots under capture-serial
- **THEN** time from load start to interactive ready is significantly shorter than the full-set drain baseline (`session_20260718_020628.log`)
- **AND** all audible tracks produce sound on first Play

### Requirement: On-demand deferred slot load priorities

After interactive ready, deferred slot load SHALL honor only:

1. Audible slots still needing Commit (normally empty after boot)
2. Explicitly requested slots (select, focus, or equivalent user/request path)

The system SHALL NOT implement unbounded speculative fill without priority. Background fill SHALL use focus-adjacent then focus-track then forward-track ordering (`computeDeferredRestorePriority`).

#### Scenario: Select unloaded slot while stopped

- **WHEN** the user selects an unloaded SD slot while transport is idle
- **THEN** the system requests deferred load for that slot
- **AND** the slot reaches COMMITTED without reboot
- **AND** display MAY paint from committed passes

The system SHALL background-fill remaining HEADER_READY SD payloads after interactive ready, ordered by:

1. Focus selected slot
2. Left and right neighbors on the focus track (wrap)
3. Other slots on the focus track
4. Other tracks in forward cycle order from the selected track

While any track is recording or overdubbing, the system MAY queue an explicit slot-load request and SHALL defer SD load work until capture is idle.

After interactive ready, deferred restores SHALL proceed as deferred job steps (`LoadLoopJob`) executed through `DeferredJobScheduler::runFrame`: SD payload bytes MAY be read in chunks across multiple main-loop iterations, then parsed under a time budget, then Committed by `StorageManager`. Parse SHALL be time-sliced; finalize / commit-id headroom SHALL NOT walk the PSRAM pool via `sm_malloc_stats_pool`. Loop length SHALL NOT gate whether a slot may restore while PLAYING.

Focus High `LoadLoopJob` work (focused / explicitly requested slot) SHALL advance its **preparation** while a track is PLAYING **only while no restore job is commit-pending**. Background Low `LoadLoopJob` work SHALL be admitted a bounded service slice while any track is PLAYING, audibly sounding, or otherwise transport-active, so that a queued restore cannot be starved for the lifetime of a transport session. Background work that is not admissable in the current turn SHALL leave active/parked/queue progress intact; it SHALL NOT be treated as complete, attempted, or refused. **Commit and cleanup remain bounded only by the runtime's existing commit admission:** a prepare-complete request under active transport waits in a named state for that gate, and this requirement does not claim that it applies while a track is timing-critical. **The commit admission is global, not per job:** while any restore job is commit-pending, the runtime denies the whole load child for that turn, so no other request's preparation advances either; that state SHALL be reported as a named wait (`waiting_job_capacity`) and SHALL NOT be described as queue starvation.

#### Scenario: Select unloaded during PLAYING hydrates via deferred job steps

- **WHEN** the user selects an unloaded slot while a track is PLAYING (no record/overdub)
- **THEN** the load request is retained and remaining payloads are enqueued for priority fill
- **AND** focus High `LoadLoopJob` for that slot MAY advance one chunk (or parse/commit) per `DeferredJobScheduler::runFrame` while PLAYING
- **AND** display MAY update from COMMITTED passes without requiring transport stop

#### Scenario: Select unloaded during RECORDING defers

- **WHEN** the user selects an unloaded slot while any track is RECORDING or OVERDUBBING
- **THEN** the load request is retained
- **AND** SD restore for that slot does not run until capture is idle

#### Scenario: Background restore preparation advances while the gates admit

- **WHEN** a slot with committed SD content is queued for background restore
- **AND** the transport is running with one or more tracks audibly playing
- **THEN** the restore request SHALL receive bounded service slices while the gates admit, rather than being skipped indefinitely
- **AND** its preparation SHALL advance or it SHALL be in a named wait state reported for it (a wait may persist until the commit gate or job-capacity phase clears)
- **AND** playback SHALL continue without requiring the transport to stop

#### Scenario: Applying a prepared restore under active transport is checked at the commit boundary

- **WHEN** a restore's preparation is complete and the transport is timing-critical
- **AND** the runtime has no content-independent bound for the commit operation
- **THEN** the request SHALL wait in a named state for the commit gate rather than being refused, retried unboundedly, or reported as complete
- **AND** the request SHALL be applied at the first turn the gate admits it

### Requirement: Restore requests have identity, named waits, and one terminal outcome

The system SHALL model each queued slot restore as a request identified by
`(track, slot, requestGeneration, setEpoch)`. `requestGeneration` advances only on an **external** change
of that coordinates pair — a user clear, a set change, or a commit by a different owner.
`setEpoch` identifies the loaded current set. A later session reusing the same `(track, slot)` SHALL be a
**different** request.

A request SHALL be in exactly one lifecycle state at a time: **queued**, **running**, **waiting**,
or **terminal**. Queued work awaits selection; running work is advancing an admitted job through
read/parse/commit/cleanup under the applicable gates; waiting work has a named blocking reason.
Exactly one terminal outcome SHALL be recorded per completed or superseded request identity.
A yield between runnable slices SHALL NOT by itself imply a blocking wait or a terminal outcome.

Wait states SHALL carry a named reason and SHALL NOT be terminals: a slot owned by a live owner, capture
active on that slot, the commit gate not admitting (no content-independent bound), **another restore job
holding the commit/cleanup phase so the whole load child is denied that turn**, and a bounded retry after
a transient failure. A wait SHALL NOT convert to a terminal merely because it persisted for a number of
turns or iterations.

Terminal outcomes SHALL be exactly: **committed** (the payload was restored), **unnecessary** (the slot
already has committed content, or the SD payload is absent or invalid), **refused** (a named,
non-retryable reason), and **superseded** (the set changed, the slot was cleared by an external owner, or
a newer request for the same coordinates replaced it). Recording deferral SHALL remain a wait, never a
terminal refusal.

A request SHALL NOT remain queued unexplained across unrelated transport activity, SHALL NOT leave the
queue by being silently marked attempted, and SHALL NOT record a terminal under coordinates a later
session reuses.

**Identity SHALL be validated rather than assumed.** A reset performed by the restore path's **own**
failure cleanup SHALL keep the request's identity and move it to its retry wait; it SHALL NOT read as
`superseded`. Identity SHALL be validated for a queued, an active and a parked job, and again immediately
before publication, so a request cannot bind or publish into a slot whose owner changed while the job was
in flight.

#### Scenario: A queued request leaves the queue exactly once

- **WHEN** a slot restore request is queued and the system processes it to completion
- **THEN** exactly one terminal outcome is recorded for that request's identity
- **AND** the request is not re-enqueued afterwards

#### Scenario: Preparation has a nonterminal running state

- **WHEN** an admitted request advances through reading or parsing across service turns
- **THEN** it is running, not terminal and not assigned a fictitious blocking reason
- **AND** a service yield preserves its identity and progress
- **AND** when a real gate denies further work it transitions to waiting with that reason

#### Scenario: A blocked request waits with a named reason

- **WHEN** a queued restore cannot begin because its slot is owned by a live owner
- **THEN** the request stays queued in a named wait state rather than being marked attempted or refused
- **AND** the wait reason is observable while it waits

#### Scenario: A long wait is not silently converted into a refusal

- **WHEN** a restore request has been in a wait state across many service turns
- **THEN** it remains a wait with its reason
- **AND** it does not acquire a terminal outcome from elapsed time or iteration count alone

#### Scenario: Recording deferral is a wait, not a refusal

- **WHEN** a restore request cannot proceed because a track on that slot is recording
- **THEN** the request is waiting with that named reason
- **AND** it is applied when capture ends

#### Scenario: A reused coordinate pair is a new request

- **WHEN** a request reaches a terminal, the slot is then cleared or committed by a different owner, and a
  restore is requested again for the same `(track, slot)`
- **THEN** the new request has a different identity
- **AND** the earlier terminal does not describe it

#### Scenario: A set change supersedes outstanding requests

- **WHEN** the loaded current set changes while restore requests are queued
- **THEN** those requests reach `superseded` rather than `refused` or `committed`
- **AND** the reason is observable

#### Scenario: The restore path's own failure cleanup does not supersede its request

- **WHEN** a restore attempt fails and the restore path resets the slot as part of its own cleanup
- **THEN** the request keeps its identity and moves to its retry wait
- **AND** it does not reach `superseded`

#### Scenario: Identity is validated before publication

- **WHEN** a job has prepared a payload and the slot's owner has changed while the job was in flight
- **THEN** the request is superseded before publication
- **AND** it publishes nothing into the replacement content and reports the reason

#### Scenario: A second request waits on job capacity rather than appearing starved

- **WHEN** one restore is commit-pending under active transport and a second request needs read/parse work
- **THEN** the second request is in the named job-capacity wait
- **AND** it is not reported as queue starvation and is not silently dropped
- **AND** it is served once the commit-pending job clears the phase

#### Scenario: An unprocessable request reaches a terminal refusal

- **WHEN** a queued restore cannot proceed for a non-retryable reason other than a live owner
- **THEN** the request reaches a terminal refusal in bounded time
- **AND** the refusal names the reason

### Requirement: Restore queue progress is independent of queue-head blockage

The restore queue SHALL make progress on every startable request, regardless of the state of higher-priority requests. A request that cannot begin SHALL NOT prevent a lower-priority startable request from beginning. Queue ordering SHALL remain a priority order for selection, not a gate on service.

#### Scenario: A blocked head does not stall the queue

- **WHEN** the highest-priority queued request cannot begin because its slot is owned by a live owner
- **AND** a lower-priority queued request's slot is free
- **THEN** the lower-priority request is started
- **AND** the blocked request remains queued with its reason observable

#### Scenario: A draining queue stays order-preserving for startable work

- **WHEN** every queued request is startable
- **THEN** they are begun in priority order

### Requirement: The blocking persistence drain preserves and reports the restore it waits on

**Delivered scope of this requirement.** The blocking deferred-save drain SHALL service the restore it waits
on where the applicable gate admits, using the existing selection and admission, within a declared
per-iteration budget, and SHALL NOT introduce a second loader, a second queue, or a parallel admission
policy. The drain SHALL NOT cancel, supersede, or discard the restore in order to complete; where the gate
denies, it SHALL preserve the request and report the **named** wait. **Not delivered by this requirement:**
that the drain *advances* a restore that holds the SD session rather than only reporting it. That behavior is
**deferred** to `runtime-working-set-ownership` S2b.7 and is bound by
`resumable-midi-boundary-playback-maintenance` **task 3.3** (bounded commit/cleanup); no advancement is
claimed here.

The drain SHALL derive its admission from the **same actual runtime eligibility** the normal load path
uses, evaluated across all tracks — it SHALL NOT assume that a CLEAR leaves the runtime non-timing-critical,
because CLEAR stops only the selected track. Where that eligibility denies commit or cleanup, the drain
SHALL preserve the request and report the named wait; it SHALL NOT bypass the gate to make CLEAR succeed.

Restore work SHALL be accounted for in the drain's budget and progress detection: the drain's step budget
SHALL include active and parked restore work, and restore progress SHALL count as progress so that a drain
advancing a large restore is not misreported as stuck or as exceeding its budget. While draining, the drain
SHALL admit at most the bounded fresh queue work it needs to clear the save gate. The existing post-load
commit holdoff SHALL be honored and reported by name.

While the drain owns the thread, the normal deferred-load service path is unavailable; the requirement is
therefore that the drain itself makes the restore progress. If the restore reaches a state the drain must
not break — capture active on that slot, or a denying commit gate — the drain SHALL report that named
reason instead of spinning its stuck budget and aborting.

#### Scenario: A blocking drain completes without cancelling the restore

- **WHEN** a deferred save is owed, a restore for `(track, slot, requestGeneration, setEpoch)` has started
  and is unresolved at the CLEAR edge, and another track is audibly playing
- **THEN** the drain completes the owed save by the workspace-save priority path
- **AND** the restore request remains tracked with its identity and named wait, and was not cancelled,
  superseded, or discarded to achieve that
- **AND** no claim is made that the drain advanced the restore

#### Scenario: A parked job is serviced, not only an active one

- **WHEN** the restore that holds the save gate is a **parked** job instance rather than the active one
- **THEN** the drain services the parked instance and the wait clears

#### Scenario: Another track playing preserves the commit wait

- **WHEN** CLEAR is pressed on one track while another track is still audibly playing or recording
- **AND** a restore has commit or cleanup pending
- **THEN** the drain does not admit the unbounded commit
- **AND** the request stays tracked and the drain reports the named commit-gate wait
- **AND** the selected track's slot is still reset only through the existing clear path

#### Scenario: Restore progress is not misreported as stuck or over budget

- **WHEN** a small deferred save is owed behind a restore whose advancement takes multiple drain slices
- **THEN** the drain's budget accounts for the restore work
- **AND** restore progress counts as drain progress
- **AND** the drain completes rather than reporting a stuck state or exceeding its step budget

#### Scenario: The drain does not become an unbounded background fill

- **WHEN** the drain services a restore while other payloads are queued
- **THEN** it admits at most the bounded fresh queue work it needs to clear the save gate
- **AND** it does not drain the whole restore queue as a side effect

#### Scenario: A post-load holdoff is reported by name

- **WHEN** the drain cannot advance because the post-load commit holdoff is in effect
- **THEN** it reports the holdoff as its named reason

#### Scenario: A wait the drain must not break is named

- **WHEN** the restore cannot progress because a track on that slot is recording
- **THEN** the drain reports the request's named wait reason
- **AND** it does not report a generic stuck state and does not spin its full stuck budget

#### Scenario: The drain does not cancel the restore

- **WHEN** the drain completes with a restore outstanding
- **THEN** the restore request is still tracked with its identity and state
- **AND** it was not discarded to unblock the save

### Requirement: A blocking drain reports completed, temporarily blocked, or failed, and its callers honor that

The blocking drain SHALL distinguish three outcomes: **completed** (the owed persistence work finished),
**temporarily blocked** (a named wait the drain could not clear within its budget), and **failed** (a real
failure). A caller SHALL NOT treat a temporarily blocked outcome as success, and SHALL NOT treat it as a
slot-reset or destructive-operation license while persistence is still pending.

A caller that performs a destructive operation only after a drain (CLEAR is the named case) SHALL, on a
temporarily blocked outcome, refuse the operation, report the named reason, and leave the slot untouched;
the owed save SHALL remain owed and be retried by the normal scheduler. On a failed outcome the caller
SHALL report the failure with its reason. The caller SHALL NOT report success while persistence remains
pending.

The finite post-load commit holdoff SHALL be **reported by name, never waited out inside the drain**. The
drain runs on the interval path, and every MIDI input-service call lives in `main`; a drain that blocked
there and serviced MIDI would move an input drain into the interval. When the holdoff is the sole blocker the
drain SHALL therefore classify it and return the named `post_load_holdoff` immediately, changing nothing: the
restore and the owed save are preserved, and the holdoff expires on its own deadline while `main` keeps
servicing MIDI. The caller refuses by name and a later attempt completes the save. The holdoff duration SHALL
be a single named declaration.

#### Scenario: A temporarily blocked drain is not reported as success

- **WHEN** the drain returns a temporarily blocked outcome with a named reason
- **THEN** the caller reports that named reason
- **AND** it does not report success and does not proceed with the destructive operation

#### Scenario: A blocked CLEAR leaves the slot untouched

- **WHEN** CLEAR's drain is temporarily blocked
- **THEN** the selected slot is not reset
- **AND** the owed save remains owed and is retried later
- **AND** the refusal is reported by name, distinguishable from a real failure

#### Scenario: The finite post-load holdoff is reported, not waited out inside the drain

- **WHEN** the only remaining blocker is the post-load commit holdoff
- **THEN** the drain returns the named `post_load_holdoff` immediately, without waiting and without
  servicing MIDI inside the drain
- **AND** the restore and the owed save are preserved and the queued MIDI is left for `main` to service

#### Scenario: The caller refuses by name on the holdoff and retries later

- **WHEN** the drain returns the named holdoff wait
- **THEN** the caller refuses the destructive operation by name rather than blocking indefinitely
- **AND** the holdoff expires on its own deadline while `main` keeps servicing MIDI, so a later attempt
  completes the save

#### Scenario: A failed drain is distinguishable from a blocked one

- **WHEN** the drain returns a failed outcome
- **THEN** the caller reports the failure and its reason
- **AND** the outcome is distinguishable in the reported state from a temporarily blocked wait

### Requirement: MIDI service cannot re-enter or invalidate a blocking drain

StorageManager SHALL own a single scoped drain-in-progress guard. A nested drain request SHALL return
`Blocked(drain_in_progress)` before changing shared drain/save state, and SHALL NOT overwrite the outer
operation's result, budget, or persistence obligation. The guard SHALL release on every exit.

MIDI-dispatched destructive/storage actions conflicting with the outer target or drain-owned storage
SHALL be refused by name before any side effects. Normal musical input and transport SHALL remain
serviced. After each callback boundary the drain SHALL re-evaluate capture/timing eligibility, restore
identity and commit admission before advancing work. The outer destructive caller SHALL retain and
revalidate its original target identity; focus changes SHALL NOT redirect it to another slot.

#### Scenario: A second CLEAR cannot recursively drain or reset

- **WHEN** a controller message requests another CLEAR while MIDI is serviced inside a drain
- **THEN** the nested action is refused with `drain_in_progress` before stopping or resetting a track
- **AND** no nested drain mutates shared state, and the outer save obligation and result remain intact

#### Scenario: Conflicting replacement is refused before mutation

- **WHEN** an undo or set/revision callback would replace the outer target or mutate drain-owned storage
- **THEN** it is refused with `drain_in_progress` before that mutation
- **AND** normal musical MIDI remains serviced

#### Scenario: Transport or capture changes invalidate cached eligibility

- **WHEN** MIDI service changes transport or capture state during the holdoff
- **THEN** the next restore/save step uses the new all-track eligibility and request identity
- **AND** a denying gate yields a named blocked result with obligations preserved

#### Scenario: Focus changes do not redirect the outer CLEAR

- **WHEN** focus changes while the original CLEAR is draining
- **THEN** any subsequent clear targets only its original, revalidated slot identity
- **AND** a changed target identity causes a named refusal without resetting replacement content

#### Scenario: Every drain exit releases its guard

- **WHEN** a drain returns completed, blocked, or failed
- **THEN** its scoped guard is released
- **AND** a later independent drain can enter normally

### Requirement: Restore and boot-catalog state is observable

The system SHALL expose, without a bisect, enough state to distinguish a restore that is waiting, one that is progressing, and one that has terminated, and SHALL report the outcome of the boot catalog reconcile. This observability SHALL be available in the session-capture tier and SHALL NOT become a per-turn hot-path cost.

#### Scenario: A waiting restore is distinguishable from a progressing one

- **WHEN** a restore request is queued or in flight
- **THEN** the observable state names the request's `(track, slot)`, whether it is waiting or terminal, its named reason when waiting, and its progress when progressing

#### Scenario: The boot catalog reconcile outcome is reported

- **WHEN** boot reconciles the set index and the reconcile fails or repairs the index
- **THEN** the outcome is reported as a named result
- **AND** a repair or a continued failure is distinguishable from a silent success

#### Scenario: Observability does not cost the hot path

- **WHEN** no restore is active
- **THEN** the observability costs no per-turn work on the MIDI service path
