## Purpose

Keep piano-roll and visual-cache reconstruction from blocking MIDI input or capture during active transport, while preserving exact musical content and coherent displayed intervals.

## ADDED Requirements

### Requirement: Real-time visual reconstruction has bounded indivisible work

Every visual-cache reconstruction operation executed between MIDI input service opportunities SHALL honor a declared, enforced per-turn work bound with persistent continuation. This SHALL include snapshot/prepared-source misses, event pairing, edit application, filtering, append/compose work and retirement. A small requested display window SHALL NOT permit whole-pass reconstruction before window filtering, and a time budget checked only after an indivisible content-sized step SHALL NOT establish a bound.

#### Scenario: Large committed source misses current snapshot and prepared views

- **WHEN** a large committed source needs a visual window while Record or overdub is active and current/prepared representations are unavailable
- **THEN** each real-time turn performs only its declared bounded work and retains qualified continuation
- **AND** MIDI input service is not delayed by a synchronous whole-pass display reconstruction

#### Scenario: Other tracks reconstruct while the selected track records

- **WHEN** another eligible track has dirty visual content while the selected track begins Record
- **THEN** its reconstruction follows the same bound and cannot monopolize the capture track's input-service opportunities
- **AND** acceptance evidence identifies the actual reconstruction owner rather than inferring it from selected-track state

### Requirement: Deferred visual work preserves exact content and coherent presentation

Deferred reconstruction SHALL preserve pass-local pairing, event ordering, note identity, edits and wrap semantics. It SHALL retain a coherent valid presentation until complete replacement content and its interval can be published together. Incomplete reconstruction SHALL NOT be presented as a complete current frame, and a deferral SHALL have a reachable continuation owner.

#### Scenario: Bounded work completes under ordinary service

- **WHEN** reconstruction receives its declared service opportunities without source supersession
- **THEN** it eventually publishes content equal to the independent pass-local oracle for the requested interval
- **AND** intermediate turns preserve the prior qualified presentation rather than mixing content from one interval with another viewport

#### Scenario: Source changes while reconstruction is private

- **WHEN** revision, owner generation, geometry or slot identity changes during deferred reconstruction
- **THEN** obsolete work cannot publish into the replacement source
- **AND** continuation retires or rebinds through its owner without discarding accepted Capture input

#### Scenario: Completed predecessors remain visible on later overdub entry

- **WHEN** earlier overdub jobs finish while a successor remains live and a later overdub starts before committed display replacement is ready
- **THEN** the earlier closed notes remain visible at their accepted identities and geometry through the display handoff
- **AND** retiring their stop descriptors or opening the next session does not erase those notes, reset the live successor, or require a delayed full rebuild to restore visibility


#### Scenario: Partial-bar Record stop rewinds the display window

- **WHEN** Record Stop truncates a partial trailing bar and rewinds the requested display interval while canonical publication is private
- **THEN** a complete qualified stopped-preview representation can replace content and interval coherently
- **AND** recorded notes in the requested interval remain visible, trimmed trailing rows do not wrap into its first bar, and held-note endpoints follow the frozen Stop geometry
- **AND** an old tail-window frame is not presented as complete content for a different viewport

#### Scenario: Overdub NoteOff before Record publication

- **WHEN** a live overdub note is shown while held and its NoteOff arrives before the Record predecessor publishes
- **THEN** the accepted note remains visible at its closed coordinates in subsequent composed frames
- **AND** temporary open-tail paint is not the sole carrier of the note, and repeated frames do not duplicate its identity

#### Scenario: Rounded-up Record stop followed by wrapped overdub input

- **WHEN** the prior tail viewport is still valid but overdub wraps into an earlier interval before Record publication
- **THEN** an available complete qualified Record preview permits coherent content/viewport replacement for ordinary follow
- **AND** accepted live notes in that interval appear without waiting for canonical publication; absence of a complete replacement retains the existing held-window fallback

#### Scenario: Empty successor cannot grow predecessor tails

- **WHEN** a new overdub starts before receiving its own first input while a stopped predecessor preview is borrowed
- **THEN** stopped note identities and endpoints stay frozen at their accepted Stop geometry
- **AND** only the live producer's open notes may grow with its playhead; borrowed predecessor notes cannot create transient long or wrap tails



### Requirement: Visual bounds and MIDI timing are verified separately

Visual reconstruction SHALL have production-path logical bound tests and device timing evidence under the supported capture/display/persistence workload. Native bounded-unit results SHALL NOT be reported as physical MIDI timing qualification, and missing or shed evidence SHALL NOT be treated as a passing timing result.

#### Scenario: Recording-start contention is qualified

- **WHEN** the clear, transport-start and Record sequence runs with a forced visual-source miss and background contention
- **THEN** the run records owner-qualified work, maximum input-service gap and output lateness against the declared contract
- **AND** a later warm overdub cycle does not substitute for first-Record acceptance
