## Context

Today NOTE_EDIT geometry mutates storage through intertwined helpers in [`NoteMovementUtils.cpp`](../../../src/Utils/NoteMovementUtils.cpp):

```
moveNoteWithOverlapHandling
  → restoreOverlapNotesNoLongerOverlapping   (mutate)
  → findOverlaps                             (decide)
  → applyShortenOrDelete                     (mutate)
  → move note events                         (mutate)
  → finalReconstructAndSelect
```

**`overlapNotes`** on [`NoteEditFocus`](../../../include/NoteEditFocus.h) records hidden/shortened overlap state between fader ticks. Restoration depends on that scratch, so every geometry update is coupled to the previous frame.

**Identity** is migrating to **`EditorSelection`** (`primaryNote`, `bracketTick`) and **`NoteId`** (DEC stable-note-id). Geometry still mixes display ticks, LIFO pairing, and `% loopLength` in places.

**Canonical storage** work (`linear-loop-tick-storage`, DEC-013/014) defines invariants and normalize boundaries; this change defines **how live geometry edits produce canonical session-store state** without imperative restore chains.

**Design authority:** User architecture proposal (2026-07-04) — transaction baseline + declarative **`EditSessionActions`**.

## Goals / Non-Goals

**Goals:**

- Separate **analysis** (pure), **action building** (pure), **edit session action apply** from **read-only projection** (read-only)
- Every fader/encoder geometry tick: **baseline + edited geometry → `EditSessionActions` → apply**
- **`EditorSelection`** + **`NoteId`** as action targets — not `selectedNoteIdx` or display wrap segments
- Native-testable phases without `Track` / Arduino
- Extension shape for **`EditSessionType::Loop`** and **`ControlChange`** later

**Non-Goals:**

- Implement loop/CC action variants in phase 1
- Replace **`EditPass`** rows or **`EditActionType`** (Create/Update/Delete on committed passes)
- New `*Manager` classes (DEC-008 pattern: extend existing owners)
- Eliminate **`NoteEditFocus`** — baseline and mover state remain there initially

## Philosophy (derived geometry, not overlap state)

The pipeline answers **one question** per geometry update:

> Given the **immutable transaction baseline** and **current edited geometry**, what should **live store** look like right now?

**Model only three kinds of truth:**

1. **Transaction baseline** — immutable reference geometry per **edit driver**
2. **Interaction graph** — objective geometry facts (`EditSessionInteraction`, positive graph only)
3. **`ConstrainedNoteGeometry`** — resolved desired geometry for **overlap target** notes

Everything else is **derived** each tick — including **`visible`**, **`EditSessionActions`**, and restore. There is no persistent **`overlapNotes`**, no **`HiddenBecauseOf…`** state machines, and no **`ConstraintRegistry`**.

The central algorithm is **`resolveConstrainedGeometry`**: combine multiple incoming **`EditSessionInteraction`** rows for one target into one **`ConstrainedNoteGeometry`**. That is harder than emitting constraints; it is where overlap policy lives. **`buildEditSessionActions`** and **`applyEditSessionActions`** bridge derived geometry to live store — they do not re-interpret interactions.

## Terminology (locked)

| Term | Meaning | Not |
|------|---------|-----|
| **`EditSessionAction`** | One deterministic step **`applyEditSessionActions`** applies to session canonical storage this tick | **`EditPass`**, **`EditChange`** (committed pass row) |
| **`EditSessionActions`** | Ordered list produced by the builder | “Plan”, blueprint, program |
| **`EditSessionInteraction`** | One **(causingNoteId → targetNoteId)** fact from analyze | Second interaction for same pair |
| **`EditSessionInteractionsByTarget`** | Ephemeral output of **`groupEditSessionInteractionsByTarget`** — rebuilt every geometry tick | Persistent **`ConstraintRegistry`**, **`overlapNotes`**, **CurrentSet** / **SavedSet** |
| **`ConstrainedNoteGeometry`** | Policy-free output of **constrained geometry resolution** (`visible`, linear ticks) | **`EditSessionAction`** semantics in resolver |
| **`resolveConstrainedGeometry`** | **Constrained geometry resolution** — baseline + interactions grouped by target → **`ConstrainedNoteGeometry`** per target | Prior store frame as authority; separate **Constraint** type |
| **Transaction baseline** | **`baselineMap`** + **`commitBaseline`** at **edit driver boundary** (D19); v1 **full loop** | Live **`focus.last`** |
| **Edit driver** | **`EditorSelection.primaryNote`**; baseline refresh when driver changes | Strict **NoteEditKind** boundary only |
| **Edited geometry** | User-intended **linear spans** for **selected notes** this tick + **`EditorSelection`** (see § Edited geometry) | Transaction baseline, live store, prior latch, overlap targets |
| **Live store** | In-RAM MIDI pairs for the active NOTE_EDIT session **now** (see § Live store) | Transaction baseline, committed loop passes, **`overlapNotes`** |
| **`buildEditSessionActions`** | **Observe** constrained geometry + baseline + live store (+ **edited geometry** for causing notes); **compute** **`EditSessionActions`** | **`ResolutionPolicy`**, direct **`EditSessionInteraction`** inspect |
| **`applyEditSessionActions`** | **Edit session action apply** — only subsystem that mutates **live store** for geometry (includes boundary split sub-step, D10) | `findOverlaps`, `restoreOverlap*` |
| **Positive interaction graph** | Analyze output: **`EditSessionInteraction`** rows only for overlapping **(causing, target)** pairs this tick | **`InteractionType::None`**; omitted pair = no overlap |
| **Prior latch** | Previous tick’s causing span per note — orchestrator input for **`geometryChangedThisTick`** only | Edited geometry; analyze input |
| **Interaction target note** | **Target `NoteId`** with ≥1 incoming **`EditSessionInteraction`** this tick | Causing note |
| **Overlap restore candidate note** | **Target `NoteId`** with **transaction baseline** entry where **live store** differs from baseline due to **current edit driver** side-effects | Every loop note |
| **Constrained geometry target notes** | **Interaction target notes** ∪ **overlap restore candidate notes** — scope of **`resolveConstrainedGeometry`** | All notes in loop |
| **Restrictive shorten combine** | Among **OverlapNoteOff** rows: **`endTick = min(computeShortenedEndTick(i))`** — earliest linear **note-off** wins (not a length limit) | **`InteractionType::None`** |
| **`computeShortenedEndTick(i)`** | Per **OverlapNoteOff** helper: linear **note-off tick** for target from **`causingSpan`**, **`baselineSpan`**, **`wraps`** (typically **`causingSpan.startTick − 1`**) | Note length; use **`endTick − startTick`** + **minimum note edit length hide** separately |
| **Minimum note edit length hide** | D16 step 5: when **`noteMinLengthRemoveEnabled`**, span **`endTick − startTick < noteMinLengthTicks`** → **`visible = false`** | Hardcoded **`TICKS_PER_16TH_STEP / 2`**; capture-only floor |
| **Eligible overlap pair** | **(causing note, target note)** where causing ∈ **`selectedNotes`**, target ∉ **`selectedNotes`** — orchestrator filter before analyze (D17) | **Selected-to-selected overlap pairs** (skipped this change) |
| **`CausingTargetPair`** | Orchestrator struct: **`causingNoteId`** + **`targetNoteId`** for one eligible pair | **`EditSessionInteraction`** (post-analyze) |

