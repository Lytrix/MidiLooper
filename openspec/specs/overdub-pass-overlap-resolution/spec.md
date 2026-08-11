## Purpose

Overdub session overlap against a stable, materialize-aware **`overdubSourceView`**: per-note
evaluation yields a complete logical delta (Add / Shorten / Hide) via shared
`resolveConstrainedGeometry` semantics; transitional dual-storage seal (capture chunks + EditPass
companions) and one `OverdubPassAdded` undo. Shipped in **overdub-pass-overlap-resolution**
(DEC-031 / DEC-032 G2). U1 unified persistent pass storage is out of scope.

**Guides:** [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)  
**Evidence:** [`docs/Plans/long_overdub_wrap_duplicate_display_freeze_bugfix.md`](../../docs/Plans/long_overdub_wrap_duplicate_display_freeze_bugfix.md)

## Requirements

### Requirement: Stable overdubSourceView at session start

At the start of an overdub session the system SHALL establish an **`overdubSourceView`**: a materialize-aware canonical note-geometry view of the active loop slot that includes the effective result of committed record/overdub material and applicable **`editPasses`**. The view SHALL remain **semantically stable** for the lifetime of that overdub session. Establishing the view MUST NOT be described as freezing the loop. The physical backing (event vector, note spans, chunk/window view, or combination) is an implementation choice provided this semantic contract holds. The system MUST NOT use bare `CommittedEventRange` alone as the source when active `editPasses` exist.

#### Scenario: Source view established at overdub start

- **WHEN** an overdub session begins
- **THEN** an `overdubSourceView` is established before overlap evaluation for newly inserted notes
- **AND** that view includes materialize-aware geometry (including `editPasses` when present)

#### Scenario: Source view stable across wraps

- **GIVEN** an overdub session with an established `overdubSourceView`
- **WHEN** the playhead wraps the loop one or more times during the session
- **THEN** subsequent overlap evaluations still use the same semantic `overdubSourceView`
- **AND** evaluation is not retargeted to a different pass identity solely because a wrap occurred

### Requirement: OverdubPass is a complete delta

The committed `overdubPass` SHALL represent the **complete delta** required to transform the session’s `overdubSourceView` into the overdub result. The delta SHALL be able to include newly added notes and changes to source notes (shorten and remove/hide) according to canonical overlap rules. Source material SHALL NOT be destructively rewritten in place during the session.

#### Scenario: Short note overlapping long source note

- **GIVEN** `overdubSourceView` contains note A spanning a long interval
- **WHEN** overlapping shorter note B is inserted and resolved during overdub
- **THEN** source geometry for A remains unchanged in the source view
- **AND** the overdub delta records Shorten (or Hide) for A and Add for B as required by canonical rules

#### Scenario: Long note covering multiple short source notes

- **GIVEN** `overdubSourceView` contains short notes A, B, C
- **WHEN** long covering note X is inserted and resolved under canonical rules
- **THEN** the overdub delta records Remove/Hide for A, B, C and Add for X as required
- **AND** the source view itself is not rewritten

### Requirement: Source-view candidate lookup domain

For each newly inserted overdub note that requires overlap evaluation, candidate lookup SHALL search the session’s `overdubSourceView`. Lookup MUST be wrap-safe and MUST NOT rely on capture append-order tick monotonicity. The system MUST NOT use `lastSeenTick`, last-appended event, current loop wrap as a materialization boundary, or undo grouping as a substitute for the source-view search domain. Candidate lookup SHOULD prefer existing committed chunk/window facilities where applicable. Optimization MUST NOT reduce the semantic candidate domain.

#### Scenario: High-then-low append order still finds source candidates

- **GIVEN** the pending capture store contains high loop-phase ticks followed by low loop-phase ticks after wrap
- **WHEN** a new note is inserted at a low loop-phase tick that overlaps source-view geometry
- **THEN** candidate lookup still finds the relevant source notes
- **AND** evaluation does not terminate solely because a reverse walk of append order encountered a lower tick

#### Scenario: lastSeenTick is not authority

