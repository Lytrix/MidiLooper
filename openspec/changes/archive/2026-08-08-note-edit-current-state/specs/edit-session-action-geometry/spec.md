## MODIFIED Requirements

### Requirement: EditSessionAction geometry pipeline

Live geometry edits (`EditSessionType::Note` first; Loop and ControlChange later) SHALL use a declarative pipeline:

1. **Transaction baseline** — immutable **`baselineMap`** per **edit driver**.
2. **Current note state** — current editable spans and presence from **NoteEditCurrentState** keyed by `NoteId`.
3. **Edited geometry** — **`EditorSelection`** plus one **linear `NoteBaseline` causing span** per selected note this tick.
4. **Edit projection** (D20) — **`buildEditProjectionContext`** + **`projectEditIntervalsForAnalysis`** produce linear spans before analysis.
5. **Edit session geometry orchestrator** — determine **changed causing notes** and **eligible overlap pairs**; MUST NOT delegate selection or latch logic to analyze.
6. **`analyzeEditSessionInteractions`** — pure geometry facts for supplied causing notes and current-state target spans; **positive interaction graph** only (omit non-overlapping pairs; no **None** enum).
7. **`groupEditSessionInteractionsByTarget`** — group interactions **per target `NoteId`**; ephemeral; MUST NOT persist across ticks.
8. **`resolveConstrainedGeometry`** — for each **constrained geometry target note** (see Resolver scope): committed baseline + current-state span/presence + per-target interactions → **`ConstrainedNoteGeometry`**; MUST NOT know **`EditSessionActionType`**.
9. **`buildEditSessionActions`** — **edit session action builder**: inputs **`constrainedGeometry`**, **`editedGeometry`**, **`transactionBaseline`**, and **current note state**; compute minimal ordered **`EditSessionActions`**; **omit actions that would not change current state**; MUST NOT mutate current state or projected store; MUST NOT inspect **`EditSessionInteraction`** directly.
10. **`applyEditSessionActions`** — **edit session action apply**; the ONLY subsystem that mutates current note state for live geometry actions, then refreshes **`EditSession.store`** from canonical projection.
11. **`normalizeWindow`** on edit closure — geometry tick path boundary (existing DEC-014).
12. **Read-only projection** — MUST NOT write back to committed storage.

The system MUST NOT use a restore-first prelude or persistent overlap scratch as the primary model. The system MUST NOT maintain a persistent **`ConstraintRegistry`** between geometry ticks. The system MUST NOT reintroduce **`ResolutionPolicy`** as a separate pipeline stage.

#### Scenario: Geometry pipeline mutates only during apply

- **WHEN** a NOTE_EDIT geometry update runs
- **THEN** analysis, grouping, resolution, and action building do not mutate current note state or projected store
- **AND** **`applyEditSessionActions`** performs the current-state mutation and projection refresh

### Requirement: Live store

**Live store** (synonyms: **session store**, **current session store**) SHALL mean the in-RAM MIDI event buffer projection for the active NOTE_EDIT session — note on/off pairs projected from current note state.

- During NOTE_EDIT, live store SHALL be **`EditSession.store`** / **`EditManager::sessionMidiEvents()`** as a canonical projection.
- Analyze, group-by-target, resolve, and action-builder stages MUST NOT mutate live store.
- **`applyEditSessionActions`** SHALL mutate current note state and refresh live store from projection.
- **`buildEditSessionActions`** SHALL compare **constrained geometry** and **edited geometry** against current note state to emit actions.
- Live store MUST NOT be confused with transaction baseline, edited geometry, committed loop passes, current note state, or **`overlapNotes`**.
- Normal NOTE_EDIT geometry readers MUST NOT reconstruct editable current state from live-store event absence or pair scans.

#### Scenario: Live store unchanged until apply

- **WHEN** analyze, interaction grouping, constrained geometry resolution, and edit session action builder run
- **THEN** live store event count and ticks are unchanged
- **AND** live store updates only when **`applyEditSessionActions`** refreshes projection after current-state mutation

#### Scenario: Edit session action builder reads current state for restore

- **GIVEN** **`ConstrainedNoteGeometry(A)`** resolves A to visible baseline geometry
- **AND** current state marks **A** as `Hidden`
- **WHEN** **`buildEditSessionActions`** runs
- **THEN** **RestoreNote** is emitted for **A**
- **AND** the comparison uses current-state presence and span, not projected live-store pair absence alone

#### Scenario: Note edit pass commit compares baseline to current state

- **WHEN** **`commitAllPendingNoteEditActions`** runs
- **THEN** **`EditPass`** rows are derived from **transaction baseline compared to note edit current state**
- **AND** not from **`overlapNotes`** or projected live-store absence

#### Scenario: Legacy store diff is diagnostic only

- **GIVEN** legacy live-store diff output exists during NOTE_EDIT migration
- **WHEN** **`commitAllPendingNoteEditActions`** runs after current-state commit authority is active
- **THEN** committed **`EditPass`** rows are derived from current state
- **AND** legacy live-store diff output MAY be compared for diagnostics / parity logging
- **AND** legacy live-store diff output MUST NOT change the committed **`EditPass`** output

#### Scenario: Analyzer independent of edit-session projection mutation

- **WHEN** `analyzeEditSessionInteractions` runs
- **THEN** it receives only causing-note inputs, current target spans/presence, and geometry spans passed by the orchestrator
- **AND** it does not mutate current state or projected live store

### Requirement: Edit session action builder inputs

**`buildEditSessionActions`** SHALL accept constrained geometry, edited geometry, transaction baseline, and a read-only view of note edit current state.

The builder SHALL observe these inputs only; it SHALL compute **`EditSessionActions`**; it SHALL not mutate current state or projected live store.

Input authorities:

- **`constrainedGeometry`** — desired overlap-target geometry from resolve.
- **`editedGeometry`** — user intent this tick for causing spans.
- **`transactionBaseline`** — original note geometry for this edit driver.
- **`NoteEditCurrentState`** — current editable span and presence for each `NoteId`.

#### Scenario: Builder observes inputs without mutation

- **WHEN** **`buildEditSessionActions`** receives constrained geometry, edited geometry, transaction baseline, and current note state
- **THEN** it emits **`EditSessionActions`**
- **AND** current note state and projected live store remain unchanged until **`applyEditSessionActions`**

### Requirement: Invariant — Constrained Geometry Authority

**`ConstrainedNoteGeometry`** SHALL be the sole description of desired overlap-target geometry during a geometry update tick.

**`buildEditSessionActions`** SHALL derive overlap-target actions from **constrained geometry**, **transaction baseline**, and current note state; causing-note actions from **edited geometry** and current note state.

**`buildEditSessionActions`** SHALL **omit actions that would not change current state** for that **`NoteId`**.

**`buildEditSessionActions`** MUST NOT inspect **`EditSessionInteraction`** or **`EditSessionInteractionsByTarget`** directly.

#### Scenario: Builder consumes constrained geometry, not interactions

- **WHEN** overlap-target desired geometry is available as **`ConstrainedNoteGeometry`**
- **THEN** **`buildEditSessionActions`** maps that geometry to current-state-changing actions
- **AND** it does not inspect **`EditSessionInteraction`** rows