### Edited geometry (definition)

**Edited geometry** is the user-intended note geometry for **one geometry update tick** — after fader/encoder input is applied to scratch fields, **before** `applyEditSessionActions` mutates **live store**.

It answers: *“Where do the selected notes sit right now, according to the user?”*

**Comprises two parts:**

| Part | Source (NOTE_EDIT v1) | Role |
|------|----------------------|------|
| **Selection** | **`EditorSelection`** — `selectedNotes`, **`primaryNote`**, `bracketTick`, track/loop scope | Which notes are in the editing domain; who is the active driver |
| **Causing spans** | One **linear** **`NoteBaseline`** per **`NoteId`** in `selectedNotes` — pitch, velocity, `startTick`, `endTick` | Spans the user is driving this tick; fed through **Edit projection** (D20) then analyze |

**Causing span authority per note:**

| Note role | Span source this tick |
|-----------|----------------------|
| **`primaryNote`** (active driver) | **`NoteEditFocus.last`** — live fader/encoder values |
| Other **`selectedNotes`** | Linear span from **live store** at tick entry (v1: not overlap targets against other selected notes) |
| **Add** (new note) | Span from create gesture (selected before store has committed pair) |
| **Delete** | Causing note absent from selection; no span in edited geometry |

**Prior latch** (previous tick’s causing span per note) is **orchestrator input only** — used by **`geometryChangedThisTick`** to decide which causing notes changed. It is **not** part of edited geometry and is **not** passed to analyze.

**Explicitly not edited geometry:**

| Concept | Why separate |
|---------|--------------|
| **Transaction baseline** (`baselineMap`, `commitBaseline`) | Immutable reference for the current **edit driver** — “what was true when editing started on this note” |
| **Live store** | Current MIDI pairs in session RAM — may still reflect hide/shorten until **edit session action apply** runs (see § Live store) |
| **Overlap targets** (non-selected notes) | User did not move them; desired state comes from **`ConstrainedNoteGeometry`**, not from edited geometry |
| **Prior latch** | Frame-to-frame delta detection — orchestrator only (D17) |

**Pipeline use:** Orchestrator builds edited geometry → **Edit projection** (D20) → analyze compares causing spans against baseline + candidate targets in the analysis window (v1: full loop). Edited geometry does **not** include overlap side-effects (hide/shorten/restore) — those emerge from the pipeline.

```cpp
struct EditedNoteSpan {
  NoteId noteId;
  NoteBaseline span;  // linear ticks; post-Edit-projection for analyze input
};

struct EditedGeometry {
  EditorSelection selection;
  // One entry per selected note with a causing span this tick
  std::vector<EditedNoteSpan> causingSpans;
};
```

**Contrast with transaction baseline:** Baseline is frozen at **edit driver boundary**. Edited geometry updates every fader/encoder tick while the same **`primaryNote`** remains active.

### Live store (definition)

**Live store** (synonyms: **session store**, **current session store**) is the **in-RAM MIDI event buffer for the active NOTE_EDIT session** — note on/off pairs as they exist **at this moment**, before **`applyEditSessionActions`** applies this tick’s actions.

It answers: *“What MIDI pairs are actually in the edit session right now?”*

**Implementation (NOTE_EDIT v1):**

| Access | Role |
|--------|------|
| **`EditSession.store`** ([`CowLoopEventStore`](../../../include/EditSession.h)) | Canonical session RAM owner on **`EditSession`** |
| **`EditManager::sessionMidiEvents()`** | Mutable **`MidiEventVec`** during note edit; playback/display read path uses this (via materialize/projection), not committed loop passes |

**Why “live”:** Mutable working copy while editing. Each geometry tick may change it (hide, shorten, move, restore). It reflects **all prior applies** in this edit session, not user intent for the current tick alone.

**Pipeline role:**

| Stage | Relationship to live store |
|-------|---------------------------|
| Analyze / group / resolve | **Read-only** — do not mutate |
| **`buildEditSessionActions`** | **Observe** constrained geometry, baseline, live store, edited geometry; **compute** **`EditSessionActions`** |
| **`applyEditSessionActions`** | **Edit session action apply** — sole **live store** writer for geometry during edit |
| Projection | **Read-only** — display/faders from live store (no write-back) |
| Macro **`commitAllPendingNoteEditActions`** | **Transaction baseline** compared to **final live store** → **`EditPass`** rows; then normalize and publish to loop passes |

**Explicitly not live store:**

| Concept | Why separate |
|---------|--------------|
| **Transaction baseline** | Frozen at **edit driver boundary** — restore reference, not current RAM |
| **Edited geometry** | User intent this tick (`focus.last`) — may differ from live store until apply |
| **`ConstrainedNoteGeometry`** | Computed desired state — edit session action builder plans **`EditSessionActions`** from this compared to live store |
| **Committed loop** (`LoopPasses`, SD) | Slot storage after macro commit — not the in-edit RAM copy |
| **`overlapNotes`** | Retired scratch — not authoritative |

**Typical mid-tick gap:** User moves causing note **B** (edited geometry updated); overlap target **A** may still be **hidden in live store** while **`ConstrainedNoteGeometry(A)`** already matches baseline — edit session action builder emits **RestoreNote**; **`applyEditSessionActions`** updates live store.

**Contrast with edited geometry:** Edited geometry = what the user is driving. Live store = what RAM actually contains, including side-effects from earlier ticks in this session.

**Playback audition (brownfield, shipped):** During NOTE_EDIT, edited pairs are **not** sent as immediate MIDI note-on per fader gesture. `Track::invalidateCaches()` bumps `sessionPreviewRevision_`; `ensurePlaybackWindowBuilt` uses `sessionMidiEvents()` while transport plays — see `loop-wrap-projection` Tier 2 and [`note_edit_geometry_wrap_regression_bugfix.md`](../../../docs/plans/note_edit_geometry_wrap_regression_bugfix.md). Pipeline **`applyEditSessionActions`** MUST keep calling **`track.invalidateCaches()`** (task 3.6).

### Brownfield interim fixes (pre-pipeline, 2026-07-04)

Shipped outside this change in `NoteMovementUtils` / `NoteEditManager` / `NoteEditFaderOutboundPlan`. **Pipeline wire MUST preserve** — do not reintroduce:

