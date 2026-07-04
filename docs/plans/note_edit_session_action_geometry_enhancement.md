# Handoff — EditSessionAction geometry pipeline

**Date:** 2026-07-04 (post architecture review)  
**OpenSpec:** [`openspec/changes/edit-session-action-geometry/`](../../openspec/changes/edit-session-action-geometry/)  
**Status:** Design locked — awaiting user sign-off (task 0.4) before implementation

---

## One-line goal

**Transaction baseline + current edited geometry → correct live store state.** Derived geometry each tick — no overlap scratch state. Orchestrator → wrap-linearize → analyze (positive graph) → **group by target** → **`resolveConstrainedGeometry`** (central algorithm) → **edit session action builder** → **edit session action apply**. No persistent registry, no restore history, no **`overlapNotes`**.

---

## Philosophy

- **One question:** given transaction baseline + edited geometry, what should live store look like now?
- **Model only:** baseline, interaction graph, **`ConstrainedNoteGeometry`** (resolved desired geometry for overlap targets)
- **`visible`** and **`EditSessionActions`** are **derived** — not stored overlap state machines
- **No Constraint type** — **`EditSessionInteraction`** + **`resolveConstrainedGeometry`** math is sufficient

## Locked decisions (2026-07-04 + post-review)

| # | Decision |
|---|----------|
| 1 | Macro = **one `noteEditPass` batch**, **`EditPass` row per changed `NoteId`** (baseline diff) |
| 2 | **Add/Delete** participate in interaction analysis (selected new note → causing; delete causing → restore via rebuild) |
| 3 | **Edit driver boundary** — **`primaryNote`** change refreshes baseline; not strict kind boundary |
| 4 | One action per causing **`NoteId`**; **`primaryNote`** = UI/bracket only |
| 5 | **`movingNoteRange`** **retired** |
| 6 | **BoundaryTouch** — analyze only for edit; capture pass refinement deferred |
| 7 | **`normalizeWrapToLinear`** before analyze; wrapped targets supported |
| 8 | **Orchestrator owns selection** — changed causing notes + **eligible overlap pairs**; **analyzer is pure geometry** |
| 9 | **Edit session action builder** — four inputs; overlap targets from **`ConstrainedNoteGeometry`**; causing notes from **edited geometry**; **must not read interactions** |
| 10 | **`ResolutionPolicy`** stays removed |
| 11 | **Omit actions that would not change live store** |
| 12 | **Constrained geometry target notes** — **interaction target notes** + **overlap restore candidate notes**; **combine precedence** documented |
| — | **Positive interaction graph** — no **None**; omitted pair = meaningful |
| — | **Selected-to-selected overlap when geometry changed** **deferred** (future multi-select length) — skip all **selected-to-selected overlap pairs** in this change |
| — | **Rejected:** ephemeral **Constraint** type; Interaction → Constraint → Geometry chain |
| — | **Constrained geometry resolution** is the central algorithm — **`resolveConstrainedGeometry`** |
| — | **Restore** = consequence of empty interactions + baseline-equivalent geometry + live store compare — not imperative behavior |
| — | **`overlapNotes`** removed; ephemeral **`EditSessionInteractionsByTarget`** grouped per target |

---

## Selection domain (orchestrator only)

- **`EditorSelection`** = already-resolved geometry for selected notes (one editing domain).
- **Orchestrator** determines **changed causing notes** (`geometryChangedThisTick` compared to encoder latch) and **eligible overlap pairs** — analyze never sees selection policy or latches.
- **Eligible overlap pair (this change):** causing note selected, target note not selected.
- **Skip (this change):** **selected-to-selected overlap pairs** — always skip.
- **Deferred:** **selected-to-selected overlap when causing note geometry changed** — future multi-select length editing only.
- **Add:** new selected note → causing input.
- **Delete:** omit causing id → rebuild → **RestoreNote** via edit session action builder.

---

## Pipeline

```
Transaction baseline (immutable per edit driver)
Edited geometry
        │
        ▼
normalizeWrapToLinear()
        │
Edit session geometry orchestrator
        │
        ▼
analyzeEditSessionInteractions()  ← positive interaction graph only
        │
        ▼
groupEditSessionInteractionsByTarget()
        │
        ▼
resolveConstrainedGeometry()      ← constrained geometry target notes; combine precedence
        │
        ▼
buildEditSessionActions()         ← edit session action builder (4 inputs; omit unchanged actions)
        │
        ▼
applyEditSessionActions()         ← edit session action apply (ordered actions + boundary split)
        │
        ▼
normalizeWindow (edit closure only — DEC-014)
        │
        ▼
Read-only projection
```

**Note edit pass commit path:** `commitAllPendingNoteEditActions` → `normalizeAll` → **transaction baseline compared to final live store** → one **`noteEditPass` batch**.

---

## Live store

