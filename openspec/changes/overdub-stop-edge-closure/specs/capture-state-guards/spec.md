## ADDED Requirements

### Requirement: Overdub Stop closes input at the press boundary

An accepted overdub Stop SHALL close that interval's input on the Stop edge, independently of deferred-finalization availability. Its destination state SHALL become Playing while transport runs, or Stopped when transport Stop wins. The first Stop's loop-phase close boundary SHALL remain immutable through retries; late input and a later transport epoch SHALL NOT extend that interval.

#### Scenario: Finalization resources are busy
- **WHEN** an overdub is live and its Stop arrives while finalization resources are unavailable
- **THEN** the interval stops accepting input on that edge and its track leaves Overdubbing
- **AND** already-closed notes retain their NoteOffs, outstanding held notes close at the first Stop's phase, and no later input joins that interval
- **AND** the accepted finalization obligation remains owned until exactly-once completion or explicit identity-qualified supersession

#### Scenario: Repeated Stop and delayed recovery
- **WHEN** the same interval's Stop is repeated and finalization later becomes available
- **THEN** the first Stop boundary is unchanged and that interval is sealed and published at most once
- **AND** after successful finalization it contributes exactly its expected content and undo unit

#### Scenario: Transport stops before finalization
- **WHEN** transport stops or restarts while an input-closed interval still owes finalization
- **THEN** its original musical close phase is retained and transport Stop prevents stale completion from restarting playback
- **AND** old-epoch input is not appended to the stopped interval

### Requirement: Successor overdub is independent of predecessor finalization

A new overdub SHALL be allowed to start while a predecessor overdub is stopping, when the existing bounded capture resources can own both intervals. A busy finalizer alone SHALL NOT defer the successor. The successor SHALL use isolated input, reservations, pending-note state, preview and session identity; predecessor completion SHALL NOT close or modify it.

#### Scenario: Successor starts while the predecessor waits
- **WHEN** Stop closes overdub A and A is still waiting for or using finalization resources
- **AND** the next Start arrives with capture resources available
- **THEN** overdub B starts on that Start edge without waiting for A to finalize
- **AND** B's distinct input belongs only to B while A retains exactly its pre-Stop content and close boundary
- **AND** A's later completion leaves B Overdubbing until B's own Stop

#### Scenario: Capture storage reaches its hard bound
- **WHEN** there is no physical capture role available for a successor
- **THEN** the Start is accepted and retained in order rather than overwriting an input-closed predecessor
- **AND** the stopped interval stays input-closed, the resource holder is observable, and the retained Start executes once its capture role is available
- **AND** no unbounded queue or extra canonical finalizer is implied

### Requirement: Stopped preview preserves content without live tail growth

An input-closed overdub's notes SHALL remain visible while finalization is pending, without live playhead extension beyond its Stop boundary. Closed NoteOff geometry SHALL remain authoritative; stopping the live preview SHALL NOT remove its content.

#### Scenario: Predecessor preview during successor capture
- **WHEN** a successor captures while the predecessor's stopped preview is still required
- **THEN** both intervals' visible notes retain their own identities and closures
- **AND** only the successor's genuinely open notes grow with the playhead

### Requirement: Admitted overdub Stop keeps its existing semantics

An overdub Stop whose resources are immediately available SHALL preserve existing capture, closure, publication, undo and persistence semantics without a duplicate seal or publication.

#### Scenario: Uncontended Stop control
- **WHEN** an ordinary overdub Stop is admitted on its press edge
- **THEN** input closes at that press phase, the expected pass is sealed and published once, and subsequent owner turns do not republish it
- **AND** a following overdub remains admissible under the same bounded capture resources