| Fix | Preserve on wire |
|-----|------------------|
| NOTELEN blocked fader 2/3 | `LengthModeEnter`/`Exit` do **not** reset note-select grace or disable `startEditingEnabled` like `SessionOpen`; skip `selectDependentSettle` on length outbound |
| F2–F4 on empty 16th scrub | Dependent motor sync only when `EditorSelection.primaryNote` valid — not empty-step fader 1 / bar-step seek without a note (archived `note-edit-fader-feedback`) |
| Wrong note on length after move | Length edit targets `focus.movingNoteId` / `liveEditDisplayNoteAtSelect`; re-select after move remains HITL best practice until D36 session bracket sync |
| Overlap linear spans | `resolveLinearNoteSpanForOverlap`, per-`noteId` `baselineMap` — interim until Phase 4 retires **`overlapNotes`** |
| HITL smoke | `edit_minimal` preset PASS on base seed (move + length + add/delete); **not** full D14 interaction matrix |

### Authority (pipeline objects)

| Object | Authority |
|--------|-----------|
| **Transaction baseline** | Original note geometry for this **edit driver** (`baselineMap` + `commitBaseline`) |
| **Edited geometry** | User intent this tick (`EditorSelection` + causing spans) |
| **Interaction graph** | Geometry facts only — positive **`EditSessionInteraction`** records from analyze |
| **Interactions grouped by target** | All incoming interactions per **target `NoteId`** (ephemeral; regrouped each tick) |
| **`ConstrainedNoteGeometry`** | Desired geometry for **overlap target** notes after resolve |
| **Live store** | Current session RAM state (`EditSession.store` / `sessionMidiEvents()`) |
| **`EditSessionActions`** | Required mutations this tick (builder output; apply input) |

## Architecture

```
Transaction baseline (full loop v1) ── immutable per edit driver (D19)
        │
Edited geometry (selection + causing spans — see design § Edited geometry)
        │
        ▼
Edit projection (D20)                  ← buildEditProjectionContext + projectEditIntervalsForAnalysis
        │
EditSession orchestrator             ← D17: determine changed causing notes + eligible pairs
        │                              (selection domain; no logic inside analyzer)
        ▼
analyzeEditSessionInteractions()     ← pure geometry facts only (positive interaction graph)
        │
        ▼
groupEditSessionInteractionsByTarget()  ← interaction grouping by target (ephemeral)
        │
        ▼
resolveConstrainedGeometry()         ← constrained geometry resolution (central algorithm; D16)
        │
        ▼
buildEditSessionActions()            ← edit session action builder (see § Live store)
        │
        ▼
applyEditSessionActions()            ← edit session action apply (ordered actions + boundary split, D10)
        │
        ▼
normalizeWindow(closure)             ← edit closure only (DEC-014); not boundary split
        │
        ▼
Projection (read-only)

Macro: commitAllPendingNoteEditActions → normalizeAll → one noteEditPass batch (D18; row per changed NoteId)
```

**Core principle:** Given **transaction baseline + current edited geometry**, compute the correct **live store** state. **No persistent constraint registry**, no restore history, no dependency on the previous edit tick.

### Stage responsibilities (single-direction flow)

| Stage | Responsibility | Mutates store? |
|-------|----------------|----------------|
| **EditSession orchestrator** | Refresh baseline at **edit driver boundary** (D19); determine **changed causing notes** and eligible **(causing, target)** pairs (D17); invoke pipeline | No |
| **Interaction analysis** | Discover **positive geometry facts** only — given causing notes + current geometry | No |
| **Interaction grouping** | Group interactions **per target `NoteId`** | No |
| **Constrained geometry resolution** | **`resolveConstrainedGeometry`** — combine grouped interactions → **`ConstrainedNoteGeometry`** per target | No |
| **Edit session action builder** | Observe constrained geometry, edited geometry, baseline, live store; compute minimal **`EditSessionActions`** | No |
| **Edit session action apply** | Apply **`EditSessionActions`** to **live store**; includes **boundary split** sub-step (D10) | **Yes** |
| **Normalize** | **`LoopTickNormalize`** at boundaries | Yes (canonicalize only) |
| **Projection** | Display, faders, playback read path | **No** write-back |
| **Macro commit** | **`EditManager::commitAllPendingNoteEditActions`** → **`EditPass`** | Yes |

### Ownership (implementation homes)

| Stage | Owner | Mutates store? |
|-------|-------|----------------|
| Transaction baseline | **`NoteEditFocus`** at **edit driver boundary** (D19); v1 full loop | No (immutable until driver changes) |
| Changed causing notes + pair eligibility | **`NoteEditManager`** / geometry entry (orchestrator) | No |
| Edited geometry | **`NoteEditFocus.last`**, **`EditorSelection`** | No (geometry fields only) |
| Live store | **`EditSession.store`** / **`sessionMidiEvents()`** | **Yes** — via **`applyEditSessionActions`** only |
| Interaction grouping | **`groupEditSessionInteractionsByTarget`** (stack/local each tick) | **No** — ephemeral only |
| Constrained geometry resolution | **`resolveConstrainedGeometry`** | **No** |
| Edit session action builder | **`buildEditSessionActions`** | **No** |
| Edit session action apply | **`applyEditSessionActions`** | **Yes** (**live store**) |

## Decisions

### D1 — `EditSessionAction` replaces “plan” and live restore chains

**Decision:** The builder emits **`EditSessionAction`** values; **`applyEditSessionActions`** runs them in order. No separate restore prelude.

**Rationale:** “Given baseline + geometry, desired state now” — **`RestoreNote`** when **`resolveConstrainedGeometry`** matches baseline and store does not (D9, D16).

**Alternative rejected:** Keep `restoreOverlapNotesNoLongerOverlapping` before overlap scan — perpetuates ordering coupling.

**Alternative rejected:** Persistent **`ConstraintRegistry`** updated incrementally — stale-entry risk and second mutable authority.

### D2 — Interaction analysis (pure geometry facts)

**Decision:** **`analyzeEditSessionInteractions`** emits **`EditSessionInteraction`** records only — one **`InteractionType`** per **(causingNoteId, targetNoteId)** per tick. Modifiers (e.g. wrap-relative trim) are **attributes**, not extra interaction types.

```cpp
enum class InteractionType : uint8_t {
  OverlapNoteOn,
  OverlapNoteOff,
  CompleteCover,
  BoundaryTouch,
};

struct EditSessionInteraction {
  InteractionType type;
  bool wraps = false;
  NoteId targetNoteId;
  NoteId causingNoteId;
  NoteBaseline baselineSpan;
  NoteBaseline causingSpan;
};
```

Pairs with **no** overlap are **omitted** from analyze output — no **`InteractionType::None`**.

### Invariant — Positive interaction graph

**Interaction analysis emits only positive geometry facts.** The absence of an interaction is represented by the **absence** of an **`EditSessionInteraction`** record for that **(causingNoteId, targetNoteId)** pair — **not** by an explicit **`InteractionType::None`** sentinel. Downstream restore when a causing note moves away follows from omitted pairs + **overlap restore candidate** resolution, not from a “cleared” interaction row.

**Positive interaction graph:** Analyze output contains **only positive geometry facts**. Absence of a pair in the graph is itself meaningful (e.g. causing note moved away → no **(B, A)** → no incoming interactions for **A** from **B** → restore may emerge via **overlap restore candidate**). Benefits: smaller graph, deterministic analysis, no sentinel **None** objects, simpler tests.

