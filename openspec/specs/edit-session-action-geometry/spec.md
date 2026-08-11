## Purpose

Declarative NOTE_EDIT geometry: transaction baseline + edited geometry → analyze → constrained
geometry → **EditSessionActions** → **applyEditSessionActions** on **NoteEditSession.store**.
Shipped in **edit-session-action-geometry** (archived 2026-08-05). Loop and ControlChange
extensions use the same pipeline entry shape later.

**Guides:** [`docs/Guides/MOVE_NOTE_LOGIC.md`](../../docs/Guides/MOVE_NOTE_LOGIC.md),
[`docs/Guides/NOTE_WRAPPING_LOGIC.md`](../../docs/Guides/NOTE_WRAPPING_LOGIC.md) (display wrap vs live edit).
## Requirements
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

### Requirement: Edited geometry

**Edited geometry** SHALL mean the user-intended geometry for the current geometry update tick — **`EditorSelection`** plus **linear causing spans** for selected notes — before **`applyEditSessionActions`** runs.

- **`EditorSelection`** SHALL identify **`selectedNotes`**, **`primaryNote`**, and bracket scope.
- Each **`NoteId`** in **`selectedNotes`** SHALL have a causing span (**`NoteBaseline`**: pitch, velocity, `startTick`, `endTick` in linear ticks).
- The **`primaryNote`** causing span SHALL come from **`NoteEditFocus.last`** (live fader/encoder values).
- **Prior latch** (previous tick span per note) SHALL be used only by the orchestrator for **`geometryChangedThisTick`** — not as edited geometry and not as analyze input for emit policy.
- Edited geometry MUST NOT include transaction baseline, **live store** state, or overlap-target desired geometry.

#### Scenario: Primary driver span from focus.last

- **GIVEN** **`EditorSelection.primaryNote`** is **B** and the user moves fader 2
- **WHEN** the geometry pipeline starts this tick
- **THEN** **B**'s causing span in edited geometry matches **`NoteEditFocus.last`**
- **AND** **`baselineMap[B]`** remains unchanged until the edit driver changes

#### Scenario: Edited geometry differs from live store before apply

- **GIVEN** note **B** overlaps hidden target **A**
- **WHEN** edited geometry for **B** is computed and analyze runs
- **THEN** **B**'s causing span reflects user input
- **AND** **live store** for **A** may still reflect hide until **`applyEditSessionActions`** runs

#### Scenario: Prior latch is not edited geometry

- **WHEN** the orchestrator evaluates **`geometryChangedThisTick`**
- **THEN** it compares current causing span to **prior latch**
- **AND** prior latch is not included in the **`EditedGeometry`** payload passed to analyze

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

### Requirement: Positive interaction graph

Interaction analysis SHALL emit **only positive geometry facts**.

- Non-overlapping **(causing, target)** pairs SHALL be **omitted** from analyze output.
- The absence of an interaction SHALL be represented by the **absence** of an **`EditSessionInteraction`** record — **not** by **`InteractionType::None`** or any other sentinel.
- **`InteractionType`** SHALL NOT include **None**.

#### Scenario: Omitted pair means no interaction

- **GIVEN** causing note **B** no longer overlaps target **A**
- **WHEN** `analyzeEditSessionInteractions` runs for **B**
- **THEN** no **`EditSessionInteraction`** exists for **(B, A)**
- **AND** absence of the pair is sufficient for downstream restore — no **None** sentinel

#### Scenario: Start-abut leave stays OverlapNoteOff

- **GIVEN** left target **A** baseline ends at tick **T**
- **AND** causing note **B** starts at tick **T** (start-abut)
- **WHEN** `analyzeEditSessionInteractions` classifies **(B, A)**
- **THEN** **`InteractionType`** is **OverlapNoteOff** (not **BoundaryTouch**)
- **AND** **`computeShortenedEndTick`** is **T − 1**
- **AND** resolve/builder emit **ShortenNote** to **T − 1** rather than full-baseline **RestoreNote**
- **AND** full restore occurs only when **B.startTick > T** (pair omitted)

### Requirement: Interactions grouped by target

**`groupEditSessionInteractionsByTarget`** SHALL regroup analyze output each geometry tick.

