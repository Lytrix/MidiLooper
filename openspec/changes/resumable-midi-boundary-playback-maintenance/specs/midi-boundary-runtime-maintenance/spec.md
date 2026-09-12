## Purpose

Keeps post-MIDI-boundary playback and deferred-frame maintenance small,
resumable, and observable while active transport requires timely MIDI service.

## ADDED Requirements

### Requirement: Active-transport maintenance is bounded and resumable

The system SHALL execute playback-order maintenance and deferred
load/display-frame maintenance during RECORDING, PLAYING, OVERDUBBING, or
`OVERDUB_STOPPING` only as individually bounded grains. A grain SHALL have a
declared input quantum and a measured or structurally enforced cost bound
proportional to that quantum. If a grain is not eligible at a MIDI service
boundary, or cannot finish within its permitted grain, the remaining work SHALL
stay pending and resume from owner-local continuation state in a later eligible
turn. `OVERDUB_STOPPING` SHALL retain playback continuity and all active
transport persistence/load-frame exclusions.

#### Scenario: MIDI service defers an ineligible grain

- **WHEN** active transport reaches a MIDI service boundary and a pending
  maintenance grain is not eligible
- **THEN** MIDI input is serviced without executing that grain
- **AND** the maintenance continuation remains pending without loss of work

#### Scenario: A grain yields before its semantic operation is complete

- **WHEN** a playback-order or deferred-frame operation exceeds one permitted
  grain during active transport
- **THEN** the system records its continuation before returning to MIDI service
- **AND** a later eligible turn resumes the same operation without restarting
  previously completed work

#### Scenario: Capture-source preparation precedes active seal

- **WHEN** an active overdub event is accepted into `capture.store`
- **THEN** fixed `CaptureAdmissionStaging` capacity has been reserved for it
- **AND** its capture-admission contract proves the bounded pre-seal candidate
  can cover its earliest audible instance before the applicable wrap or stop
  boundary
- **AND** an event that cannot satisfy that contract is rejected before it
  enters `capture.store`

#### Scenario: Accepted NoteOn reserves its terminal closure

- **WHEN** an active-overdub NoteOn is accepted into `capture.store`
- **THEN** its `CaptureNoteReservation` reserves fixed source, bridge, and
  stop-finalization capacity for that NoteOn, matching NoteOff, and any required
  wrap or synthetic terminal closure
- **AND** a matching later NoteOff consumes that reservation without ordinary
  capacity or readiness rejection
- **AND** if the complete note lifetime cannot be reserved, the NoteOn is
  rejected before it mutates capture or pending-note state

#### Scenario: NoteOff follows a rejected NoteOn

- **WHEN** a NoteOn was rejected by capture admission and its later NoteOff
  arrives
- **THEN** the NoteOff is forwarded through normal MIDI handling
- **AND** it does not create, close, or otherwise mutate captured-note state

#### Scenario: Load Commit is not structurally bounded

- **WHEN** a load job's Commit publication, bookkeeping, or required cleanup
  cannot demonstrate a complete content-independent active-transport bound
- **THEN** `DeferredJobScheduler::runFrame` does not admit that job during
  RECORDING, PLAYING, OVERDUBBING, or `OVERDUB_STOPPING`
- **AND** the job remains pending until STOPPED or a separately bounded
  preparation, Commit, or cleanup continuation is available

### Requirement: Deferred maintenance preserves playback and publication semantics

Deferring maintenance SHALL NOT drop, duplicate, or reorder audible MIDI
events. An active playback cursor SHALL read an event stream, playback order,
source identity, cycle-qualified emission cursor, ledger-boundary summary, and
explicit phase coverage from one immutable playback view. A successor build
SHALL NOT mutate that active view. The system SHALL retain the last validly
published view until a successor reaches its atomic promotion boundary; it
SHALL NOT expose partially built playback ordering, loop content, slot-load
state, or display-frame data. A long-loop cursor SHALL not run beyond published
coverage unless a complete contiguous successor coverage interval is selected
at the handoff.

#### Scenario: Playback order is pending at an audible event boundary

- **WHEN** a new playback order is incomplete when an audible event boundary
  is reached
- **THEN** the system uses one valid immutable playback view or an immutable
  source-change bridge paired with that view
- **AND** it does not emit a duplicate, missing, or out-of-order MIDI event

#### Scenario: Successor promotion crosses an event boundary

- **WHEN** a complete successor playback view becomes eligible between two MIDI
  service boundaries
- **THEN** its promotion preserves every event already completed by the active
  view and every event newly due after promotion
- **AND** the playback cursor cannot index an order from a different event
  stream or source identity

#### Scenario: Successor promotion occurs before loop wrap

- **WHEN** a successor promotes after an event instance has emitted in the
  current projection cycle
- **THEN** its cycle-qualified emission cursor suppresses only that already
  emitted instance after promotion