**Classification order** (mutually exclusive per **(causing, target)** pair): **CompleteCover** → **OverlapNoteOn** → **OverlapNoteOff** → **BoundaryTouch**. Set **`wraps = true`** when trim/end math is mover-relative wrap.

**Analysis scope:** All notes in the analysis window (v1: **full loop**). Applies to **Move**, **Length**, **Pitch**, **Add**, and **Delete** edit steps.

**Cross-pitch and pitch-lane scope (D21):** Overlap evaluation is **not** limited to the causing note’s current pitch only.

- **Destination pitch lane:** all notes on the pitch the causing note **moves onto** (move/length/add).
- **Source pitch lane:** on **pitch change**, all notes on the pitch lane the causing note **leaves** that were **mutated earlier under the same edit driver** (hidden/shortened by this causing note) must be re-evaluated for **RestoreNote** via rebuild.
- **Cross-pitch time overlap** (different pitches, overlapping ticks) stays **in scope** for move/length where brownfield requires it; **polyphonic shorten across pitches** is **deferred** (see Q14 decision).

**Add:** treated like **Move** on the same pitch — standard **InteractionType** classify (**OverlapNoteOn** / **OverlapNoteOff** / **CompleteCover**); no special block or invalid-pair allowance.

**Pre-analysis (D20):** **Edit projection** via **`buildEditProjectionContext`** + **`projectEditIntervalsForAnalysis`** ([`IntervalProjection`](../../../include/Utils/IntervalProjection.h)) resolves display/wrap segments to **linear** **`NoteBaseline`** spans for the mover, each candidate target, and **wrapped targets** before **`analyzeEditSessionInteractions`**. Wrap logic is not deferred into classify.

**Analyzer boundary:** **`analyzeEditSessionInteractions`** MUST NOT read encoder latches, edit gestures, or prior-frame session state. See **D17**.

**Retired:** **`movingNoteRange`**, **PartialOverlap**, **Contained**, **WrapCrossing** as **`InteractionType`**, **`ResolutionPolicy`** as a separate pipeline stage (see **D22**).

### D17 — Orchestrator owns selection + changed causing notes; analyzer stays pure

**Decision:** **Selection domain** and **which causing notes changed this tick** are **EditSession orchestrator** responsibilities — not **`analyzeEditSessionInteractions`**.

```
EditSession (orchestrator)
    │
    ├── determine changed causing notes (encoder latch vs current span)
    ├── filter eligible (causing, target) pairs (selection domain)
    │
    ▼
analyzeEditSessionInteractions(changedCausingNotes, editedGeometry, baseline, …)
    └── "Given these causing notes and current geometry, what interactions exist?"
```

The analyzer answers geometry facts only. It MUST NOT call **`geometryChangedThisTick`**, inspect **`EditorSelection`** for emit/skip policy, or depend on edit-session state beyond the **inputs passed in**.

**Orchestrator — selection domain** (during **Move**, **Length**, **Pitch**, **Add**, **Delete**):

1. **Default (this change):** Do not pass **(causing note, target note)** when **both** ids ∈ **`EditorSelection.selectedNotes`** — always skip **selected-to-selected overlap pairs**.
2. **Eligible overlap pair (this change):** causing note selected, target note not selected — only pair shape analyzed in this change.

**Deferred (out of scope — future multi-select length editing):** **Selected-to-selected overlap when causing note geometry changed** — both selected **and** the **geometry-changing causing note** moved/lengthened this tick → analyze **(geometry-changing causing note, overlap target note)** even when the overlap target is selected. Do **not** implement, test, or spec scenarios for this case in this change.

**Changed causing notes:** Orchestrator compares each relevant **`selectedNotes`** span to **prior fader/encoder latch** (not transaction baseline). Only notes with a geometry delta this tick are passed as causing inputs (plus **Add** new note, minus **Delete** removed causing note).

**Add:** New selected note → causing input; existing overlaps → targets.

**Delete:** Removing a causing note → orchestrator omits that id from causing inputs → analyze omits its pairs → **RestoreNote** via edit session action builder (D9).

**Do not store** selection membership on **`EditSessionInteraction`**.

```cpp
// Orchestrator helpers (NOT inside analyzeEditSessionInteractions)
bool isSelectedNote(NoteId id, const EditorSelection& sel);
bool isIntraSelectionPair(NoteId causing, NoteId target, const EditorSelection& sel);
bool geometryChangedThisTick(NoteId causing, const NoteBaseline& priorLatch,
                             const NoteBaseline& currentSpan);

span<const NoteId> determineChangedCausingNotes(...);
span<const CausingTargetPair> determineEligiblePairs(...);
```

### D19 — Edit driver boundary (not strict kind boundary)

**Decision:** **Transaction baseline** refreshes on **edit driver boundary** — when **`EditorSelection.primaryNote`** (active editing driver) changes or a new edit gesture starts on a note — not only on **NoteEditKind** (Move/Length/Pitch) transition.

```
Move Note A  →  encoder ticks  →  same driver (no baseline refresh)
Move Note B  →  new driver     →  new edit driver boundary (baseline refresh)
```

Within one edit driver (same **primaryNote**): regroup interactions by target each tick; **pitch change on same primary** re-analyzes interactions (prior overlap targets restore via rebuild; new same-pitch lane evaluated fresh).

Session undo may still align with kind changes in firmware; **overlap authority** is rebuild-from-baseline, not kind-scoped scratch.

**v1 baseline scope:** **Full loop** (D11 chunk scope deferred).

**Terminology:** Prefer **edit driver boundary** over “edit step boundary” — “step” overloads with macro/micro and **NoteEditKind**.

### D18 — Macro commit: one **`noteEditPass` batch**, row per changed note

**Decision:** **Geometry tick path** runs the full pipeline each geometry update. **Note edit pass commit path** **`commitAllPendingNoteEditActions`** produces **one `noteEditPass` batch** containing **`EditPass` rows for every changed `NoteId`** — mover, overlap targets (hide/shorten/restore), add, delete, lengthen, wrap — derived from **transaction baseline compared to final live store**. **`overlapNotes`** is not the row source.

### D20 — Wrap resolution before analysis

**Decision:** **Edit projection** (**`buildEditProjectionContext`** + **`projectEditIntervalsForAnalysis`**) runs **before** **`analyzeEditSessionInteractions`**. Targets may be **wrapped** display notes; analysis and **`causingSpan`** / **`baselineSpan`** use **linear** ticks only. **`wraps`** on **`EditSessionInteraction`** flags trim math that still depends on mover-relative wrap after linearization. Shipped in [`IntervalProjection`](../../../include/Utils/IntervalProjection.h) (UIP Phase 2) — no separate **`normalizeWrapToLinear`** module.

### D15 — Group interactions by target note (no *Set* suffix)

**Decision:** Every geometry tick **regroups** analyze output by **target `NoteId`**. Pure reorganisation — no new domain facts, no storage mutation. There is **no** **`updateConstraintRegistry`**, no persistent map on **`NoteEditFocus`**, no removal bookkeeping.

