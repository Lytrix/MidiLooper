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

The system SHALL declare its successor admission capacity per track and SHALL deny,
explicitly and observably, any interval that exceeds it. A denied interval SHALL
leave all previously accepted intervals intact, and no press SHALL be queued
silently, dropped after acceptance, or reported as ordinary latency.

#### Scenario: Capacity is exhausted

- **WHEN** a new interval would exceed the declared capacity because a predecessor
  still owns the resources it needs
- **THEN** the start is refused with an explicit exhaustion outcome
- **AND** the pending predecessor and the live interval keep their accepted data,
  staging and receipts

#### Scenario: Capacity frees after finalization

- **WHEN** the predecessor's finalization retires and releases its frozen staging
  role
- **THEN** a subsequent interval start is admitted again without requiring transport
  stop or a slot reload

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

#### Scenario: Held same-pitch note crosses the boundary

- **WHEN** B starts while A's held note of the same pitch is still being closed by
  A's finalization
- **THEN** B's note identity and closure belong to B
- **AND** A's original audible NoteOff is retained exactly once and cannot terminate
  B's new note

### Requirement: Superseded or diverged publication never selects a stale window

When an interval's recorded causal predecessor is no longer the published root, the
interval SHALL retire without publishing an audible successor window rather than
selecting a window for a source it did not succeed. Transport Stop at any
finalization phase SHALL likewise preserve accepted data and SHALL NOT publish a
stale playback window.

#### Scenario: A publication supersedes the recorded predecessor

- **WHEN** the published root advances past an interval's recorded causal
  predecessor before that interval publishes
- **THEN** its accepted capture is still committed
- **AND** no bridge is selected or published for the stale base
- **AND** the unpublished companion horizon returns to its prewarmed state

#### Scenario: Transport Stop during finalization with a live successor

- **WHEN** transport Stop arrives while a predecessor is finalizing and a successor
  interval is live
- **THEN** the predecessor retargets and completes without publishing a stale
  playback window
- **AND** the successor keeps its accepted input and its own state

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