**Live store** = in-RAM MIDI pairs for the active NOTE_EDIT session (`EditSession.store` / `sessionMidiEvents()`). Authoritative RAM during edit; **only** `applyEditSessionActions` mutates it for geometry. Edit session action builder **observes** constrained geometry, edited geometry, baseline, and live store; **computes** actions — it does not write. See [design.md § Live store](../../openspec/changes/edit-session-action-geometry/design.md).

---

## Edit driver boundary

Baseline refreshes when **`EditorSelection.primaryNote`** changes or a new edit gesture starts. Repeated encoder ticks on the **same** primary do **not** refresh baseline.

---

## Stage responsibilities

| Stage | Responsibility |
|-------|----------------|
| Orchestrator | Baseline boundary; changed causing notes; **eligible overlap pairs** |
| Interaction analysis | Discover geometry facts only |
| Interaction grouping | Group interactions per target note |
| Constrained geometry resolution | Desired geometry (`ConstrainedNoteGeometry`) |
| Edit session action builder | Observe inputs; compute minimal actions; **omit unchanged live store actions** |
| Edit session action apply | Apply actions to live store; **boundary split** sub-step (D10) |
| Read-only projection | Display/playback |

---

## Interaction types (positive graph)

**OverlapNoteOn**, **OverlapNoteOff**, **CompleteCover**, **BoundaryTouch**. Non-overlapping pairs **omitted** — absence is meaningful. **`wraps`** attribute only.

---

## Resolver precedence

1. **Complete hide precedence** — **OverlapNoteOn** / **CompleteCover**
2. **Restrictive shorten combine** — **`endTick = min(computeShortenedEndTick(i))`** over **OverlapNoteOff** rows (earliest linear note-off tick)
3. **Boundary unchanged** — **BoundaryTouch** only
4. **Baseline equivalent** — no incoming interactions
5. **Minimum note edit length hide** — when **`noteMinLengthRemoveEnabled`**: span **`endTick − startTick < noteMinLengthTicks`** → hide; when disabled, skip

**`computeShortenedEndTick(i)`** — per **OverlapNoteOff**: linear **note-off tick** for target (typically **`causingSpan.startTick − 1`**); combine via **`min(...)`**.

**Constrained geometry target notes:** **interaction target notes** + **overlap restore candidate notes**. Causing notes via **edited geometry**, not resolve.

---

## Edit session action builder

**Inputs:** **`constrainedGeometry`**, **`editedGeometry`**, **`transactionBaseline`**, **`liveStore`**.

**Omit actions that would not change live store.** Emit **HideNote** / **RestoreNote** / **ShortenNote** / causing note actions only when live store would change.

---

## Authority

| Object | Authority |
|--------|-----------|
| Transaction baseline | Original note geometry for this edit driver |
| Edited geometry | User intent this tick |
| Interaction graph | Geometry facts only |
| Constrained geometry | Desired overlap-target geometry |
| Live store | Current session state |
| EditSessionActions | Required mutations |

---

## Invariant — Constrained Geometry Authority

**`ConstrainedNoteGeometry`** is the sole desired-geometry description for **overlap targets** per tick. **`buildEditSessionActions`** compares constrained geometry + baseline + live store for overlap targets; **edited geometry** + live store for causing notes — **never** **`EditSessionInteraction`**.

---

## Boundary split

**Part of edit session action apply** (`applyEditSessionActions`), not **`normalizeWindow`**.

## Restore (consequence, not behavior)

```
No constraints → ConstrainedNoteGeometry == baseline → live store wrong → RestoreNote
```

No restore history, no reverse chains, no restore prelude.

---

## Tests

| Suite | Focus |
|-------|-------|
| `test_edit_session_interaction` | Pure analyze; positive graph; wrap linearize |
| `test_resolve_constrained_geometry` | Combine precedence; constrained geometry target notes |
| `test_edit_session_action_builder` | Omit unchanged live store actions; invariant |
| `test_apply_edit_session_actions` | Edit session action apply + invariants |

HITL: **base + 2× overdub** captured loop.

---

## Files (planned)

| New | Role |
|-----|------|
| `include/EditSessionAction.h` | Types |
| `src/EditSessionInteraction.cpp` | **`analyzeEditSessionInteractions`**, **`normalizeWrapToLinear`**, **`groupEditSessionInteractionsByTarget`**, **`determineConstrainedGeometryTargetNoteIds`** |
| `src/ResolveConstrainedGeometry.cpp` | **`resolveConstrainedGeometry`** |
| `src/EditSessionActionBuilder.cpp` | **`buildEditSessionActions`** (edit session action builder) |
| `src/ApplyEditSessionActions.cpp` | **`applyEditSessionActions`** |
| Orchestrator in `NoteEditManager` / geometry entry | Changed causing notes + **eligible overlap pairs** |

See [tasks.md](../../openspec/changes/edit-session-action-geometry/tasks.md).