**Naming:** Avoid **`*Set`** in type names — collides with **CurrentSet** / **SavedSet** product vocabulary. Prose may still say “constraint grouping stage” (semantic role); identifiers use **group** + **Interaction** + **Target**.

**Structure** — resolver operates only on this shape:

```
EditSessionInteractionsByTarget
    │
    ├── Target A
    │     ├── Interaction (causing B)
    │     ├── Interaction (causing C)
    │     └── …
    ├── Target D
    │     └── …
    └── …
```

```cpp
struct TargetNoteInteractionGroup {
  NoteId targetNoteId;
  // All incoming EditSessionInteraction for this target this tick;
  // deterministic order: ascending causingNoteId
  std::vector<EditSessionInteraction> incoming;
};

struct EditSessionInteractionsByTarget {
  // One group per target NoteId at most; deterministic order: ascending targetNoteId
  std::vector<TargetNoteInteractionGroup> groups;
};

EditSessionInteractionsByTarget groupEditSessionInteractionsByTarget(
    span<const EditSessionInteraction> interactions);
```

**Grouping invariants:**

- Every **target `NoteId`** appears **at most once** in **`groups`**.
- Each **`TargetNoteInteractionGroup`** contains **all** incoming interactions for that target this tick.
- **`incoming`** order is **deterministic** — ascending **`causingNoteId`**.
- **`groups`** order is **deterministic** — ascending **`targetNoteId`**.

When causing **B** moves away and **C** still constrains **A**, the next tick’s analyze pass simply omits **(B, A)**; **`groupEditSessionInteractionsByTarget`** output reflects **{C → A}** only. Order of prior ticks does not matter.

### D16 — **`ConstrainedNoteGeometry`**, constrained geometry resolution, and combine precedence

**Decision:** **`resolveConstrainedGeometry`** (**constrained geometry resolution**) combines per-target interactions into **`ConstrainedNoteGeometry`** — the sole description of **desired live geometry** for overlap **target** notes this tick. No interaction types, no action types, no policy table in output. **No separate Constraint type** — **`EditSessionInteraction`** rows carry sufficient span data; off-limit math runs inside resolve.

**Resolver output scope:**

**`ConstrainedNoteGeometry`** is emitted **only** for **target `NoteId`** values that:

1. have **one or more incoming interactions** in **`EditSessionInteractionsByTarget`** this tick, **or**
2. are **overlap restore candidate notes** — **transaction baseline** has the note, **live store** linear span differs from baseline (**hidden**, **shortened**, or **missing pair**) due to side-effects of the **current edit driver**, and restore may be required when interactions rebuild empty.

**Output invariants:**

- Every **target `NoteId`** appears **at most once** in the constrained geometry collection.
- **Causing notes** are **not** in resolver output; the **edit session action builder** maps **edited geometry** → **MoveNote** / **ChangeLength** / etc.

**Constrained geometry target notes (resolve scope)** — union of categories 1 and 2:

| Category | Criterion |
|----------|-----------|
| **Interaction target notes** | **`targetNoteId`** appears in **`EditSessionInteractionsByTarget.groups`** with at least one incoming interaction this tick |
| **Overlap restore candidate notes** | **`NoteId`** has **transaction baseline** entry; **live store** linear span differs from baseline due to **current edit driver** side-effects; restore may be required when interactions rebuild empty |

**Overlap restore candidate notes** enable restore without incoming interactions (causing note moved away). Do **not** resolve every loop note — only **constrained geometry target notes**. **Causing notes** are **not** resolved here; the **edit session action builder** maps **edited geometry** → **MoveNote** / **ChangeLength** / etc.

```cpp
std::vector<NoteId> determineConstrainedGeometryTargetNoteIds(
    const EditSessionInteractionsByTarget& grouped,
    const BaselineMap& baseline,
    const MidiEventVec& liveStore,
    uint8_t channel);
```

**Per-target resolve** (unchanged pure function):

```cpp
ConstrainedNoteGeometry resolveConstrainedGeometry(
    NoteId target,
    const NoteBaseline& baseline,
    span<const EditSessionInteraction> incomingInteractionsForTarget,  // empty for overlap restore candidate only
    uint32_t loopLength,
    uint32_t noteMinLengthTicks,
    bool noteMinLengthRemoveEnabled);
```

When **`incomingInteractionsForTarget`** is empty, output SHALL match **baseline** span (**`visible = true`**, baseline ticks) — **restore** is decided by the **edit session action builder** when **live store** still differs.

```cpp
struct ConstrainedNoteGeometry {
  NoteId noteId;
  bool visible;           // false → pair absent in live store
  uint32_t startTick;     // linear
  uint32_t endTick;       // linear off / shortened end when visible
  uint8_t pitch;
};
```

**Combine precedence (evaluated in order):**

1. **Complete hide precedence** — if **any** incoming interaction is **OverlapNoteOn** or **CompleteCover** → **`visible = false`** (do not apply shorten math).
2. **Restrictive shorten combine** — else if **one or more** **OverlapNoteOff** → **`visible = true`**, **`startTick`** = baseline **`startTick`**, **`endTick`** = **`min(computeShortenedEndTick(i))`** over all **OverlapNoteOff** rows (**earliest linear note-off tick** wins; **`wraps`** affects off math). Span length is **`endTick − startTick`** — not computed inside **`computeShortenedEndTick`**.
3. **Boundary unchanged** — else if **BoundaryTouch** only → baseline span unchanged (**`visible = true`**, baseline ticks; **boundary split** in edit session action apply per D10).
4. **Baseline equivalent** — else (no incoming interactions) → baseline-equivalent span.
5. **Minimum note edit length hide** (always last) — when **`noteMinLengthRemoveEnabled`** is **true**, if **`visible = true`** and **`endTick − startTick < noteMinLengthTicks`** → **`visible = false`**. When **`noteMinLengthRemoveEnabled`** is **false**, skip this step (shorten result stands). Uses runtime **`noteMinLengthTicks`** / **`noteMinLengthRemoveEnabled`** from [`Globals.h`](../../../include/Globals.h) — same user settings as capture **NoteMinLength** (Q16); not a separate hardcoded edit floor.

**Resolution algorithm** (per target — normative pseudocode; implement inside **`resolveConstrainedGeometry`**):

```
Given baseline B, incoming interactions I[] (from one TargetNoteInteractionGroup):

  if any(i.type in {OverlapNoteOn, CompleteCover} for i in I):
    visible = false                                    // complete hide precedence
  else if any(i.type == OverlapNoteOff for i in I):
    visible = true
    startTick = B.startTick
    endTick = min(computeShortenedEndTick(i) for i in I where OverlapNoteOff)  // restrictive shorten combine
  else if I non-empty and every i.type == BoundaryTouch:
    visible = true; startTick = B.startTick; endTick = B.endTick      // boundary unchanged
  else:
    visible = true; startTick = B.startTick; endTick = B.endTick      // baseline equivalent

  if noteMinLengthRemoveEnabled and visible and (endTick - startTick) < noteMinLengthTicks:
    visible = false                                    // minimum note edit length hide

  pitch = B.pitch
  noteId = B.noteId (or target parameter)
```

