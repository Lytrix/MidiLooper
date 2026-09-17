## Purpose

Admits, routes and preserves a successor capture interval while a predecessor's
deferred finalization is still pending, so punching out and immediately recording
again never waits for finalization and never corrupts either interval.

## ADDED Requirements

### Requirement: A successor interval is admitted while a predecessor finalizes

While one deferred overdub stop is pending on a track, the system SHALL admit a new
capture interval on that track without waiting for that job to finish, provided the
declared admission capacity is available. The successor SHALL begin at the tick its
input arrives and SHALL NOT be substituted with a later start.

#### Scenario: Punch out A and immediately record B

- **WHEN** the user stops overdub interval A and starts interval B before A's
  deferred finalization has received a worker turn
- **THEN** B is admitted and its NoteOn/NoteOff events are captured at their
  original input ticks
- **AND** A's finalization continues independently and still reaches its own
  destination

#### Scenario: Public control surfaces agree on admission

- **WHEN** the same reentry is driven through Button A, through the TrackManager
  selection path, and directly on the Track
- **THEN** all three routes admit B on the same terms
- **AND** no route depends on an outer guard having refused the press first

### Requirement: Successor admission capacity is declared and explicit

The system SHALL declare its successor admission capacity per track and split it by
direction: **starting** a successor requires the live admission role to be free, and
**stopping** an interval requires the deferred/frozen admission role to be free. The
supported capacity is one finalizing predecessor plus one independently recording
successor; two simultaneously deferred capture intervals are not supported. A
pending predecessor SHALL NOT block a successor start, and SHALL block a successor
stop while it owns the sole deferred role. A denied operation SHALL leave every
previously accepted interval intact, and no press SHALL be queued silently, dropped
after acceptance, or reported as ordinary latency.

#### Scenario: A successor starts while the predecessor finalizes

- **WHEN** the predecessor's deferred stop is pending and the user starts a successor
  interval on the same track
- **THEN** the successor is admitted and captures on the live role

#### Scenario: Successor stop is denied while the predecessor pends

- **WHEN** the live successor is stopped while the deferred predecessor still owns
  the frozen admission role
- **THEN** the stop is refused with an explicit exhaustion outcome and no stop
  descriptor is created for the successor
- **AND** the successor remains the live recording interval and keeps recording
- **AND** its event count, pending notes, reservation/NoteId identity, capture
  extent, preview state and interval identity are unchanged across the denial

#### Scenario: The same interval transfers once capacity frees

- **WHEN** the predecessor's finalization retires and releases its frozen role, and
  the still-live successor is stopped again
- **THEN** that stop is admitted, the same interval transfers into the deferred role
  exactly once, and the live role becomes available
- **AND** the finalized successor output equals the accepted successor input exactly,
  with no event duplicated or dropped by the earlier denial

#### Scenario: A further interval starts only after the successor transfers

- **WHEN** a start is requested while the successor is still the live interval
- **THEN** it is not admitted as a second live interval
- **AND** once the successor has transferred to the deferred role, a subsequent start
  is admitted

### Requirement: Interval ownership survives concurrent finalization

Each interval SHALL own its capture input, pending note changes, preview, undo
baseline and stop receipts for its whole life. A predecessor's finalization,
completion or cleanup SHALL NOT clear, retarget or consume a successor's live state.

#### Scenario: Old finalization completes while a successor is recording

- **WHEN** interval A's deferred finalization completes while interval B is
  actively capturing
- **THEN** B's capture rows, pending note changes, preview and reservations are
  unchanged
- **AND** B's track state remains its own (a completion of A SHALL NOT force the
  track into playback or stopped on B's behalf)
- **AND** A's cleanup mutates live capture state only if A's interval still
  identifies the live interval; otherwise it releases its own resources, commits its
  own result and clears its own receipts only

#### Scenario: A retiring job's cleanup is job-local

- **WHEN** the retiring job's interval identity no longer matches the live interval
- **THEN** its cleanup performs no live state transition, pending-note, preview,
  capture-extent or reservation mutation
- **AND** the same rule holds on every terminal path (playback commit, STOPPED
  recovery, superseded route, fault fallback)

#### Scenario: Held same-pitch note crosses the boundary

- **WHEN** B starts while A's held note of the same pitch is still being closed by
  A's finalization
- **THEN** B's note identity and closure belong to B
- **AND** A's original audible NoteOff is retained exactly once and cannot terminate
  B's new note

### Requirement: A staged capture pass is owned by the interval that staged it

