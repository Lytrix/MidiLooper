# Handoff — Derived note overlap logic (EditSessionAction pipeline)

**Date:** 2026-07-04 · **Updated:** 2026-08-04 (Phase 0 doc sync + 0.4 sign-off)  
**Status:** **Ready for Phase 1** — UIP Phases 1–5 **code shipped**; UIP Phase 6 doc sync complete; brownfield guard queued before/alongside Phase 1 native  
**OpenSpec:** [`openspec/changes/edit-session-action-geometry/`](../../openspec/changes/edit-session-action-geometry/)  
**Apply command:** `/opsx:apply edit-session-action-geometry`

**Prerequisite (met):** [`unified_interval_projection_enhancement.md`](unified_interval_projection_enhancement.md) — **Edit projection** (`buildEditProjectionContext`, `projectEditIntervalsForAnalysis`) replaces planned `normalizeWrapToLinear`

**Design detail:** [`note_edit_session_action_geometry_enhancement.md`](note_edit_session_action_geometry_enhancement.md)  
**Prior art (Q1–Q16):** [`edit_session_action_geometry_prior_art_refinement.md`](edit_session_action_geometry_prior_art_refinement.md)  
**Plan:** Cursor plan `note_geometry_refactor` — Phase 0 complete 2026-08-04

---

## One-line goal

Replace imperative **`NoteMovementUtils`** overlap chains and **`overlapNotes`** scratch with a **derived** pipeline each geometry tick: **transaction baseline + edited geometry → Edit projection → analyze → group by target → `resolveConstrainedGeometry` → `buildEditSessionActions` → `applyEditSessionActions`**.

---

## Brownfield context (2026-08-04)

| Layer | Status on `dev` |
|-------|-----------------|
| **Note edit** | Tier 2 playback audition; `baselineMap`; stable **`NoteId`** / **`EditorSelection`**; imperative overlap in **`NoteMovementUtils`** |
| **UIP** | **`IntervalProjection`** shipped — Edit projection API ready for geometry orchestrator |
| **OpenSpec** | **`edit-session-action-geometry`** design locked; **0.4 approved**; **no pipeline implementation code yet** |
| **Known bug** | Overlap target → select same note → `overlapNotes` self-collision (`session_20260804_180228`) — brownfield guard before Phase 4 wire |

---

## Brownfield fixes to preserve on wire (design.md § Brownfield interim)

When Phase 4 replaces `NoteMovementUtils` bodies, **do not regress**:

| Fix | Location |
|-----|----------|
| NOTELEN grace / fader 2–3 block | `NoteEditManager::toggleLengthEditingMode`, `requestFaderOutbound`, `completeOutboundPipelineAtDone` |
| Empty-step F2–F4 motor sync | `NoteEditFaderOutboundPlan.h`, `EditManager::applySelectNav`, `BarStepButtonHandler` |
| Playback audition after apply | `track.invalidateCaches()` → `sessionPreviewRevision_` (task **3.6**) |
| Length targets **`focus.movingNoteId`** | `liveEditDisplayNoteAtSelect` + focus paths until pipeline owns selection |

---

## Playback audition (confirmed intended)

Edited notes are **not** sent as immediate MIDI note-on per fader move. During NOTE_EDIT with transport playing, **`ensurePlaybackWindowBuilt`** uses **`sessionMidiEvents()`** (Tier 2). Pipeline **`applyEditSessionActions`** must call **`track.invalidateCaches()`** after mutating live store.

Ref: [`loop-wrap-projection`](../../openspec/changes/linear-loop-tick-storage/specs/loop-wrap-projection/spec.md), [`note_edit_geometry_wrap_regression_bugfix.md`](note_edit_geometry_wrap_regression_bugfix.md).

---

## OpenSpec status

| Phase | Status |
|-------|--------|
| **0** Docs + design | **Done** — 0.4 approved 2026-08-04; 0.5 DECISION_LOG on first code land |
| **1** Types + analyze + resolve | **Next** — no `EditSessionAction.h` in tree |
| **2–4** Builder, apply, wire | Not started |
| **5** HITL matrix (D14) | Not started; **`edit_minimal`** is smoke only |

Task **0.10** (brownfield interim in design) is checked in `tasks.md`.

---

## Start here (recommended order)

1. ~~**0.4** Confirm D1–D22 vocabulary~~ — **done 2026-08-04**
2. **Brownfield guard** — evict `overlapNotes[movingNoteId]` in `rebuildNoteEditFocusForDisplayNote` + native repro (`session_20260804_180228`)
3. **0.5** Append **`DECISION_LOG`** when landing **1.1**
4. **1.1** `include/EditSessionAction.h` — types only
5. **1.2a** Wire **Edit projection** (shipped UIP) — not new `normalizeWrapToLinear`
6. **1.3** `analyzeEditSessionInteractions` — pure geometry, no store mutation
7. **1.6** `test/test_edit_session_interaction/` — first matrix rows: **CompleteCover**, **OverlapNoteOff**, **selected-to-selected skip**
8. Then **1.4–1.5** group-by-target + **`resolveConstrainedGeometry`** + **1.7** resolver tests

**Do not start Phase 4 wire** until Phases 2–3 have native tests.

---

## Verification (green before sharing)

```bash
pio test -e native
pio test -e native -f test_note_edit_fader_feedback
pio test -e native -f test_note_edit_focus

openspec validate edit-session-action-geometry
```

Firmware build (ask before upload): `pio run -e teensy41-capture-serial`

---

## Explicitly deferred (this change)

| Item | Notes |
|------|--------|
| **`overlapNotes` removal** | Phase **4.5** only — brownfield still uses scratch until wire |
| **Selected-to-selected overlap when geometry changed** | Future multi-select length |
| **Poly shorten cross-pitch** | Q14 |
| **D14 full HITL matrix** | base + 2× overdub fixture; not **`edit_minimal`** alone |
| **D36 session bracket sync** | Separate fader-feedback track |
| **UIP 5.5 HITL** | Deferred DEC-017 — not a geometry Phase 1 blocker |

---

## Architecture constraints (do not violate)

- **Design-session change** — no new persistent overlap registry; **`resolveConstrainedGeometry`** is the central algorithm.
- **Analyzer is pure** — selection policy in orchestrator only (**D17**).
- **Edit session action builder** must not read **`EditSessionInteraction`** directly (**Invariant — Constrained Geometry Authority**).
- **Sole live-store writer** for geometry: **`applyEditSessionActions`** (Phase 3).
- **DEC-014:** **`normalizeAll`** at macro commit only; geometry tick uses boundary split in apply + **`normalizeWindow`** on edit closure.
- Extend **`NoteEditFocus`** / movement pair helpers — do not add parallel **`NoteMovementUtils`** overlap paths during migration.

---

## Key files today (brownfield)

| Area | Files |
|------|--------|
| Imperative overlap (replace in Phase 4) | `src/Utils/NoteMovementUtils.cpp` |
| Focus / baseline | `src/NoteEditFocus.cpp`, `include/NoteEditFocus.h` |
| Selection boundary (guard) | `src/EditManager.cpp` — `rebuildNoteEditFocusForDisplayNote` |
| Edit projection (D20) | `include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp` |
| Playback preview | `src/Track.cpp`, `src/EditManager.cpp` |
| HITL | `scripts/hitl/scenarios/layered/` — `edit_full` preset |