- Every **target `NoteId`** SHALL appear **at most once** in **`EditSessionInteractionsByTarget.groups`**.
- Each **`TargetNoteInteractionGroup`** SHALL contain **all** incoming interactions for that target this tick.
- **`incoming`** order SHALL be deterministic — ascending **`causingNoteId`**.
- **`groups`** order SHALL be deterministic — ascending **`targetNoteId`**.

#### Scenario: All incoming interactions per target

- **GIVEN** causing notes **B** and **C** both constrain target **A**
- **WHEN** **`groupEditSessionInteractionsByTarget`** runs
- **THEN** **A**'s **`TargetNoteInteractionGroup`** lists incoming interactions from **B** and **C** in ascending **`causingNoteId`** order
- **AND** **`resolveConstrainedGeometry(A)`** consumes only that per-target group

### Requirement: Resolver scope and combine precedence

**`ConstrainedNoteGeometry`** SHALL be produced only for **target `NoteId`** values that:

1. have **one or more incoming interactions** in **`EditSessionInteractionsByTarget`** this tick, **or**
2. are **overlap restore candidate notes** — **transaction baseline** has the note; **live store** linear span differs from baseline (**hidden**, **shortened**, or **missing pair**) due to side-effects of the **current edit driver**; restore may be required when interactions rebuild empty.

**Output invariants:**

- Every **target `NoteId`** SHALL appear **at most once** in the constrained geometry collection.
- The resolver SHALL NOT run for all loop notes. **Causing notes** are handled via **edited geometry** in the **edit session action builder**, not **`resolveConstrainedGeometry`**.

**Combine precedence** (evaluated in order):

1. **Complete hide precedence** — any **OverlapNoteOn** or **CompleteCover** → **`visible = false`**.
2. **Restrictive shorten combine** — else one or more **OverlapNoteOff** → **`visible = true`**, **`startTick = B.startTick`**, **`endTick = min(computeShortenedEndTick(i))`** over **OverlapNoteOff** rows (**earliest linear note-off tick**; not a length limit).
3. **Boundary unchanged** — else **BoundaryTouch** only → baseline span unchanged.
4. **Baseline equivalent** — else no incoming interactions → baseline-equivalent span.
5. **Minimum note edit length hide** — when **`noteMinLengthRemoveEnabled`**, if **`visible = true`** and **`endTick − startTick < noteMinLengthTicks`** → **`visible = false`**. When disabled, skip (shorten stands).

#### Scenario: Resolver scope includes only constrained targets

- **GIVEN** grouped interactions contain target **A**
- **AND** live store differs from baseline for restore candidate **C**
- **WHEN** constrained geometry target notes are computed
- **THEN** **A** and **C** are included
- **AND** unrelated baseline note **D** is not resolved

### Requirement: Constrained geometry resolution algorithm

**`resolveConstrainedGeometry`** SHALL implement **constrained geometry resolution** per target using **`EditSessionInteraction`** rows from one **`TargetNoteInteractionGroup`** — **without** a separate **Constraint** type.

For baseline **B** and incoming interactions **I[]**:

1. If any **I** has **`InteractionType`** **OverlapNoteOn** or **CompleteCover** → **`visible = false`** (**complete hide precedence**).
2. Else if any **OverlapNoteOff** in **I** → **`visible = true`**, **`startTick = B.startTick`**, **`endTick = min(computeShortenedEndTick(i))`** over **OverlapNoteOff** rows (**restrictive shorten combine**).
3. Else if **I** non-empty and every row is **BoundaryTouch** → baseline span unchanged (**boundary unchanged**).
4. Else → baseline-equivalent span (**baseline equivalent**).
5. If **`noteMinLengthRemoveEnabled`** and **`visible = true`** and **`endTick − startTick < noteMinLengthTicks`** → **`visible = false`** (**minimum note edit length hide**). If **`noteMinLengthRemoveEnabled`** is **false**, skip step 5.
6. **`pitch = B.pitch`**.

**Runtime configuration:** **`noteMinLengthTicks`** and **`noteMinLengthRemoveEnabled`** ([`Globals.h`](../../../include/Globals.h)) — same user-facing **NoteMinLength** settings as capture hot stop (Q16). **`resolveConstrainedGeometry`** SHALL receive both as parameters (wired from globals at geometry entry).

**`computeShortenedEndTick(i)`** SHALL return the linear **note-off tick** for one **OverlapNoteOff** interaction from **`causingSpan`**, **`baselineSpan`**, and **`wraps`** (typically **`causingSpan.startTick − 1`** for tail trim). It SHALL **not** return note length. **`BoundaryTouch`** SHALL NOT change ticks in resolve; **boundary split** runs in **edit session action apply** (D10).

