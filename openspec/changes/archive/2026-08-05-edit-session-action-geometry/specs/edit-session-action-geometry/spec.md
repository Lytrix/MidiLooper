## ADDED Requirements

### Requirement: EditSessionAction geometry pipeline

Live geometry edits (`EditSessionType::Note` first; Loop and ControlChange later) SHALL use a declarative pipeline:

1. **Transaction baseline** — immutable **`baselineMap`** per **edit driver**; full loop in this change
2. **Edited geometry** — **`EditorSelection`** plus one **linear `NoteBaseline` causing span** per selected note this tick (`focus.last` for **`primaryNote`**; see design § Edited geometry)
3. **Edit projection** (D20) — **`buildEditProjectionContext`** + **`projectEditIntervalsForAnalysis`** produce linear spans before analysis
4. **Edit session geometry orchestrator** — determine **changed causing notes** and **eligible overlap pairs**; MUST NOT delegate selection or latch logic to analyze
5. **`analyzeEditSessionInteractions`** — pure geometry facts for supplied causing notes; **positive interaction graph** only (omit non-overlapping pairs; no **None** enum)
6. **`groupEditSessionInteractionsByTarget`** — group interactions **per target `NoteId`**; ephemeral; MUST NOT persist across ticks
7. **`resolveConstrainedGeometry`** — for each **constrained geometry target note** (see Resolver scope): baseline + per-target interactions → **`ConstrainedNoteGeometry`**; MUST NOT know **`EditSessionActionType`**
8. **`buildEditSessionActions`** — **edit session action builder**: inputs **`constrainedGeometry`**, **`editedGeometry`**, **`transactionBaseline`**, **`liveStore`**; compute minimal ordered **`EditSessionActions`**; **omit actions that would not change live store**; MUST NOT mutate live store; MUST NOT inspect **`EditSessionInteraction`** directly
9. **`applyEditSessionActions`** — **edit session action apply**; the ONLY subsystem that mutates **live store** for live geometry (includes **boundary split** sub-step, D10); **`normalizeWindow`** is separate (edit closure only)
10. **`normalizeWindow`** on edit closure — geometry tick path boundary (existing DEC-014)
11. **Read-only projection** — MUST NOT write back to storage

The system MUST NOT use a restore-first prelude or persistent overlap scratch as the primary model. The system MUST NOT maintain a persistent **`ConstraintRegistry`** between geometry ticks. The system MUST NOT reintroduce **`ResolutionPolicy`** as a separate pipeline stage.

#### Scenario: Geometry pipeline mutates only during apply

- **WHEN** a NOTE_EDIT geometry update runs
- **THEN** analysis, grouping, resolution, and action building do not mutate **live store**
- **AND** **`applyEditSessionActions`** performs the storage mutation

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

**Live store** (synonyms: **session store**, **current session store**) SHALL mean the in-RAM MIDI event buffer for the active NOTE_EDIT session — note on/off pairs as they exist at the current moment, before this tick’s **`applyEditSessionActions`**.

- During NOTE_EDIT, live store SHALL be **`EditSession.store`** / **`EditManager::sessionMidiEvents()`**.
- Analyze, group-by-target, and resolve stages MUST NOT mutate live store.
- **`applyEditSessionActions`** SHALL be the only subsystem that mutates live store for live geometry.
- **`buildEditSessionActions`** SHALL compare **constrained geometry** and **transaction baseline** against **live store** to emit actions.
- Live store MUST NOT be confused with transaction baseline, edited geometry, committed loop passes, or **`overlapNotes`**.

#### Scenario: Live store unchanged until apply

- **WHEN** analyze, interaction grouping, constrained geometry resolution, and edit session action builder run
- **THEN** live store event count and ticks are unchanged
- **AND** live store updates only when **`applyEditSessionActions`** runs

#### Scenario: Edit session action builder reads live store for restore

- **GIVEN** **`ConstrainedNoteGeometry(A)`** matches baseline but live store still hides **A**
- **WHEN** **`buildEditSessionActions`** runs
- **THEN** **RestoreNote** is emitted for **A**
- **AND** the comparison uses live store pairs, not **`overlapNotes`**

#### Scenario: Note edit pass commit compares baseline to final live store

- **WHEN** **`commitAllPendingNoteEditActions`** runs
- **THEN** **`EditPass`** rows are derived from **transaction baseline compared to final live store**
- **AND** not from **`overlapNotes`**

#### Scenario: Apply-owned rows are diagnostic only

- **GIVEN** **`applyOwnedEditPassRows`** exist during NOTE_EDIT migration
- **WHEN** **`commitAllPendingNoteEditActions`** runs
- **THEN** committed **`EditPass`** rows are derived from **transaction baseline compared to final live store**
- **AND** **`applyOwnedEditPassRows`** MAY be compared for diagnostics / parity logging
- **AND** **`applyOwnedEditPassRows`** MUST NOT change the committed **`EditPass`** output

#### Scenario: Analyzer independent of edit-session state

- **WHEN** `analyzeEditSessionInteractions` runs
- **THEN** it receives only causing-note inputs and geometry spans passed by the orchestrator
- **AND** it does not read encoder latches, **`EditorSelection`** emit policy, or prior-frame session state

#### Scenario: Orchestrator determines changed causing notes

- **GIVEN** selected note **B**'s span unchanged this tick (latch matches current geometry)
- **WHEN** the geometry update pipeline runs
- **THEN** **B** is not passed as a causing input to analyze unless another orchestrator rule applies (e.g. **Add**)
- **AND** **`geometryChangedThisTick`** is evaluated only in the orchestrator, not inside analyze

#### Scenario: Pitch change re-evaluates source lane