- **AND** the same source event is eligible again in the next projection cycle
- **AND** the candidate ledger-boundary summary matches the live
  `ActiveNoteLedger` without a clock-path state scan

#### Scenario: A pending source changes an already-open note

- **WHEN** a pending source would suppress or replace an event paired with a
  NoteOn already emitted into the live ledger
- **THEN** the bridge retains the base NoteOff needed to close that ledger entry
- **AND** the changed note pair becomes effective only after that safe boundary
  or in the next projection cycle

#### Scenario: A source change supersedes a pending successor

- **WHEN** a newer source identity arrives before a pending successor completes
- **THEN** the system discards or coalesces only the unpublished successor work
- **AND** it leaves the active playback view valid until the newest successor
  can be promoted

#### Scenario: A newly due source-change event precedes full successor completion

- **WHEN** a source change affects an event that becomes due before its full
  successor view completes during active transport
- **THEN** a fully prepared immutable delta bridge, with explicit insert,
  suppress, or replace records, is selected before that event is due
- **AND** the active clock path consumes the published view and bridge by a
  bounded ordered merge with the cycle-qualified exactly-once cursor
- **AND** if that bridge cannot be ready in time, its event is denied at capture
  ingestion and never reaches canonical commit, rather than delaying, omitting,
  or exposing incomplete audible content

#### Scenario: A second source change arrives while a bridge is selected

- **WHEN** a second source identity arrives before the selected bridge retires
- **THEN** the selected bridge remains immutable and continues to serve due
  events
- **AND** the system coalesces unpublished bridge and successor work to the
  newest identity
- **AND** it atomically replaces the selected bridge only after the replacement
  covers its earliest affected un-emitted event

#### Scenario: Active canonical commit requires audible readiness

- **WHEN** a capture pass would commit during active transport and its first
  affected event instance can become due in the current projection cycle
- **THEN** `PlaybackSourceAdmission` has already prepared the complete bridge
  or successor coverage for that instance before canonical commit
- **AND** `Loop` commits canonical content and its audible admission atomically
- **AND** if that precondition fails, the event was rejected before capture and
  telemetry identifies the resource-admission failure

#### Scenario: Pre-seal candidate is incomplete at an overdub wrap

- **WHEN** `maybeCommitOverdubWrap` reaches a wrap crossing and the candidate
  generation does not match the accepted capture generation or is incomplete
- **THEN** it does not call a mutating seal/detach or restart the capture session
- **AND** it records a coverage-contract fault and takes the deterministic
  all-notes-off/stop path before an accepted event can be silently omitted
- **AND** it preserves accepted capture state for recovery after STOPPED

#### Scenario: A long-loop window approaches its handoff phase

- **WHEN** an unchanged long-loop source approaches the end of its published
  playback-view coverage interval
- **THEN** its next contiguous coverage interval has been built by resumable
  grains and is selected at the handoff phase, including across phase wrap
- **AND** a verified per-owner coverage-service contract proves enough phase
  lead and admitted grains to prepare the interval after that successor
- **AND** if that contract cannot be established, windowed maintenance is
  excluded during active transport in favor of a prewarmed full-loop view or
  STOPPED-only operation
- **AND** any runtime contract violation takes the deterministic fail-safe
  all-notes-off/stop path rather than silently omitting a due event

#### Scenario: Deferred frame is incomplete

- **WHEN** a load/display-frame operation yields before it can complete
- **THEN** the existing valid loop and display state remain visible
- **AND** the incomplete frame is not published as a completed result

#### Scenario: Stop follows an append without an intervening staging grain

- **WHEN** an active-overdub stop command arrives immediately after an accepted
  capture append
- **THEN** the system latches `stopTick`, enters `OVERDUB_STOPPING`, and freezes
  new capture input without faulting the transport
- **AND** it applies one reserved stop-finalization grain after each existing
  MIDI service boundary
- **AND** it commits only after the frozen candidate and audible admission are
  ready and transitions to the command-epoch-validated destination, PLAYING by
  default or STOPPED after transport Stop

#### Scenario: Stop finalization exceeds its declared contract

- **WHEN** a frozen stop generation cannot complete within its declared
  `StopAdmissionContract`
- **THEN** the system reports an invariant breach and takes the deterministic
  all-notes-off/stop path
- **AND** it preserves the capture generation for STOPPED recovery

#### Scenario: Transport Stop arrives during deferred stop admission

- **WHEN** a transport Stop command arrives between `OVERDUB_STOPPING`
  finalization grains
- **THEN** transport Stop increments the pending-stop command epoch, selects
  STOPPED, and invalidates the pending PLAYING transition before another grain
  or atomic transfer is selected
- **AND** normal transport Stop emits all-notes-off and prevents playback from
  resuming without a later explicit transport Start
- **AND** the frozen candidate may complete canonical recovery only under
  STOPPED policy