#### Scenario: Resolution algorithm matches combine precedence

- **GIVEN** target **A** has incoming **OverlapNoteOn** from **B** and **OverlapNoteOff** from **C** this tick
- **WHEN** **`resolveConstrainedGeometry(A)`** runs
- **THEN** **`visible = false`**
- **AND** **restrictive shorten combine** from **C** is not applied

#### Scenario: Combined shortens below minimum length become hide

- **GIVEN** target **A** has two **OverlapNoteOff** interactions whose combined **`min(computeShortenedEndTick)`** yields span below **`noteMinLengthTicks`**
- **AND** **`noteMinLengthRemoveEnabled`** is **true**
- **WHEN** **`resolveConstrainedGeometry(A)`** runs
- **THEN** **`visible = false`**

#### Scenario: Overlap restore candidate without incoming interactions

- **GIVEN** causing note **B** moved away so **(B, A)** is omitted from analyze
- **AND** **live store** still hides **A** but **transaction baseline** has **A**
- **WHEN** **constrained geometry target notes** are computed
- **THEN** **A** is an **overlap restore candidate note**
- **AND** **`resolveConstrainedGeometry(A)`** with empty incoming interactions yields baseline-equivalent geometry
- **AND** edit session action builder may emit **RestoreNote** if live store still differs

#### Scenario: Regroup interactions by target each tick

- **WHEN** the user moves causing note **B** one fader step
- **THEN** the pipeline rebuilds the full **`EditSessionInteractionsByTarget`** from analyze output
- **AND** does not call **`updateConstraintRegistry`** or read prior-tick grouping state

#### Scenario: One InteractionType per pair per tick

- **WHEN** `analyzeEditSessionInteractions` evaluates causing note **B** against target **A**
- **THEN** exactly one primary **`InteractionType`** is produced for **(B, A)**
- **AND** wrap-relative trim is **`wraps`** on **`EditSessionInteraction`**, not a second interaction type

#### Scenario: Order-independent multi-causing combine

- **GIVEN** causing notes **B** and **C** both constrain target **A** on this tick
- **WHEN** **`resolveConstrainedGeometry(A)`** runs on the regrouped interactions for **A**
- **THEN** the result follows **combine precedence** (complete hide → restrictive shorten → minimum note edit length hide)
- **AND** on the next tick with **B** gone, analyze omits **(B, A)** and the rebuilt grouping reflects **{C}** only — no explicit removal step

#### Scenario: Resolver is policy-free

- **WHEN** **`resolveConstrainedGeometry`** runs
- **THEN** output is **`ConstrainedNoteGeometry`** (`visible`, linear ticks, pitch) only
- **AND** the resolver does not emit **`HideNote`**, **`RestoreNote`**, or other action types

#### Scenario: Edit session action builder omits unchanged actions

- **GIVEN** **`ConstrainedNoteGeometry(A)`** matches **live store** for **A**
- **WHEN** **`buildEditSessionActions`** runs
- **THEN** no **HideNote**, **ShortenNote**, or **RestoreNote** is emitted for **A**

#### Scenario: Edit session action builder emits only store-changing actions

- **GIVEN** **`ConstrainedNoteGeometry(A)`** has **`visible = false`** and **live store** still contains **A**'s pair
- **WHEN** **`buildEditSessionActions`** runs
- **THEN** **HideNote** is emitted for **A**
- **AND** if **A** is already absent from live store, **HideNote** is **not** emitted

#### Scenario: Edit session action builder maps geometry to actions

- **WHEN** **`ConstrainedNoteGeometry.visible`** is false, baseline had the note, and live store still has the pair
- **THEN** **`buildEditSessionActions`** emits **HideNote**
- **AND** that mapping lives in the edit session action builder, not the resolver
- **AND** the edit session action builder does not read **`EditSessionInteraction`** structs

#### Scenario: Fresh evaluation per geometry tick

- **WHEN** the user moves the selected note one fader step
- **THEN** the pipeline recomputes from transaction baseline + new geometry only
- **AND** does not depend on **`overlapNotes`** or prior-tick constraint storage

#### Scenario: Analysis does not mutate