**`computeShortenedEndTick(i)`** returns the linear **note-off tick** (**`endTick`**) the target should have for one **OverlapNoteOff** interaction — derived from **`i.causingSpan`**, **`i.baselineSpan`**, and **`i.wraps`** (brownfield: tail trim → **`causingSpan.startTick − 1`**, with wrap at loop boundary). It does **not** return note length; length is **`endTick − baseline.startTick`** and is floor-checked only in **minimum note edit length hide** (step 5). **`BoundaryTouch`** does not change **`visible`** or ticks in resolve; **boundary split** runs in **edit session action apply** (D10).

**Multi-causing example:** Two **OverlapNoteOff** constraints yielding **`endTick`** above minimum length → **shorten**. Any **OverlapNoteOn** or **CompleteCover** among incoming → **complete hide precedence** regardless of shortens. Two shortens whose combined most-restrictive off is below minimum length → step 5 → **hide**.

Represents only: **“What should this note look like right now?”** Ideal unit-test boundary between **constrained geometry resolution** and **edit session action builder**.

**Pipeline stage separation:**

```
Interaction  →  groupByTarget  →  ConstrainedNoteGeometry  →  EditSessionActions  →  apply
   (analyze)    (group)              (resolve)                  (build)
```

### D3 — Edit session action builder: minimal actions from constrained geometry compared to live store

**Decision:** **`buildEditSessionActions`** is the **edit session action builder** — it **observes** inputs, **computes** minimal **`EditSessionActions`**, and **does not mutate** live store (**edit session action apply** does). It MUST NOT inspect **`EditSessionInteraction`** directly (see **Invariant — Constrained Geometry Authority**).

**Builder inputs (explicit):**

```cpp
EditSessionActions buildEditSessionActions(
    span<const ConstrainedNoteGeometry> constrainedGeometry,
    const EditedGeometry& editedGeometry,
    const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore);
```

| Input | Role in builder |
|-------|-----------------|
| **`constrainedGeometry`** | Desired overlap-target geometry from resolve — **HideNote** / **ShortenNote** / **RestoreNote** |
| **`editedGeometry`** | User-intended causing spans this tick — **MoveNote** / **ChangeLength** / **ChangePitch** / **CreateNote** / **DeleteNote** |
| **`transactionBaseline`** | Restore reference; create/delete authority |
| **`liveStore`** | Current RAM — **omit actions that would not change live store** |

**Causing-note rows:** one action per **`NoteId`** in **`editedGeometry`** whose causing span differs from **live store** this tick. **`EditorSelection.primaryNote`** drives UI/bracket only.

**Two desired-geometry sources (locked):**

| Note role | Desired geometry source | Builder reads |
|-----------|-------------------------|---------------|
| Overlap target | **`ConstrainedNoteGeometry`** from **constrained geometry resolution** | constrained + **transaction baseline** + live store |
| Causing (selected) | **`EditedGeometry`** causing spans | edited + live store |

**Invariant — Constrained Geometry Authority** applies to overlap targets: builder must not read **`EditSessionInteraction`**. Causing-note actions never pass through **`resolveConstrainedGeometry`**.

**Evolution from prior art:** Old shape was Interaction → Policy → Action. Locked shape:

Interaction → **`groupEditSessionInteractionsByTarget`** → **`resolveConstrainedGeometry`** → **`ConstrainedNoteGeometry`** → **`buildEditSessionActions`** → **`applyEditSessionActions`**.

**Action mapping** — no **`ResolutionPolicy`** table (D22):

| **Constrained geometry compared to baseline / live store** | **EditSessionAction** (only if live store would change) |
|-----------------------------------------------------------|--------------------------------------------------------|
| **`visible = false`**, baseline had note, pair present in live store | **HideNote** |
| **`visible = true`**, **`endTick`** shortened compared to baseline, live store not yet shortened | **ShortenNote** |
| Matches baseline, live store differs (hidden/shortened) | **RestoreNote** |
| Causing note edited span differs from live store | **MoveNote** / **ChangeLength** / **ChangePitch** / **CreateNote** / **DeleteNote** |

**Omit actions that would not change live store (D3):** The builder SHALL emit an action **only** when applying it would **change** the current **live store** for that **`NoteId`**. If **constrained geometry** already matches **live store** (pair present, same ticks/pitch; or already absent when **`visible = false`**), **omit** the action.

**Retired:** **`movingNoteRange`**, `allowSharedEndCoexistence`, adjacent **merge**, legacy **49-tick** rule.

**Minimum note edit length hide:** Applied in resolver step 5 (D16) — builder does not re-apply length floor; **`visible = false`** from resolver → **HideNote** only if live store still has the pair.

### D10 — Boundary touch: analyze + split (edit); capture follow-on

**Decision (NOTE_EDIT):** **`BoundaryTouch`** is detected in **`analyzeEditSessionInteractions`** only. Hide/shorten/restore ignore it.

**Boundary split ownership (locked):** **Boundary split** (off-at-on−1 and same-tick on/off adjustment) is **part of edit session action apply** — the final live-store geometry sub-step inside **`applyEditSessionActions`** after ordered **RestoreNote** / **ShortenNote** / **HideNote** / causing-note actions. It is **not** **`normalizeWindow`** (DEC-014 edit-closure canonicalization only).

**Same-tick note-on / note-off (locked):** When causing **note-on** and target **note-off** share the same linear tick, **note-on keeps its tick** (audible overlap intent — two notes at boundary). Target **note-off moves to `onTick − 1`** during **edit session action apply** boundary split so both notes can sound. **Merge into one note** is a **future** action (see §Future MergeNote).

**General split (v1 — Q7 locked A):** boundary split adjusts abutting **BoundaryTouch** pairs so earlier **off → later on − 1** (same rule family as same-tick Q6).

**Q7:** **Locked A** — D10 off-at-on−1 for non-same-tick boundary touch as well.

**Follow-on OpenSpec (accepted):** Record/overdub capture cleanup — **`capture-pass-boundary-materialization`** (boundary split + **NoteMinLength** hot stop per Q16). See [`capture_pass_note_min_length_refinement.md`](../../../docs/plans/capture_pass_note_min_length_refinement.md).

### D11 — Chunk-scoped baseline (deferred)

**Decision:** **v1** uses **full-loop** **`baselineMap`** at **edit driver boundary** (D19). Chunk-scoped baseline aligned with display/preload window is **deferred** until adjustable analysis window ships.

### D12 — Live apply model

**Decision:** **In-place** mutate **`NoteEditSession.store`** each geometry tick. Session undo snapshot at **edit driver boundary** or kind change per existing **`EditManager`** contract.

### D4 — Edit session action apply is the only live store writer

**Decision:** **`applyEditSessionActions(actions, sessionStore, focus, channel, loopLength)`** applies pairs/ticks/hide/restore, then runs **boundary split** (D10). No other function removes or rewrites note on/off for overlap during live edit.

