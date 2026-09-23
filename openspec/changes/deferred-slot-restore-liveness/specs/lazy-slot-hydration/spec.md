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

Focus High `LoadLoopJob` work (focused / explicitly requested slot) SHALL advance while a track is PLAYING. Background Low `LoadLoopJob` work SHALL be admitted a bounded service slice while any track is PLAYING, audibly sounding, or otherwise transport-active, so that a queued restore cannot be starved for the lifetime of a transport session. Background work that is not admissable in the current turn SHALL leave active/parked/queue progress intact; it SHALL NOT be treated as complete, attempted, or refused.

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
- **AND** the slot SHALL eventually reach COMMITTED, or reach a named terminal state reported for it
- **AND** playback SHALL continue without requiring the transport to stop

## ADDED Requirements

### Requirement: Every queued slot restore reaches exactly one terminal state

The system SHALL model each queued slot restore as a request with exactly one terminal outcome, chosen from: **committed** (the payload was restored), **unnecessary** (the slot already has committed content, or the SD payload is gone or invalid), or **refused** (a named reason why the restore cannot proceed, reported for that request). A request SHALL NOT remain queued across unrelated transport activity, session changes, or clears, and SHALL NOT leave the queue by being silently marked attempted.

The named refusal reason SHALL be observable and SHALL distinguish at least: no SD payload, unverifiable payload, slot owned by a live owner, and payload present but not yet processed.

#### Scenario: A queued request leaves the queue exactly once

- **WHEN** a slot restore request is queued and the system processes it to completion
- **THEN** exactly one terminal outcome is recorded for that `(track, slot)`
- **AND** the request is not re-enqueued afterwards

#### Scenario: A blocked request is reported, not silently attempted

- **WHEN** a queued restore cannot begin because its slot is owned by a live owner
- **THEN** the request stays queued rather than being marked attempted
- **AND** the reason is observable while it waits

#### Scenario: An unprocessable request reaches a terminal refusal

- **WHEN** a queued restore cannot proceed for a reason other than a live owner
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

### Requirement: A pending slot restore does not block persistence indefinitely

A pending or in-flight slot restore SHALL NOT cause the deferred-save drain to wait without bound. While a restore is in flight, the system SHALL either continue the restore to a terminal state within a bounded service budget, or report the drain as blocked by the restore with that named reason. The blocking drain SHALL NOT spin its full stuck budget and abort while the restore it waits on is itself making no progress.

#### Scenario: A blocking drain completes once the restore progresses

- **WHEN** a deferred save is owed and a slot restore is in flight
- **AND** the restore is able to progress
- **THEN** the drain completes once the restore reaches a terminal state
- **AND** the restore was not cancelled to achieve that

#### Scenario: A stalled restore is reported as the cause

- **WHEN** a deferred save is owed and the restore it waits on cannot make progress
- **THEN** the drain reports the restore as the blocking cause, distinct from a capture block
- **AND** it does not report a generic stuck state

### Requirement: Restore and boot-catalog state is observable

The system SHALL expose, without a bisect, enough state to distinguish a restore that is waiting, one that is progressing, and one that has terminated, and SHALL report the outcome of the boot catalog reconcile. This observability SHALL be available in the session-capture tier and SHALL NOT become a per-turn hot-path cost.

#### Scenario: A waiting restore is distinguishable from a progressing one

- **WHEN** a restore request is queued or in flight
- **THEN** the observable state names the request's `(track, slot)`, its wait reason when waiting, and its progress when progressing

#### Scenario: The boot catalog reconcile outcome is reported

- **WHEN** boot reconciles the set index and the reconcile fails or repairs the index
- **THEN** the outcome is reported as a named result
- **AND** a repair or a continued failure is distinguishable from a silent success

#### Scenario: Observability does not cost the hot path

- **WHEN** no restore is active
- **THEN** the observability costs no per-turn work on the MIDI service path