- **GIVEN** causing note **B** hid/shortened notes on pitch **P0** earlier under the same edit driver
- **WHEN** **B** changes pitch to **P1**
- **THEN** analyze re-evaluates targets on **P1** (destination) and **P0** (source lane vacated)
- **AND** former targets on **P0** may receive **RestoreNote** when interactions rebuild empty

#### Scenario: Add uses move interaction logic

- **GIVEN** the user adds a selected note overlapping an existing note on the same pitch
- **WHEN** analyze runs for the new causing note
- **THEN** the same **InteractionType** rules apply as for **Move** (**OverlapNoteOn** / **OverlapNoteOff** / **CompleteCover**)

#### Scenario: Same-tick note-on and note-off

- **GIVEN** causing **note-on** and target **note-off** share the same linear tick
- **WHEN** **edit session action apply** runs **boundary split**
- **THEN** **note-on** remains at that tick
- **AND** target **note-off** moves to **`onTick − 1`**

#### Scenario: Geometry parity classify fixtures

- **WHEN** native parity fixtures run (internal swallow, head-on overlap, tail overlap, external cover, no overlap)
- **THEN** **`InteractionType`** matches the locked classify order after **Edit projection** (D20)

#### Scenario: Selected set is one editing domain

- **GIVEN** notes **B** and **D** are both in **`EditorSelection.selectedNotes`**
- **WHEN** the orchestrator builds analyze inputs
- **THEN** **(B, D)** and **(D, B)** are not passed to analyze in this change

#### Scenario: Add note triggers overlap

- **GIVEN** the user adds a note on an existing note's span
- **WHEN** the new note is selected and passed as causing input
- **THEN** an **`EditSessionInteraction`** is produced with the new note as **causingNoteId**
- **AND** the existing note is evaluated as target

#### Scenario: Delete causing note restores hidden target

- **GIVEN** selected note **B** caused **A** to be hidden
- **WHEN** **B** is deleted and analyze rebuilds without **(B, A)**
- **THEN** **`resolveConstrainedGeometry(A)`** matches baseline
- **AND** **RestoreNote** is emitted by the edit session action builder when live store still hides **A**

#### Scenario: Restore is consequence not imperative

- **GIVEN** no active constraints remain for target **A**
- **WHEN** **`resolveConstrainedGeometry(A)`** equals baseline but live store still reflects hide/shorten
- **THEN** the edit session action builder emits **RestoreNote**
- **AND** no restore history or reverse-restore chain is consulted

#### Scenario: Wrapped target before analyze

- **GIVEN** target **A** is a wrapped display note
- **WHEN** analyze runs
- **THEN** **Edit projection** has produced linear **`baselineSpan`** for **A** before **`InteractionType`** is classified

#### Scenario: Causing selected, target not selected

- **GIVEN** **B** ∈ **`selectedNotes`** and **A** ∉ **`selectedNotes`**
- **WHEN** edited geometry for **B** overlaps **A**
- **THEN** an **`EditSessionInteraction`** is produced for **(B, A)** as usual

#### Scenario: Selected-to-selected overlap pairs skipped in this change

- **GIVEN** notes **B** and **C** are both selected on the same pitch
- **WHEN** a **Length** edit on **B** overlaps **C**
- **THEN** **(B, C)** is **not** passed to analyze in this change
- **AND** **selected-to-selected overlap when geometry changed** is **deferred** until future multi-select length editing

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

**`buildEditSessionActions`** SHALL accept:

```cpp
EditSessionActions buildEditSessionActions(
    span<const ConstrainedNoteGeometry> constrainedGeometry,
    const EditedGeometry& editedGeometry,
    const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore);
```

| Input | Authority |
|-------|-----------|
| **`constrainedGeometry`** | Desired overlap-target geometry from resolve |
| **`editedGeometry`** | User intent this tick (causing spans) |
| **`transactionBaseline`** | Original note geometry for this edit driver |
| **`liveStore`** | Current session RAM state |

The builder SHALL **observe** these inputs only (plus types above); it SHALL **compute** **`EditSessionActions`**; it SHALL **not mutate** live store.

#### Scenario: Builder observes inputs without mutation

- **WHEN** **`buildEditSessionActions`** receives constrained geometry, edited geometry, transaction baseline, and live store
- **THEN** it emits **`EditSessionActions`**
- **AND** live store remains unchanged until **`applyEditSessionActions`**

### Requirement: Boundary split ownership

**Boundary split** (D10 — off-at-on−1 and same-tick on/off adjustment) SHALL be **part of edit session action apply** — the final live-store geometry sub-step inside **`applyEditSessionActions`**.

**Boundary split** SHALL **not** be **`normalizeWindow`** (DEC-014 edit-closure canonicalization).

#### Scenario: Boundary split is not normalization

- **WHEN** a geometry tick completes **`applyEditSessionActions`**
- **THEN** **boundary split** has already adjusted abutting **BoundaryTouch** pairs in live store
- **AND** **`normalizeWindow`** runs only on edit closure, not each geometry tick

### Requirement: Invariant — Constrained Geometry Authority

**`ConstrainedNoteGeometry`** SHALL be the sole description of desired overlap-target geometry during a geometry update tick.

**`buildEditSessionActions`** SHALL derive overlap-target actions from **constrained geometry**, **transaction baseline**, and **live store**; causing-note actions from **edited geometry** and **live store**.

**`buildEditSessionActions`** SHALL **omit actions that would not change live store** for that **`NoteId`**.

**`buildEditSessionActions`** MUST NOT inspect **`EditSessionInteraction`** or **`EditSessionInteractionsByTarget`** directly.

#### Scenario: Builder consumes constrained geometry, not interactions

- **WHEN** overlap-target desired geometry is available as **`ConstrainedNoteGeometry`**
- **THEN** **`buildEditSessionActions`** maps that geometry to store-changing actions
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