### Requirement: Canonical commit is distinct from audible-source admission

`Loop` SHALL retain ownership of canonical capture-pass commit, canonical
revision, persistence, and undo/redo. `LoopPlaybackRuntime` SHALL own
generation-matched `CaptureAdmissionStaging`, the pre-commit
`PlaybackSourceAdmission`, effective audible source revision, and one pending
later-effective-cycle admission that references immutable canonical content.
Each accepted NoteOn SHALL own a `CaptureNoteReservation` for its matching
closure and stop/wrap finalization work. Before active canonical commit,
staging SHALL have completed the semantic equivalents of seal sorting, identity
assignment, wrap finalization, and filtering without mutating `capture.store`.
An active canonical commit SHALL transfer that frozen candidate and ready
admission atomically; it SHALL NOT run those semantic operations, detach mutable
capture chunks, or build an audible bridge. A pending admission is allowed only
when all affected current-cycle instances are already covered; it SHALL NOT
delay, omit, or roll back an accepted canonical commit. A stop request MAY
enter bounded `OVERDUB_STOPPING` until the frozen candidate is ready, and SHALL
not take the fault path during normal declared-contract operation.

#### Scenario: Later-effective pending admission is superseded

- **WHEN** a newer canonical revision arrives before a pending later-effective
  audible-source admission promotes
- **THEN** the runtime coalesces only the pending admission to the newest
  immutable canonical source
- **AND** persistence and undo/redo retain both their existing canonical
  semantics and ownership
- **AND** the effective audible source remains immutable until a valid promotion

#### Scenario: Source change preempts rolling long-loop build work

- **WHEN** a source change is admitted while long-loop playback has current,
  ready, and successor-after-next build coverage
- **THEN** the selected bridge covers all affected instances through the end of
  the ready coverage before the build buffer is reused
- **AND** the reused build buffer constructs the new-source interval after the
  ready coverage
- **AND** at the current-to-ready handoff the new-source interval MAY remain
  pending while the retired current buffer remains idle
- **AND** once the new-source interval is complete, it becomes ready and the
  retired current buffer begins the interval after it
- **AND** the source-change coverage contract proves the new-source interval is
  ready before ready coverage reaches its handoff, and proves the retired-current
  interval is ready before the new-source interval reaches its handoff from the
  latest permitted new-source completion point
- **AND** if that retired-current interval remains pending at the ready-to-
  new-source handoff, the retired ready buffer remains idle until it completes
  and starts only the interval after it
- **AND** current and ready coverage remain immutable until their handoffs

#### Scenario: Source-change preparation completes midway through two successive coverage windows

- **WHEN** the new-source interval completes after the current-to-ready handoff
  but before the ready interval reaches its handoff, and its following interval
  completes midway through the new-source interval
- **THEN** the pending interval becomes the ready successor without replacing
  the current ready-plus-bridge playback
- **AND** the retired current buffer starts the interval after that successor
- **AND** at the ready-to-new-source handoff that following interval remains
  pending and the retired ready buffer remains idle
- **AND** when it completes, it becomes ready and the retired ready buffer
  begins the next interval
- **AND** both successive handoffs occur with complete contiguous coverage and
  without overwriting a published or pending view

#### Scenario: A second source change preempts the source-change build

- **WHEN** a second source change arrives while the selected bridge spans the
  current/ready horizon and the build buffer is constructing a new-source view
- **THEN** the selected bridge and current/ready coverage remain immutable
- **AND** the candidate bridge covers the remaining horizon before acceptance
- **AND** the build buffer restarts only for the newest source identity

### Requirement: Active-transport maintenance is attributable by owner and grain

The system SHALL report capture telemetry for each active-transport maintenance
owner that distinguishes completed grain duration, yielded continuation, and
denied eligibility. Timing evidence SHALL distinguish active transport from
STOPPED work and SHALL permit attribution of `OVERDUB_STOPPING` and post-overdub
PLAYING MIDI gaps to the responsible owner and grain. Scored active transport
SHALL include RECORDING, PLAYING, OVERDUBBING, and `OVERDUB_STOPPING`. A timing
window containing capture-ring overflow while in any scored active-transport
interval SHALL be rejected as evidence for the active-transport bound.

#### Scenario: Stopping or post-overdub capture contains a deferred maintenance yield

- **WHEN** an `OVERDUB_STOPPING` or subsequent PLAYING turn denies or yields a
  pending maintenance grain
- **THEN** capture output identifies the owner, grain outcome, and duration
- **AND** the output can be correlated with MIDI-gap and deadline-lateness
  telemetry in the same active-transport interval

#### Scenario: Capture overflow occurs in a scored window

- **WHEN** capture-ring overflow occurs between an active state transition and
  the end of its scored timing window
- **THEN** the timing report marks that window invalid for bound verification
- **AND** it does not use the window to establish a post-overdub MIDI bound
