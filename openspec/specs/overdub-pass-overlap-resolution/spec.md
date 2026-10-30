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

### Requirement: Monotonic loop-relative touched coverage

Overdub source preparation SHALL be driven by the **loop-relative phase regions the session has actually
touched**, not by a window that follows the playhead.

**Required coverage is "phases touched so far", established by three cheap transitions** — the note's
final span is not known at acceptance:

- **NoteOn** marks the **pressed-phase tile** required;
- **held advancement** marks each newly crossed tile required while the playhead continues through the
  held note;
- **NoteOff or stop** seals the complete span and ensures every tile in it is required.

None of the three SHALL perform a source derivation.

Required coverage SHALL be **monotonic within an overdub session**: once a tile is required it SHALL NOT
become unrequired. Playhead movement and a loop wrap SHALL NOT invalidate, unrequire or move coverage
that is already prepared, and a wrap SHALL add a tile only when a **held note actually continues into an
as-yet-untouched phase**.

Coverage SHALL be represented as a set of small bounded tiles rather than a single contiguous interval.

#### Scenario: Sparse touches in distant regions

- **GIVEN** a 31-bar loop and an overdub session
- **WHEN** notes are pressed at bar 2 and at bar 27 and nowhere else
- **THEN** required coverage is those two tiles
- **AND** the untouched middle is not required and not prepared

#### Scenario: Repeated wraps with no held note crossing

- **WHEN** the transport wraps the loop repeatedly and no note is held across a new phase
- **THEN** required coverage does not grow and no new source derivation occurs

#### Scenario: A held note continuing into an untouched phase

- **GIVEN** a note held across a wrap
- **WHEN** the playhead carries it into a tile that is not yet required
- **THEN** that tile becomes required
- **AND** it is prepared by the deferred owner, not on the event path

#### Scenario: Revisiting a prepared tile

- **WHEN** a note is played inside a tile already prepared for the same source generation
- **THEN** the tile is not re-derived

### Requirement: Coverage tiles are not execution bounds

A tile SHALL be the **spatial** unit of correctness and publication atomicity: what is required, what is
prepared atomically, and what the composition rule applies to. It SHALL NOT be treated as an execution
bound.

The **execution** unit SHALL be a separate, resumable **event/operation cursor inside a tile**, because a
dense tile can contain far more events than a nominal one and conservative closure can make a small tile
inspect much of the loop. A worker turn SHALL stop at its declared grain and retain its cursor.

#### Scenario: Dense tile does not fit one turn

- **GIVEN** a tile whose content exceeds the declared grain
- **WHEN** the worker prepares it
- **THEN** the turn stops at the declared grain
- **AND** the tile is not published as prepared until a later turn completes it

#### Scenario: Long closure does not enlarge the turn

- **GIVEN** a maximum committed note span that makes a small tile inspect most of the loop
- **WHEN** the worker prepares that tile
- **THEN** the turn still stops at the declared grain

### Requirement: Event acceptance marks demand; preparation stays deferred

Accepting, holding, completing or abandoning a note SHALL only mark and queue coverage demand. It SHALL
NOT synchronously perform substantial source derivation on the note-on, note-off, hold or stop path.

After marking, preparation SHALL be performed by the existing bounded deferred-maintenance owner, one
declared grain per turn, with each grain confined to one required tile.

#### Scenario: Acceptance does not derive

- **WHEN** an accepted note extends required coverage by a tile that is not yet prepared
- **THEN** the note is accepted without a source derivation on that path
- **AND** the tile's preparation is owed to the deferred owner

### Requirement: Named prerequisites gate what needs coverage

Where an overdub decision semantically requires source information, that requirement SHALL be a **named
prerequisite state**, never an inline derive and never a blocked MIDI path.

Three prerequisites SHALL exist:

- **`OccupancyAtPhase(phase)`** — on the NoteOn path. The MIDI note SHALL be accepted immediately, the
  pressed-phase demand marked, and only then ledger authority consulted. When the ledger cannot establish
  the answer and the tile is not prepared, the overlap decision becomes a named owed prerequisite. A
  refused source-view fallback SHALL NOT be interpreted as "not occupied".
- **`HeldParticipationAtPhase(phase)`** — on the hold path, replacing the synchronous derive that
  currently runs there. It SHALL mark the currently touched tile and consume only prepared source data,
  deferring the overlap-side consequence by name when the tile is not prepared.
- **`SpanFinalization(span)`** — when the note completes. If the span's tiles are not prepared for the
  current generation, the completion SHALL be deferred by name and SHALL NOT be computed against partial
  coverage.

Only the span **behind** the playhead may lag while a note is held.

#### Scenario: Occupancy at an unprepared pressed phase

- **WHEN** a note is pressed at a phase whose tile is not prepared
- **THEN** occupancy is answered from the playback ledger
- **AND** if the ledger cannot answer, the decision is a named owed prerequisite
- **AND** the note is not treated as unoccupied

#### Scenario: Held participation cannot derive

- **WHEN** a held note's phase falls in an unprepared tile
- **THEN** the hold path performs no source derivation
- **AND** the overlap-side consequence is deferred by name until the tile is prepared

#### Scenario: Completion requires coverage of its own span

- **WHEN** a completing note's span extends into a tile that is still owed
- **THEN** its overlap delta is not computed against an incomplete tile
- **AND** the deferral names the unsatisfied prerequisite
- **AND** the completion still happens when the tile finishes

