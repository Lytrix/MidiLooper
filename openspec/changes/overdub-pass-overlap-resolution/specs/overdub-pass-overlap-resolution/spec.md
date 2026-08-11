## ADDED Requirements

### Requirement: Two-pass overdub overlap model

The system SHALL resolve overdub note overlap as a two-pass transformation: (1) one immutable **source pass** providing pre-session canonical note geometry, and (2) one **overdub pass** that records Add / Shorten / Hide (or equivalent) consequences. The source pass SHALL NOT be destructively rewritten during overdub capture. The term **source pass** SHALL mean the single immutable pre-session canonical committed note-geometry view of the active loop slot, not an ad-hoc scan of arbitrary historical pass rows, last-appended events, or undo grouping.

#### Scenario: Source remains unchanged when overdub shortens

- **GIVEN** source geometry contains note A spanning a long interval
- **WHEN** an overlapping shorter note B is inserted during overdub
- **THEN** source pass geometry for A remains unchanged
- **AND** the pending overdub pass records the shorten (or hide) consequence for A and the add for B per canonical overlap rules

#### Scenario: Covering note encodes removals on overdub pass

- **GIVEN** source geometry contains short notes A, B, C
- **WHEN** a long overlapping note X is inserted during overdub that covers them under canonical rules
- **THEN** the overdub pass records remove/hide for A, B, C and add for X as required by those rules
- **AND** the source pass rows remain unmodified

### Requirement: Source-pass candidate lookup domain

For each newly inserted overdub note that requires overlap evaluation, candidate lookup SHALL search the immutable source-pass geometry. Lookup MUST be wrap-safe and MUST NOT rely on capture append-order tick monotonicity. The system MUST NOT use `lastSeenTick`, last-appended event, current loop wrap as a materialization boundary, or undo grouping as a substitute for the source-pass search domain. Candidate lookup SHOULD use existing committed chunk/window intersection facilities where applicable.

#### Scenario: High-then-low append order still finds source candidates

- **GIVEN** the pending capture store contains high loop-phase ticks followed by low loop-phase ticks after wrap
- **WHEN** a new note is inserted at a low loop-phase tick that overlaps source geometry
- **THEN** candidate lookup still finds the relevant source notes
- **AND** evaluation does not terminate solely because a reverse walk of append order encountered a lower tick

#### Scenario: lastSeenTick is not authority

- **WHEN** overlap evaluation runs for an inserted overdub note
- **THEN** the decision MUST NOT be determined solely by a `(channel, note, type) → lastSeenTick` map

### Requirement: Evaluate on insert across wraps

During an overdub session, every newly inserted note that can overlap source material SHALL be evaluated when it is inserted. A loop wrap SHALL NOT suppress or defer that evaluation merely because the same normalized loop phase was evaluated earlier in the session.

#### Scenario: Same phase re-evaluated on later wrap

- **GIVEN** an overdub session inserts note A at phase P during wrap 1 and evaluates it
- **WHEN** the session inserts note A again at phase P during wrap 2
- **THEN** overlap evaluation runs again against the source pass
- **AND** wrap 2 insertion is not skipped as “already handled”

### Requirement: Session commit remains one overdubPass

An overdub session (`start overdub` through `stop overdub` / `commitCapturePass`) SHALL produce exactly one committed `overdubPass` and one corresponding overdub undo unit under the current `timeline-passes` model, regardless of how many loop wraps or per-note evaluations occurred. A loop wrap SHALL NOT create a pass or undo unit.

#### Scenario: Multi-wrap session one undo

- **GIVEN** an overdub session spans three loop wraps with multiple overlap evaluations
- **WHEN** overdub stops and `commitCapturePass` succeeds
- **THEN** exactly one `overdubPass` is committed
- **AND** exactly one overdub undo unit is created for that session

### Requirement: Note-edit overlap policy parity

For the same source note geometry and the same incoming note geometry, overdub overlap resolution SHALL apply the same shorten / hide / min-length decisions as NOTE_EDIT geometry resolution (`NoteGeometryResolver` / `resolveConstrainedGeometry` semantics). Overdub MAY use a different apply/encode target (pending overdub pass) than `NoteEditSession.store`, but MUST NOT invent a contradictory overlap policy. Exact duplicate handling SHALL be treated as one outcome of that flow, not as a separate capture-only semantic model.

#### Scenario: Equivalent geometry same shorten decision

- **GIVEN** identical source span A and identical incoming span B
- **WHEN** NOTE_EDIT geometry resolution and overdub overlap resolution both evaluate the pair
- **THEN** both decide the same shorten or hide outcome for A relative to B under the shared min-length globals

### Requirement: Persistence does not resolve overlap

Persistence, deferred save, mid-pass seal, load, and recovery MUST NOT invent, remove, or shorten notes by re-running overdub overlap resolution. They MAY persist sealed capture material and/or the committed `overdubPass` according to existing persistence contracts.

#### Scenario: Load does not invent shorten

- **WHEN** a loop is loaded from SD after an overdub that shortened a source note via overdub-pass ops
- **THEN** persistence reconstructs committed passes without performing a new geometric overlap resolve
- **AND** musical outcome matches the committed pass content

### Requirement: Shared minimum note length globals

Overdub overlap hide/shorten decisions that depend on minimum retained note length SHALL use `Config::noteMinLengthTicks` and `Config::noteMinLengthRemoveEnabled` (the same globals used by NOTE_EDIT constrained geometry). Capture-tier pair sanity that removes pairs shorter than the minimum SHALL use those same tick globals.

#### Scenario: Boundary uses shared threshold

- **GIVEN** `noteMinLengthTicks` is N and remove-enabled is true
- **WHEN** overlap resolution would leave a source note shorter than N
- **THEN** the note is hidden/removed according to constrained-geometry rules using threshold N