**Rationale:** Matches DEC-013 — **`normalizeWindow`** canonicalizes at edit closure; **`applyEditSessionActions`** writes geometry (including boundary split); validate asserts.

**Ordering (deterministic):**

1. **RestoreNote** (non-mover baseline notes)
2. **ShortenNote**
3. **HideNote**
4. **MoveNote** / **ChangeLength** / **ChangePitch** / create-delete — **one row per causing `NoteId`** with geometry delta

Aligns with overlap-before-mover ordering at micro apply.

### D9 — Rebuild constraints each tick; restore is a consequence, not a behavior

**Decision:** Live overlap state is **never** read from a persistent registry or **`overlapNotes`**. Each tick: analyze → **group interactions by target** → **resolve constrained geometry** → **edit session action builder**.

**Restore is not imperative.** There is no restore history, reverse restore chain, or “restore prelude”:

```
No active constraints for target
        │
        ▼
ConstrainedNoteGeometry == baseline
        │
        ▼
Edit session action builder: live store still hidden/shortened
        │
        ▼
RestoreNote emitted if required
```

**Note edit pass commit path:** One **`noteEditPass` batch** with **`EditPass` row per changed `NoteId`** — **transaction baseline compared to final live store** (D18).

### D5 — Remove `overlapNotes`; no persistent constraint store

**Decision:** Remove **`overlapNotes`**. Do **not** add a persistent **`ConstraintRegistry`** on **`NoteEditFocus`**. Only **transaction baseline** persists across ticks within one **edit driver** (D19).

### D22 — Do not reintroduce **`ResolutionPolicy`**

**Decision:** Keep **`ResolutionPolicy`** removed. Policy lives in **constrained geometry resolution** combine precedence (D16) and **edit session action builder** mapping (D3) only.

### Rejected alternatives (pipeline shape)

| Alternative | Why rejected |
|-------------|--------------|
| Persistent **`ConstraintRegistry`** on **`NoteEditFocus`** | Stale entries; second mutable authority (D5) |
| Ephemeral **`Constraint`** / **`ConstraintType`** projection struct | **`EditSessionInteraction`** already carries spans; off-limit math belongs inside **`resolveConstrainedGeometry`** — no additional domain object |
| Interaction → Constraint → **`ConstrainedNoteGeometry`** three-layer model | Locked: Interaction → **group by target** → **`resolveConstrainedGeometry`** → **`ConstrainedNoteGeometry`** |
| **`ResolutionPolicy`** lookup table | Duplicates D16 + D3; removed (D22) |
| Draft stage labels (Diff Builder, Executor, Constraint Set) | Use locked § Terminology — **edit session action builder**, **edit session action apply**, **`EditSessionInteractionsByTarget`** |

### Invariant — Constrained Geometry Authority

**`ConstrainedNoteGeometry`** is the **sole** description of desired **overlap-target** geometry during a geometry update tick.

- **`buildEditSessionActions`** SHALL derive overlap-target **`EditSessionActions`** from **constrained geometry**, **transaction baseline**, and **live store**; causing-note actions from **edited geometry** and **live store**. It SHALL **omit actions that would not change live store** for that **`NoteId`** (D3).
- **`buildEditSessionActions`** MUST NOT inspect **`EditSessionInteraction`** or **`EditSessionInteractionsByTarget`** directly.
- **`resolveConstrainedGeometry`** MUST NOT emit or branch on **`EditSessionActionType`**.

Benefits: strict stage ownership, no interaction logic leaking into the builder, **constrained geometry resolution** and **edit session action builder** independently testable.

### D6 — `EditorSelection` anchors targets; baselineMap anchors spans

**Decision:** Every **`EditSessionAction`** targeting a note references **`NoteId`**. Span ticks come from **`baselineMap`** / linear session pair — not display **`DisplayNote.endTick`** wrap segments.

**Rationale:** 224146, 231310, Tier 5 baseline bugs.

### D7 — Normalize boundaries unchanged (DEC-014)

**Decision:** **`applyEditSessionActions`** does not call **`normalizeAll`**. **Geometry tick path:** **boundary split** inside apply (D10); **`normalizeWindow`** on **edit closure** only after apply + before fader latch. **Note edit pass commit path:** **`normalizeAll`** at **`commitAllPendingNoteEditActions`**.

**Rationale:** Already shipped partial wiring + invariant gates.

### D8 — Cross-session extension (future)

**Decision:** Pipeline API takes **`EditSessionType`** + session-specific geometry payload. NOTE_EDIT implements first; Loop/CC add relationship kinds and action types without renaming core types.

Example future actions:

| **EditSessionType** | Actions |
|---------------------|---------|
| **Note** | MoveNote, ChangeLength, ChangePitch, ShortenNote, HideNote, RestoreNote |
| **Loop** | ChangeLoopLength, ChangeLoopStart, … |
| **ControlChange** | SetControlChangeValue, … |

Shared verbs: **`analyzeEditSessionInteractions`**, **`groupEditSessionInteractionsByTarget`**, **`resolveConstrainedGeometry`**, **`buildEditSessionActions`**, **`applyEditSessionActions`**.

### D13 — Priority: replaces `linear-loop-tick-storage` Phase 2 wire

**Decision:** This pipeline is the geometry fix path on hardware; do not wire imperative overlap fixes under linear-loop Phase 2 separately.

### D14 — HITL fixture: record + 2× overdub capture pass

**Decision:** Extend HITL so interaction matrix tests reuse a **live-recorded** loop fixture: canonical **base + 2× overdub** (`host_midi_hitl.py run --preset base`), saved as a reusable MIDI/session capture. **`edit_full`** (or per-interaction presets) replays that fixture instead of relying on one monolithic edit test that does not cover all **`InteractionType`** rows.

**Native:** Full matrix in `test_edit_session_*`; HITL validates subset + regression on captured loop.

## Data structures (proposed)

```cpp
struct EditSessionInteraction { /* D2 */ };

struct TargetNoteInteractionGroup {
  NoteId targetNoteId;
  std::vector<EditSessionInteraction> incoming;
};

struct EditSessionInteractionsByTarget {
  std::vector<TargetNoteInteractionGroup> groups;  // ephemeral — D15
};

struct ConstrainedNoteGeometry {
  NoteId noteId;
  bool visible;
  uint32_t startTick;
  uint32_t endTick;
  uint8_t pitch;
};

EditSessionInteractionsByTarget groupEditSessionInteractionsByTarget(
    span<const EditSessionInteraction> interactions);

ConstrainedNoteGeometry resolveConstrainedGeometry(
    NoteId target,
    const NoteBaseline& baseline,
    span<const EditSessionInteraction> incomingInteractionsForTarget,
    uint32_t loopLength,
    uint32_t noteMinLengthTicks,
    bool noteMinLengthRemoveEnabled);

enum class EditSessionActionType : uint8_t {
  RestoreNote,
  ShortenNote,
  HideNote,
  MoveNote,
  ChangeLength,
  ChangePitch,
};

struct EditSessionAction {
  EditSessionActionType type;
  NoteId targetNoteId;
  uint32_t startTick;   // linear storage
  uint32_t endTick;     // linear storage (off or shortened end)
  uint8_t pitch;
  // ...
};

using EditSessionActions = std::vector<EditSessionAction>;
```

