## MODIFIED Requirements

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

Focus High `LoadLoopJob` work (focused / explicitly requested slot) SHALL advance its **preparation** while a track is PLAYING. Background Low `LoadLoopJob` work SHALL be admitted a bounded service slice while any track is PLAYING, audibly sounding, or otherwise transport-active, so that a queued restore cannot be starved for the lifetime of a transport session. Background work that is not admissable in the current turn SHALL leave active/parked/queue progress intact; it SHALL NOT be treated as complete, attempted, or refused. **Commit and cleanup remain bounded only by the runtime's existing commit admission:** a prepare-complete request under active transport waits in a named state for that gate, and this requirement does not claim that it applies while a track is timing-critical.

#### Scenario: Select unloaded during PLAYING hydrates via deferred job steps

- **WHEN** the user selects an unloaded slot while a track is PLAYING (no record/overdub)
- **THEN** the load request is retained and remaining payloads are enqueued for priority fill
- **AND** focus High `LoadLoopJob` for that slot MAY advance one chunk (or parse/commit) per `DeferredJobScheduler::runFrame` while PLAYING
- **AND** display MAY update from COMMITTED passes without requiring transport stop

#### Scenario: Select unloaded during RECORDING defers

- **WHEN** the user selects an unloaded slot while any track is RECORDING or OVERDUBBING
- **THEN** the load request is retained
- **AND** SD restore for that slot does not run until capture is idle

#### Scenario: Background restore is not starved by playback

- **WHEN** a slot with committed SD content is queued for background restore
- **AND** the transport is running with one or more tracks audibly playing
- **THEN** the restore request SHALL receive bounded service slices rather than being skipped indefinitely
- **AND** its preparation SHALL advance or it SHALL be in a named wait state reported for it
- **AND** playback SHALL continue without requiring the transport to stop

#### Scenario: Applying a prepared restore under active transport is checked at the commit boundary

- **WHEN** a restore's preparation is complete and the transport is timing-critical
- **AND** the runtime has no content-independent bound for the commit operation
- **THEN** the request SHALL wait in a named state for the commit gate rather than being refused, retried unboundedly, or reported as complete
- **AND** the request SHALL be applied at the first turn the gate admits it

## ADDED Requirements

### Requirement: Restore requests have identity, named waits, and one terminal outcome

The system SHALL model each queued slot restore as a request identified by
`(track, slot, requestGeneration, setEpoch)`, where `requestGeneration` advances whenever that
coordinates pair is cleared, reset, or committed by a different owner, and `setEpoch` identifies the
loaded current set. A later session reusing the same `(track, slot)` SHALL be a **different** request.

A request SHALL be in exactly one **wait** state or exactly one **terminal** state at a time.

Wait states SHALL carry a named reason and SHALL NOT be terminals: a slot owned by a live owner, capture
active on that slot, the commit gate not admitting (no content-independent bound), and a bounded retry
after a transient failure. A wait SHALL NOT convert to a terminal merely because it persisted for a
number of turns or iterations.

Terminal outcomes SHALL be exactly: **committed** (the payload was restored), **unnecessary** (the slot
already has committed content, or the SD payload is absent or invalid), **refused** (a named,
non-retryable reason), and **superseded** (the set changed, the slot was cleared, or a newer request for
the same coordinates replaced it). Recording deferral SHALL remain a wait, never a terminal refusal.

A request SHALL NOT remain queued unexplained across unrelated transport activity, SHALL NOT leave the
queue by being silently marked attempted, and SHALL NOT record a terminal under coordinates a later
session reuses.

#### Scenario: A queued request leaves the queue exactly once

- **WHEN** a slot restore request is queued and the system processes it to completion
- **THEN** exactly one terminal outcome is recorded for that request's identity
- **AND** the request is not re-enqueued afterwards

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

### Requirement: The blocking persistence drain advances the restore it waits on

The blocking deferred-save drain SHALL advance a restore it is waiting on, rather than only observing it. The drain SHALL service both the active and the parked restore job instances through the existing
selection and admission, within a declared per-iteration budget, and SHALL NOT introduce a second loader,
a second queue, or a parallel admission policy. The drain SHALL NOT cancel, supersede, or discard the
restore in order to complete.

While the drain owns the thread, the normal deferred-load service path is unavailable; the requirement is
therefore that the drain itself makes the restore progress. If the restore reaches a state the drain must
not break — for example capture active on that slot — the drain SHALL report that named reason instead of
spinning its stuck budget and aborting.

#### Scenario: A blocking drain completes by advancing the restore

- **WHEN** a deferred save is owed and a slot restore is in flight, with no external advancement of the
  restore
- **THEN** the drain advances the restore to a terminal state within its budget
- **AND** the drain then completes
- **AND** the restore was not cancelled to achieve that

#### Scenario: A parked job is serviced, not only an active one

- **WHEN** the restore that holds the save gate is a **parked** job instance rather than the active one
- **THEN** the drain services the parked instance and the wait clears

#### Scenario: A wait the drain must not break is named

- **WHEN** the restore cannot progress because a track on that slot is recording
- **THEN** the drain reports the request's named wait reason
- **AND** it does not report a generic stuck state and does not spin its full stuck budget

#### Scenario: The drain does not cancel the restore

- **WHEN** the drain completes with a restore outstanding
- **THEN** the restore request is still tracked with its identity and state
- **AND** it was not discarded to unblock the save

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