The producer's staged (pending) capture pass SHALL be detached from loop-shared
producer state and owned by exactly one interval. Opening, stopping or wrapping another
interval's capture on the same loop SHALL NOT discard, consume or mutate the pass of an
interval that does not own it, and SHALL NOT find another interval's pass occupying its
own staging slot. At most one owner may hold a given staged pass, while multiple
intervals may each hold their own staged pass concurrently.

A terminal transfer SHALL consume the pass from its owning interval/job and SHALL NOT
require that owner to reacquire loop-global producer state, so a deferred interval can
always finish with the pass it already owns.

All capacity the destination owner requires SHALL be admitted before ownership
transfers. Once the transfer begins it SHALL be non-fallible and SHALL leave no shared
alias of the pass in the producer, so no failure can leave the pass owned by neither
the producer nor the destination. The loop's pending-pass slot SHALL be producer-local:
a deferred interval SHALL NOT depend on its contents after its stop admission.

#### Scenario: A successor opens its interval while the predecessor is finalizing

- **WHEN** the predecessor's deferred job has staged its capture pass and the successor
  starts capturing on the same loop
- **THEN** the predecessor's staged pass still exists and the predecessor's companion
  preparation can still be admitted
- **AND** the predecessor's job reaches its own terminal transfer and retires
- **AND** neither interval's accepted rows, pending notes, reservations or interval
  identity are changed by the other's capture start

#### Scenario: Both intervals hold a staged pass at the same time

- **WHEN** the predecessor's staged pass has moved into its deferred job, the successor
  then starts and stages its own pass (for example through a wrap), and the predecessor
  reaches its terminal transfer
- **THEN** the predecessor's transfer consumes only its own pass, without reacquiring the
  loop's pending-pass slot, and performs no allocation
- **AND** the successor's staged pass is unchanged in identity, chunks and session
  identity across that transfer
- **AND** the successor later consumes its own pass exactly once

#### Scenario: A successor moves its held note across loop end while the predecessor finalizes

- **WHEN** the live successor commits a wrap pass while the predecessor's job is still
  pending
- **THEN** the predecessor's staged pass is not consumed or discarded by that wrap
- **AND** the successor can still stage its own pass at its own terminal transfer

### Requirement: Superseded or diverged publication never selects a stale window

When an interval's recorded causal predecessor is no longer the published root, the
interval SHALL retire without publishing an audible successor window rather than
selecting a window for a source it did not succeed. Transport Stop at any
finalization phase SHALL likewise preserve accepted data and SHALL NOT publish a
stale playback window.

#### Scenario: Capacity denial is distinguishable from latency

- **WHEN** a stop is denied because the deferred role is occupied
- **THEN** the denial is observable as its own outcome rather than as an absent or
  delayed result
- **AND** the interval continues recording in the meantime

#### Scenario: A publication supersedes the recorded predecessor

- **WHEN** the published root advances past an interval's recorded causal
  predecessor before that interval publishes
- **THEN** its accepted capture is committed exactly once
- **AND** no bridge is selected or published for the stale base
- **AND** the newer published root stays selected for playback
- **AND** the retiring job cannot alter the live successor's state
- **AND** the unpublished companion horizon returns to its prewarmed state

#### Scenario: Transport Stop during finalization with a live successor

- **WHEN** transport Stop arrives while a predecessor is finalizing and a successor
  interval is live
- **THEN** the predecessor retargets and completes without publishing a stale
  playback window
- **AND** the successor keeps its accepted input and its own state

#### Scenario: Transport Stop retargets a job that already entered the PLAYING-only preparation

- **WHEN** transport Stop arrives after the pending job has passed the transfer
  decision point and entered the PLAYING-only preparation (canonical tail,
  companion history, undo collection or companion bridge)
- **THEN** the job leaves that branch on the same edge, retires its unused
  companion and undo estimates, commits its already-staged prepared pass on the
  STOPPED route and reaches deferred cleanup
- **AND** it releases the stop owner, the frozen staging role and its session-exit
  receipt, so a later stop of the live interval is admitted again
- **AND** it never parks in a phase whose inputs the stopped transport invalidated

### Requirement: Admission decisions are observable without a logging flood

The system SHALL record compact transition and aggregate evidence for reentry —
command action tick, interval identity, admission or denial outcome, queue
depth/high-water, and reuse versus restart counts — and SHALL NOT add per-event
logging to the capture path.

#### Scenario: Reentry is audited from a capture

- **WHEN** a device capture contains a punch-out followed by an immediate reentry
- **THEN** the capture shows the successor's admission (or its explicit denial) and
  the predecessor's continuation stage, with queue depth and interval identity
- **AND** no per-event line was added to the active capture path