- **WHEN** `analyzeEditSessionInteractions` runs
- **THEN** session event count and ticks are unchanged

#### Scenario: Edit session action apply ordering

- **WHEN** `buildEditSessionActions` emits restore, shorten, hide, and move actions
- **THEN** `applyEditSessionActions` applies them in deterministic order: **RestoreNote** → **ShortenNote** → **HideNote** → causing note geometry actions
- **AND** **boundary split** runs after those actions within **`applyEditSessionActions`**

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

### Requirement: Boundary split ownership

**Boundary split** (D10 — off-at-on−1 and same-tick on/off adjustment) SHALL be **part of edit session action apply** — the final live-store geometry sub-step inside **`applyEditSessionActions`**.

**Boundary split** SHALL **not** be **`normalizeWindow`** (DEC-014 edit-closure canonicalization).

#### Scenario: Boundary split is not normalization

- **WHEN** a geometry tick completes **`applyEditSessionActions`**
- **THEN** **boundary split** has already adjusted abutting **BoundaryTouch** pairs in live store
- **AND** **`normalizeWindow`** runs only on edit closure, not each geometry tick

### Requirement: Invariant — Constrained Geometry Authority

**`ConstrainedNoteGeometry`** SHALL be the sole description of desired overlap-target geometry during a geometry update tick.

**`buildEditSessionActions`** SHALL derive overlap-target actions from **constrained geometry**, **transaction baseline**, and current note state; causing-note actions from **edited geometry** and current note state.

**`buildEditSessionActions`** SHALL **omit actions that would not change current state** for that **`NoteId`**.

**`buildEditSessionActions`** MUST NOT inspect **`EditSessionInteraction`** or **`EditSessionInteractionsByTarget`** directly.

#### Scenario: Builder consumes constrained geometry, not interactions

- **WHEN** overlap-target desired geometry is available as **`ConstrainedNoteGeometry`**
- **THEN** **`buildEditSessionActions`** maps that geometry to current-state-changing actions
- **AND** it does not inspect **`EditSessionInteraction`** rows

### Requirement: EditSessionAction types (NOTE_EDIT first implementation)

NOTE_EDIT **`EditSessionActionType`** values SHALL include at minimum:

| Type | Effect on live store |
|------|-------------------------|
| **RestoreNote** | Reinsert or extend baseline span for **`NoteId`** |
| **ShortenNote** | Set paired **NoteOff** to shortened linear tick |
| **HideNote** | Remove paired on/off; baseline retained in transaction baseline |
| **MoveNote** | Move causing **`NoteId`** on/off by edited linear span |
| **ChangeLength** | Update causing note off tick from edited end |
| **ChangePitch** | Update causing note pitch; ticks unchanged |

Committed **`EditPass`** rows at note edit pass commit are unchanged; **`EditSessionAction`** is live RAM only until **`commitAllPendingNoteEditActions`**.

#### Scenario: EditSessionAction remains live RAM only

- **WHEN** a NOTE_EDIT geometry tick emits **`EditSessionAction`** rows
- **THEN** they mutate the active **live store** only through **`applyEditSessionActions`**
- **AND** persisted **`EditPass`** rows are produced later by macro commit

### Requirement: EditorSelection and baseline as action inputs

**`buildEditSessionActions`** and **`applyEditSessionActions`** SHALL resolve targets by **`NoteId`** from **`EditorSelection`** and **`baselineMap`**. They MUST NOT use **`selectedNoteIdx`** or display wrap **`DisplayNote.endTick`** as storage mutation authority.

#### Scenario: Storage mutation targets NoteId

- **WHEN** a selected display index changes after projection
- **THEN** storage mutation still targets the selected **`NoteId`**
- **AND** display index is not used as mutation authority

### Requirement: EditorSelection selection domain (NOTE_EDIT)

During any NOTE_EDIT geometry update (**Move**, **Length**, **Pitch**, **Add**, **Delete**), the **orchestrator** (not analyze) SHALL filter **eligible overlap pairs**:

**causing note ∈ `EditorSelection.selectedNotes`** and **target note ∉ `EditorSelection.selectedNotes`**.

**Selected-to-selected overlap pairs** (both causing and target selected) SHALL be skipped in this change.

**Selected-to-selected overlap when causing note geometry changed** is **deferred** — reserved for future multi-select length editing; not implemented or tested in this change.

