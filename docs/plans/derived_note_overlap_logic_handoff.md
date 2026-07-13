# Handoff — Derived note overlap logic (EditSessionAction pipeline)

**Date:** 2026-07-04  
**Branch:** `derived-note-overlap-logic`  
**Baseline commit:** `bbc2284` (OpenSpec starting point) · parent `e2f089b` (note-edit fader + edit_minimal HITL)  
**Status:** **PAUSED** — blocked by [`unified-interval-projection`](../../openspec/changes/unified-interval-projection/) Phases 1–5 + HITL  
**OpenSpec:** [`openspec/changes/edit-session-action-geometry/`](../../openspec/changes/edit-session-action-geometry/)  
**Apply command:** `/opsx:apply edit-session-action-geometry` — **after UIP Phase 6 sync** (was: Phase 0 closeout, then tasks 1.1–1.6)

**Prerequisite:** [`unified_interval_projection_enhancement.md`](unified_interval_projection_enhancement.md) — Edit projection replaces planned `normalizeWrapToLinear`

**Design detail:** [`note_edit_session_action_geometry_enhancement.md`](note_edit_session_action_geometry_enhancement.md)  
**Prior art (Q1–Q16):** [`edit_session_action_geometry_prior_art_refinement.md`](edit_session_action_geometry_prior_art_refinement.md)

---

## One-line goal

Replace imperative **`NoteMovementUtils`** overlap chains and **`overlapNotes`** scratch with a **derived** pipeline each geometry tick: **transaction baseline + edited geometry → analyze → group by target → `resolveConstrainedGeometry` → `buildEditSessionActions` → `applyEditSessionActions`**.

---

## Branch baseline (what you inherit)

| Layer | Shipped on this branch |
|-------|-------------------------|
| **Set/revision persistence** | REVPK02 commit/load, overlay browser, revision HITL (`revision_load_record`, etc.) — core path done; overlay loop picker **4.8–4.10** still open on `load-save-sets-loops` |
| **Note edit brownfield** | Tier 2 playback audition (`sessionMidiEvents` + `sessionPreviewRevision_`); linear overlap spans / `baselineMap`; stable **`NoteId`** / **`EditorSelection`** |
| **Fader feedback (2026-07-04)** | NOTELEN no longer blocks fader 2/3; empty-step fader 1 does not sync F2–F4; `edit_minimal` HITL PASS |
| **OpenSpec (this commit)** | Full **`edit-session-action-geometry`** change folder + plan docs; **no implementation code yet** |

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
| **0** Docs + design | Mostly done; **0.4** user sign-off, **0.5** DECISION_LOG on first code land, **0.2–0.3** handoff/supersede optional |
| **1** Types + analyze + resolve | **Not started** — no `EditSessionAction.h` in tree |
| **2–4** Builder, apply, wire | Not started |
| **5** HITL matrix (D14) | Not started; **`edit_minimal`** is smoke only |

Task **0.10** (brownfield interim in design) is checked in `tasks.md`.

---

## Start here (recommended order)

1. **0.4** Confirm D1–D22 + **`InteractionType`** / **`EditSessionAction`** vocabulary (design locked in session).
2. **0.5** Append **`DECISION_LOG`** when landing **1.1**.
3. **1.1** `include/EditSessionAction.h` — types only.
4. **1.3** `analyzeEditSessionInteractions` — pure geometry, no store mutation.
5. **1.6** `test/test_edit_session_interaction/` — first matrix rows: **CompleteCover**, **OverlapNoteOff**, **selected-to-selected skip**.
6. Then **1.4–1.5** group-by-target + **`resolveConstrainedGeometry`** + **1.7** resolver tests.

**Do not start Phase 4 wire** until Phases 2–3 have native tests.

---

## Verification (green before sharing)

```bash
pio test -e native
pio test -e native -f test_note_edit_fader_feedback
pio test -e native -f test_note_edit_focus

# HITL smoke (hardware + serial):
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_minimal \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --loop-slot 2 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120

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
| **D36 session bracket sync** | Separate fader-feedback track (`setBracketTick` vs `sessionState.selection.bracketTick`) |
| **`set-revision-persistence` 4.8–4.10** | Orthogonal; merge **`load-save-sets-loops`** separately if needed |

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
| Fader / outbound | `src/NoteEditManager.cpp`, `include/Utils/NoteEditFaderOutboundPlan.h` |
| Playback preview | `src/Track.cpp` (`ensurePlaybackWindowBuilt`), `src/EditManager.cpp` |
| HITL | `scripts/hitl/scenarios/edit_minimal.py`, `scripts/test_edit_minimal_serial_verify.py` |

---

## Push / merge notes

```bash
git push -u origin derived-note-overlap-logic
```

Branch history includes full **`load-save-sets-loops`** stack (persistence + note-edit fixes). Persistence overlay **4.8–4.10** can land on parent branch without blocking this pipeline work.