### Requirement: Prerequisite obligations persist until discharged

A prerequisite is **not** a transient function-call result. When coverage needed by
`OccupancyAtPhase`, `HeldParticipationAtPhase` or `SpanFinalization` is unavailable, the semantic
consequence SHALL become an **owed obligation** that persists until it is discharged or the overdub
session ends. No consequence SHALL be forgotten because the playhead has moved past the tile before
preparation completed.

Obligations SHALL be **coalesced by the minimum stable identity that keeps them semantically distinct** —
note identity plus region/tile — and SHALL NOT be queued once per playback tick, once per hold sample, or
once per event.

Obligations SHALL be qualified by the source generation and SHALL be retained and re-owed alongside
required coverage when the generation changes.

#### Scenario: A held note leaves the tile before it is prepared

- **GIVEN** a held note that enters an unprepared tile
- **WHEN** the note leaves that tile and the tile is prepared afterwards
- **THEN** the held-participation consequence still executes, exactly once

#### Scenario: Occupancy owed into completion

- **GIVEN** a note whose note-on occupancy prerequisite is owed
- **WHEN** the note reaches note-off before its tile is prepared
- **THEN** the preparation still produces the same final overlap delta as the synchronous reference
- **AND** the delta is not computed twice

#### Scenario: Many hold ticks in one unprepared tile

- **WHEN** a held note crosses a single unprepared tile many times
- **THEN** the obligation set stays bounded by note identity plus tile
- **AND** no per-tick backlog accumulates

#### Scenario: Generation change retains obligations

- **WHEN** the source generation changes while obligations are outstanding
- **THEN** the obligations are retained for the new generation
- **AND** they are re-owed with the required coverage rather than discarded

### Requirement: Conservative closure and identity composition

Candidate lookup SHALL be expanded backward by the **maximum committed source-note span**, so a committed
note that begins before a required tile and sustains into it is still a candidate. This is **conservative
closure**, not an exact domain: a global maximum admits more than necessary but SHALL NOT under-admit. A
tighter per-pitch or per-tile index MAY replace it later.

Composition across tiles SHALL be by stable **note identity**, not by concatenating per-tile candidate
lists: the same source note may fall in the backward closure of several adjacent tiles and SHALL appear
once in the composed candidate set.

When the maximum committed note span is greater than or equal to the loop length, closure SHALL
**saturate to the whole loop** rather than producing ambiguous modular ranges.

#### Scenario: Note sustaining into a required tile

- **GIVEN** a committed note that starts before a required tile and sustains into it
- **WHEN** an overdub note inside that tile is evaluated
- **THEN** the committed note is a candidate
- **AND** the resulting delta (shorten or hide) is recorded

#### Scenario: A note in two closures appears once

- **GIVEN** a committed note visible in the backward closure of two adjacent tiles
- **WHEN** the tiles' candidates are composed
- **THEN** the note appears exactly once, with its own identity

#### Scenario: Closure saturates

- **WHEN** the maximum committed note span is at least the loop length
- **THEN** closure covers the whole loop
- **AND** no ambiguous or duplicated modular range results

### Requirement: Preparation never exposes partial coverage as ready

A tile SHALL be published ready, atomically, only when its preparation has completed for the current
source generation. A tile in flight SHALL be reported as owed, not as prepared.

A change of source generation SHALL **not** discard required coverage: that is session state describing
what the session touched, and the session may remain semantically alive across the change. Instead,
prepared coverage and any in-flight preparation SHALL be invalidated, and **every currently required tile
SHALL become pending again** for the new generation.

#### Scenario: Multi-turn preparation is not visible early

- **WHEN** a tile's preparation spans several deferred turns
- **THEN** no consumer observes that tile as ready before its completing grain
- **AND** no partially prepared tile is used for a decision

#### Scenario: Source generation changes

- **WHEN** the committed source generation changes during a session
- **THEN** prepared coverage for the previous generation stops being authoritative
- **AND** every required tile is re-owed for the new generation
- **AND** required coverage itself is preserved

### Requirement: Convergence to whole-loop parity

Preparation SHALL converge to the whole-loop derivation: once every loop phase has been touched and
prepared for one generation, the composed candidate geometry SHALL be identical to the whole-loop
derivation for that generation — including wrap-crossing notes, repeated and simultaneous same-pitch
notes, note identity, start and end geometry, and the absence of duplicates and omissions.

Whole-loop prewarm SHALL be an optional optimisation and SHALL NOT be a prerequisite for correctness.

#### Scenario: Touching the whole loop converges

- **GIVEN** an overdub session whose notes cover every loop phase
- **WHEN** preparation completes
- **THEN** candidate geometry equals the whole-loop derivation for that generation
- **AND** no duplicate or omitted note results from the tile boundaries

#### Scenario: Sparse session does not converge to whole-loop cost

- **GIVEN** a session that touches only two bars of a 31-bar loop
- **WHEN** preparation completes
- **THEN** the prepared work is proportional to those two bars, not to the loop

### Requirement: Source-view preparation is not the capture path

Establishing the view at session open remains the capture path's business and keeps its existing timing
contract. The touched-coverage preparation defined here SHALL apply to maintenance during and after the
session and SHALL NOT be introduced into the overdub-open path.

#### Scenario: Session open unchanged

- **WHEN** an overdub session opens
- **THEN** the view is established by the existing open path
- **AND** the touched-coverage preparation is not invoked as part of opening