- **WHEN** overlap evaluation runs for an inserted overdub note
- **THEN** the decision MUST NOT be determined solely by a `(channel, note, type) → lastSeenTick` map

### Requirement: Evaluate on insert across wraps

During an overdub session, every newly inserted note that can overlap source-view material SHALL be evaluated when it is inserted. A loop wrap SHALL NOT suppress or defer that evaluation merely because the same normalized loop phase was evaluated earlier in the session.

#### Scenario: Same phase re-evaluated on later wrap

- **GIVEN** an overdub session inserts note A at phase P during wrap 1 and evaluates it against `overdubSourceView`
- **WHEN** the session inserts note A again at phase P during wrap 2
- **THEN** overlap evaluation runs again against the same `overdubSourceView`
- **AND** wrap 2 insertion is not skipped as “already handled”

### Requirement: Session commit remains one overdubPass

An overdub session (`start overdub` through `stop overdub` / `commitCapturePass`) SHALL produce exactly one committed `overdubPass` and one corresponding overdub undo unit under the current `timeline-passes` model, regardless of how many loop wraps or per-note evaluations occurred. A loop wrap SHALL NOT create a pass or undo unit. Undoing the overdub pass SHALL remove its delta (additions and source-note transformations), exposing the previous materialized state.

#### Scenario: Multi-wrap session one undo

- **GIVEN** an overdub session spans three loop wraps with multiple overlap evaluations
- **WHEN** overdub stops and `commitCapturePass` succeeds
- **THEN** exactly one `overdubPass` is committed
- **AND** exactly one overdub undo unit is created for that session

### Requirement: Note-edit overlap policy parity

For the same source note geometry and the same incoming note geometry, overdub overlap resolution SHALL apply the same shorten / hide / min-length decisions as NOTE_EDIT geometry resolution (`resolveConstrainedGeometry` semantics). Overdub MAY use a different apply/encode target for the pending overdub delta than `NoteEditSession.store`, but MUST NOT invent a contradictory overlap policy. Exact duplicate handling SHALL be one outcome of that flow, not a separate capture-only semantic model.

#### Scenario: Equivalent geometry same shorten decision

- **GIVEN** identical source span A and identical incoming span B
- **WHEN** NOTE_EDIT geometry resolution and overdub overlap resolution both evaluate the pair
- **THEN** both decide the same shorten or hide outcome for A relative to B under the shared min-length globals

### Requirement: Persistence does not resolve overlap

Persistence, deferred save, mid-pass seal, load, and recovery MUST NOT invent, remove, or shorten notes by re-running overdub overlap resolution. They MAY persist sealed capture material and/or the committed `overdubPass` according to existing persistence contracts.

#### Scenario: Load does not invent shorten

- **WHEN** a loop is loaded from SD after an overdub that shortened a source note via overdub-pass delta
- **THEN** persistence reconstructs committed passes without performing a new geometric overlap resolve
- **AND** musical outcome matches the committed pass content

### Requirement: Shared minimum note length globals

Overdub overlap hide/shorten decisions that depend on minimum retained note length SHALL use `Config::noteMinLengthTicks` and `Config::noteMinLengthRemoveEnabled` (the same globals used by NOTE_EDIT constrained geometry). The system MUST NOT introduce an overdub-specific minimum-length constant. Capture-tier pair sanity that removes pairs shorter than the minimum SHALL use those same tick globals.

#### Scenario: Boundary uses shared threshold

- **GIVEN** `noteMinLengthTicks` is N and remove-enabled is true
- **WHEN** overlap resolution would leave a source note shorter than N
- **THEN** the note is hidden/removed according to constrained-geometry rules using threshold N

### Requirement: Ownership of overdubSourceView lifetime

`Track` SHALL own overdub session lifecycle that triggers establish/clear of the source view. `Loop` SHALL own providing the canonical materialized loop state used as `overdubSourceView`. The system MUST NOT introduce a new top-level Manager for this view.

#### Scenario: Lifecycle trigger without new Manager

- **WHEN** overdub starts and later stops
- **THEN** `overdubSourceView` is established and cleared under Track lifecycle + Loop state ownership
- **AND** no new top-level Manager type is required for the view
