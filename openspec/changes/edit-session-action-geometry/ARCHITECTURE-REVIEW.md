# Architecture review — edit-session-action-geometry

**Change:** `edit-session-action-geometry`  
**Date:** 2026-08-05  
**Status:** Active — use this file before and after **each phase** of the transaction-baseline ownership rebuild  

**Related:** [proposal.md](proposal.md), [design.md](design.md), [tasks.md](tasks.md),
[`docs/plans/note_edit_session_action_geometry_transaction_baseline_bugfix.md`](../../../docs/plans/note_edit_session_action_geometry_transaction_baseline_bugfix.md)

---

## Primary invariant (north star)

> Given the **immutable transaction baseline** and **current edited geometry**, compute what
> **live store** must look like right now. Overlap analysis is gated to the **mover's current
> pitch lane**. Baseline is frozen at **edit driver boundary** (D19) and is never pruned from
> live-store presence.

---

## Finding → phase map (transaction baseline rebuild)

| Severity | Finding | Phase | Owner |
|----------|---------|-------|-------|
| Critical | `buildEditClosureNoteIds` seeds baseline from `focus.last.pitch` only → zero overlap candidates when mover pitch ≠ neighbour pitches | 2 | `NoteEditFocus` / `buildEditClosureNoteIds` |
| Critical | `analyzeEditSessionInteractions` has no pitch gate → widening baseline would hide unrelated pitches | 1 | `analyzeEditSessionInteractions` |
| Critical | Per-tick `enrichBaselineMapFromCommittedAndLive` prune deletes hidden notes' baseline → no restore, no Delete commit row | 2 | `runEditSessionGeometryPipeline` / baseline ownership |
| Critical | Mid-edit `assignMissingNoteIds` mints ids absent from capture materialize → Delete rows replay as no-ops | 3 | `openNoteEditSession` |
| High | Resolve uses unprojected baseline while analyze uses projected → inverted spans → `LinearNoteOff` (check=2) | 4 | `resolveAllConstrainedGeometry` |
| High | Session-tagged apply/builder fallbacks paper over LIFO ambiguity | 5 | `ApplyEditSessionActions` / `EditSessionActionBuilder` |
| Medium | `overlapNotes` still drives display filter / legacy commit while pipeline writes none | 5 | OpenSpec 4.5 / 4.5b |

---

## Evidence anchors

| Concern | Primary location |
|---------|------------------|
| Same-pitch closure | `buildEditClosureNoteIds` in `NoteEditFocus.cpp` |
| Per-tick enrich + prune | `enrichBaselineMapFromCommittedAndLive`, `runEditSessionGeometryPipeline` |
| Pitch-less analyze | `analyzeEditSessionInteractions` in `EditSessionInteraction.cpp` |
| Baseline writes on apply | `ensureBaselineMapEntryForEditSessionAction`, `ensureBaselineMapBeforeShortenApply` |
| Pre-commit row source | `buildPreCommitBaselineLiveDiffOverlapPasses` |
| noteId assign | `Loop::assignMissingNoteIds`, `openNoteEditSession` |
| Capture evidence | `captures/session_20260805_013428.log`, `session_20260805_011000.log` |

---

## Per-phase gates

### Phase 0 — Documentation + WIP preserve (this session)

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `NoteEditFocus.baselineMap`; `runEditSessionGeometryPipeline` |
| Primary invariant | Immutable full-loop baseline; pitch-lane analyze |
| Ownership change? | **YES** — approved (plan); documented here |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** for Phase 0 (docs / branch only) |
| Reuse decision | YES — extend existing baseline + pipeline |
| Phase scope | Branch/commit WIP; this file; bugfix plan; Q14 in design.md |

| Implementation review | |
|-------------------------|--|
| WIP committed off detached HEAD | [x] |
| Bugfix plan written | [x] |
| Q14 resolved in design.md | [x] |
| **Approval** | APPROVE |

---

### Phase 1 — Pitch-lane gate in analyze

**Scope:** Skip pairs in `analyzeEditSessionInteractions` when target baseline pitch ≠ edited causing span pitch.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `analyzeEditSessionInteractions` |
| Primary invariant | Positive interaction graph is same-pitch only |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** in isolation (baseline still same-pitch today) |
| Reuse decision | YES — extend analyzer classify path |
| Phase scope | `EditSessionInteraction.cpp` + interaction tests |

| Implementation review | |
|-------------------------|--|
| Cross-pitch pair omitted | [ ] |
| Pitch-change destination lane in scope | [ ] |
| `pio test -e native` (interaction suite) | [ ] |

---

### Phase 2 — Immutable full-loop transaction baseline

**Scope:** Snapshot `baselineMap` once per edit driver from committed materialize; remove per-tick enrich/prune; delete apply-path baseline writers; full-loop closure.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `NoteEditFocus.baselineMap` at edit-driver boundary |
| Primary invariant | Baseline immutable until `primaryNote` changes |
| Ownership change? | **YES** — approved Phase 2 |
| State transition change? | **NO** — refresh still at D19 boundary |
| Behavior-preserving? | **NO** — intentional fix |
| Reuse decision | YES — extend `rebuildNoteEditFocus*` / driver boundary |
| Phase scope | `NoteEditFocus.*`, `RunEditSessionGeometryPipeline.*`, `ApplyEditSessionActions.*`, `EditManager.*` |

| Implementation review | |
|-------------------------|--|
| No per-tick prune | [ ] |
| Hide survives to pre-commit Delete row | [ ] |
| Restore after leave | [ ] |
| `pio test -e native` | [ ] |

---

### Phase 3 — noteId identity contract

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `openNoteEditSession` / `Loop::assignMissingNoteIds` |
| Primary invariant | Session noteIds stable for geometry + commit replay |
| Ownership change? | **NO** — move call site only |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional fix for Delete replay |
| Reuse decision | YES — existing assign APIs |
| Phase scope | `EditManager::openNoteEditSession`, `runEditSessionGeometryPipeline` |

---

### Phase 4 — Projection consistency + canonical store

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `resolveAllConstrainedGeometry` / pipeline orchestrator |
| Primary invariant | Resolve uses same projected baseline as analyze; no inverted spans |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — fixes check=2 |
| Reuse decision | YES — existing projection helpers |
| Phase scope | `RunEditSessionGeometryPipeline.cpp`, `ResolveConstrainedGeometry.cpp` |

---

### Phase 5 — Retire workarounds + overlapNotes scratch

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | Live store + baselineMap (display/commit); retire `overlapNotes` |
| Primary invariant | No persistent constraint registry (D5, D15) |
| Ownership change? | **YES** — approved OpenSpec 4.5 / 4.5b |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional retire |
| Reuse decision | YES — baseline-diff commit path already primary |
| Phase scope | Apply/builder cleanup; display filter; commit gates |

---

### Phase 6 — Verification

| Gate | Pass |
|------|------|
| `pio test -e native` | [ ] |
| Device: stable `flatEvents` | [ ] |
| Device: `type=1` / `type=2` on same-pitch overlap | [ ] |
| Device: no `non-canonical store` / `missing in recon` | [ ] |

---

## Architecture checkpoint (bugfix)

1. **Does this bug require changing ownership?** YES — transaction baseline mutability / prune ownership. Approved via this review + bugfix plan.
2. **Does this bug require changing state transitions?** NO — edit-driver boundary refresh remains D19.