**Do not** name the container **`EditPlan`**. **`EditSessionActions`** is the ordered list type.

## Mapping from current code

| Today | After |
|-------|-------|
| `restoreOverlapNotesNoLongerOverlapping` | Regroup interactions → **resolveConstrainedGeometry** → **RestoreNote** |
| `findOverlaps` | **`analyzeEditSessionInteractions`** |
| `applyShortenOrDelete` | **`resolveConstrainedGeometry`** + builder **ShortenNote** / **HideNote** |
| `overlapNotes` | **Removed** — ephemeral **`EditSessionInteractionsByTarget`** |
| `moveNoteWithOverlapHandling` / `changeLengthWithOverlapHandling` | Full pipeline (D architecture) |
| Adjacent merge loop | Boundary **split** (D10) |
| `buildPreCommitEditPasses` | Replaced by **baseline diff** → one **`noteEditPass` batch** (D18) |
| `movingNoteRange` | **Retired** |
| `filterSelectableDisplayNotes` / edit closure | Derive hidden from store vs baseline, not **`overlapNotes`** |
| `finalReconstructAndSelect` | After apply; sync **`EditorSelection`** + filtered index |

## Retire list (post wire)

- `restoreOverlapNotesNoLongerOverlapping`
- `restoreOverlapNotesForPitchLaneClear` (logic → builder + restore actions)
- Restore-first staging in `moveNoteWithOverlapHandling` / `changeLengthWithOverlapHandling`
- Display-tick `% loopLength` on storage mutation in length path
- Adjacent lane **merge** loop in `NoteMovementUtils`
- `allowSharedEndCoexistence`
- **`movingNoteRange`** on **`NoteEditFocus`**
- **`overlapNotes`** struct and restore paths

## Test strategy

| Suite | Tests |
|-------|-------|
| **`test_edit_session_interaction`** | D17 Add/Delete; D20 wrap-before-analyze; no **None** enum |
| **`test_resolve_constrained_geometry`** | Combine precedence; **constrained geometry target notes** scope; rebuild when causing gone |
| **`test_edit_session_action_builder`** | **Omit unchanged live store actions**; invariant — no interaction inspect |
| **`test_apply_edit_session_actions`** | **Edit session action apply** + **`LoopEventValidation`**; 144458 restore scope |
| Existing **`test_note_edit_focus`** | Baseline/linear helpers (unchanged) |
| HITL | Per-interaction presets on **base + 2× overdub** captured loop (D14) |

## Risks

| Risk | Mitigation |
|------|------------|
| Dual authority during migration | **`overlapNotes`** removed in Phase 4 wire, not phased parallel |
| Builder/apply drift from HITL | Scenario matrix + captured loop fixture (D14) |
| Performance (recompute every tick) | Required — every fader tick re-analyzes; closure-scoped apply |
| Contained 24 vs universal 23 floor | **Resolved** — **OverlapNoteOff** + **minimum note edit length hide** via **`noteMinLengthTicks`**; **OverlapNoteOn** → **HideNote** |

## Resolved (2026-07-04 design session)

- Baseline refresh: **edit driver boundary** (primary driver), not strict kind-only (D19)
- Analyzer independent of edit-session state; orchestrator owns D17 (post-review)
- **Constrained Geometry Authority** invariant; **edit session action builder** does not read interactions
- **`ResolutionPolicy`** stays removed (D22)
- Live mutate: **in-place** + session undo per existing contract
- **Note edit pass commit — **`EditPass`**: **one `noteEditPass` batch**, row per changed **`NoteId`** (D18)
- Analysis window (this change): **full loop** (D11 deferred)
- **Add/Delete** trigger same interaction pipeline (D17)
- **`movingNoteRange`**: **retired**
- **Wrap:** **Edit projection** before analyze (D20)
- **Interaction grouping:** ephemeral **`EditSessionInteractionsByTarget`** (D15); avoid **\*Set** suffix (CurrentSet/SavedSet)
- **Constrained geometry resolution (D16):** **`resolveConstrainedGeometry`** — central algorithm; no **Constraint** type
- **Derived geometry philosophy:** visibility and actions derived per tick — no overlap scratch state machines
- **Selection (D17):** skip **selected-to-selected overlap pairs**; **selected-to-selected overlap when geometry changed** **deferred**
- **Resolver scope (D16):** **constrained geometry target notes** — **interaction target notes** + **overlap restore candidate notes**
- **Combine precedence (D16):** **complete hide precedence** → **restrictive shorten combine** → **boundary unchanged** → **baseline equivalent** → **minimum note edit length hide**
- **Edit session action builder (D3):** **omit actions that would not change live store**
- **Cross-pitch + pitch-lane scope (D21):** destination lane + source lane on pitch change
- **Minimum note edit length hide (D16):** **`noteMinLengthTicks`** + **`noteMinLengthRemoveEnabled`** (runtime globals; default 12 ticks, enabled)
- **Same-tick boundary:** note-on keeps tick; note-off → on−1 (D10)
- **Always apply** overlap pipeline this change (no reject-on-overlap mode)
- **Poly shorten / MergeNote:** deferred
- **Prior art:** geometry parity fixtures (Q1); LOOP_MIDI pairing doc (Q10); Lytrix test reuse (Q11)

## Prior art comparison (informative — our vocabulary only)

| Our outcome | Typical DAW batch tool behavior |
|-------------|----------------------------------|
| **OverlapNoteOff** → shorten target | “Cut overlaps” / shorten to next note start |
| **HideNote** / **CompleteCover** | Delete overlap / replace overlapped note |
| **RestoreNote** on rebuild | *(most DAWs lack live restore-on-move-away)* |
| Reject edit | Optional in some DAWs — **not v1** |
| Merge two spans into one | “Legato merge” / extend — **future MergeNote** |

## Future — MergeNote (deferred)

When causing span **fully covers** target on same pitch, v1 emits **HideNote** on target. Future **`MergeNote`** could:

1. Compute union span `[min(starts), max(ends)]` on linear ticks.
2. Emit single pair for survivor **`NoteId`**; remove swallowed **`NoteId`** pair.
3. Velocity/pitch from **causing** note (Ardour “replace both with one note” analogue).

Requires new **`EditSessionActionType`**, macro **`EditPass`** diff rules, and undo rows for removed **`NoteId`**. Out of NOTE_EDIT v1.

## References

- [`docs/plans/note_edit_session_action_geometry_enhancement.md`](../../../docs/plans/note_edit_session_action_geometry_enhancement.md)
- [`linear-loop-tick-storage`](../linear-loop-tick-storage/design.md) D3–D4 normalize
- [`note-edit-stable-note-id`](../../specs/note-edit-stable-note-id/spec.md) **EditorSelection**
- BUG: 144458, 152335, 231310; handoffs in `docs/plans/note_edit_geometry_*`