**`EditSessionInteraction`** SHALL carry **`causingNoteId`** and **`targetNoteId`** only. Selection membership SHALL be derived via **`isSelectedNote(id, EditorSelection)`** at orchestrator time — not stored on the interaction struct.

#### Scenario: Selection derived from NoteIds

- **GIVEN** an emitted **`EditSessionInteraction`** with **causingNoteId = B** and **targetNoteId = C**
- **WHEN** eligible overlap pair membership is evaluated
- **THEN** **`isSelectedNote(B, EditorSelection)`** is true
- **AND** whether **C** is selected is determined by **`isSelectedNote(C, EditorSelection)`** without reading extra fields on the interaction struct

#### Scenario: No redundant selection fields on interaction

- **WHEN** **`EditSessionInteraction`** is defined for this change
- **THEN** it does not include **`causingSelected`**, **`targetSelected`**, or a duplicate geometry-changing causing **`NoteId`**
- **AND** **`wraps`** remains the only non-id attribute besides **`InteractionType`**

### Requirement: Edit driver boundary

Transaction baseline (**`baselineMap`**) SHALL refresh when **`EditorSelection.primaryNote`** changes or a new edit gesture starts on a note — not only on **NoteEditKind** transition. Within one edit driver, pitch change on the same primary re-analyzes interactions; prior overlap targets restore via rebuild.

#### Scenario: New primary driver refreshes baseline

- **GIVEN** the user was moving note **A**
- **WHEN** the user selects note **B** as **`primaryNote`**
- **THEN** transaction baseline refreshes at the new **edit driver boundary**

#### Scenario: Pitch change same primary re-analyzes

- **GIVEN** the user is editing note **B** as **primaryNote**
- **WHEN** pitch changes without changing **primaryNote**
- **THEN** transaction baseline is unchanged for the edit driver
- **AND** analyze rebuilds interactions from current geometry

### Requirement: Macro commit one noteEditPass batch

At **`commitAllPendingNoteEditActions`**, the system SHALL produce **one `noteEditPass` batch** containing **`EditPass` rows for every `NoteId` changed** compared to transaction baseline — mover, overlap targets, add, delete.

#### Scenario: Macro commit batches changed notes

- **WHEN** macro commit runs after one mover edit and one hidden overlap target
- **THEN** one **`noteEditPass`** batch is created
- **AND** the batch contains rows for both changed **`NoteId`** values

### Requirement: overlapNotes and persistent registry removed

**`overlapNotes`** and any persistent **`ConstraintRegistry`** MUST NOT be authoritative after phase 4 wire.

#### Scenario: Restore without scratch

- **WHEN** rebuilt constraints yield baseline-equivalent **`ConstrainedNoteGeometry`** but live store still hides the note
- **THEN** **RestoreNote** is emitted by the edit session action builder
- **AND** restore does not require **`overlapNotes`** or reverse-restore ordering

### Requirement: Cross-session extension shape

The pipeline entry points SHALL accept **`EditSessionType`** so Loop and ControlChange edits can extend interaction kinds and action types without renaming **`EditSessionAction`**.

#### Scenario: Note implementation keeps extension type

- **WHEN** NOTE_EDIT calls the pipeline entry points
- **THEN** the API carries **`EditSessionType::Note`**
- **AND** future Loop or ControlChange extensions do not require renaming **`EditSessionAction`**

### Requirement: Overdub reuses constrained geometry decisions

When overdub overlap resolution evaluates an incoming note against immutable source-pass geometry, the system SHALL obtain shorten / hide / min-length outcomes from the same constrained-geometry decision rules used by NOTE_EDIT (`resolveConstrainedGeometry` semantics and shared `noteMinLengthTicks` / `noteMinLengthRemoveEnabled` globals). The system MUST NOT maintain a second, contradictory capture-only overlap policy for those decisions. Encoding of resulting actions onto a pending `overdubPass` MAY use a different apply owner than `applyEditSessionActions` on `NoteEditSession.store`, provided the decided geometry outcomes match.

#### Scenario: Shared min-length hide decision

- **GIVEN** constrained geometry would hide a target note under NOTE_EDIT because residual length is below `noteMinLengthTicks` with remove enabled
- **WHEN** overdub overlap resolution evaluates an equivalent causing/target pair
- **THEN** overdub decides hide/remove for that target as well
- **AND** the decision is not overridden by capture-store duplicate detection alone

