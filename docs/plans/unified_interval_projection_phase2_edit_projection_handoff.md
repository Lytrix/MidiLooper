# Handoff — Unified interval projection Phase 2 (Edit projection)

**Date:** 2026-07-05  
**Branch:** `derived-note-overlap-logic`  
**OpenSpec:** [`openspec/changes/unified-interval-projection/`](../../openspec/changes/unified-interval-projection/) — tasks **2.1–2.6** complete  
**Parent handoff:** [`unified_interval_projection_enhancement.md`](unified_interval_projection_enhancement.md)  
**Apply command (next):** `/opsx:apply unified-interval-projection` — **Phase 3 only** (`tasks.md` §3)

---

## Status summary

| Phase | OpenSpec tasks | Status |
|-------|----------------|--------|
| **1 — Core engine** | 1.1–1.6 | **Shipped** (prior session) |
| **2 — Edit projection** | 2.1–2.6 | **Shipped** (this session) |
| **3 — Display projection** | 3.1–3.5 | **Next** |
| **4–5 — Playback + integration** | 4.x, 5.x | Not started |
| **6 — Overlap unblock** | 6.x | Blocked until Phases 3–5 + HITL |

**`edit-session-action-geometry` firmware:** still **blocked** — no `EditSessionAction.h`, no analyze/apply wire (task 2.6 honored).

---

## One-line outcome

Edit overlap linearization now runs through **`IntervalProjection`** (generate → select) instead of a parallel wrap path. Brownfield **`resolveLinearNoteSpanForOverlap`** resolves canonical storage, then delegates to **`projectEditLinearSpan`**.

---

## What shipped

### New API (`include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`)

| Symbol | Role |
|--------|------|
| `makeFullLoopEditAnalysisWindow(loopLength)` | v1 analysis frame `{0, loopLength}` |
| `buildEditProjectionContext(selection, loopLength, analysisWindow, primaryLinearStartTick, loopStartTick)` | `ProjectionType::Edit`; `selectedTick` ← `bracketTick`; `originTick` ← primary note **linear** start |
| `projectEditLinearSpan(span, context)` | Single-span generate → `selectProjectedInterval` |
| `projectEditIntervalsForAnalysis(spans, context)` | Batch Edit projection (forces `ProjectionType::Edit`) |

**Selection policy (D4):** Edit uses same Stage-2 rule as Playback — one interval closest to `context.originTick` (see `test_selection_edit_closest_to_origin` in `test_interval_projection`).

### Brownfield wiring

| Path | Change |
|------|--------|
| `resolveLinearNoteSpanForOverlap` (`src/NoteEditFocus.cpp`) | Canonical resolution unchanged (baselineMap → display → `findLinearNoteSpanForNoteId`); final span via **`projectEditLinearSpan`** with `originTick = dn.startTick` |
| `NoteMovementUtils::resolveOverlapNoteLinearSpan` | Unchanged — still falls through to `resolveLinearNoteSpanForOverlap` |

### D6 — `wraps` retirement

- No `EditSessionInteraction` type in firmware yet.
- Delta spec [`openspec/changes/unified-interval-projection/specs/edit-session-action-geometry/spec.md`](../../openspec/changes/unified-interval-projection/specs/edit-session-action-geometry/spec.md) **REMOVED** `wraps`; future overlap types must not add it back without design session.
- Full `edit-session-action-geometry/design.md` sync deferred to **Phase 6** (task 6.2).

### Tests added

| Suite | New cases | Total |
|-------|-----------|-------|
| `test_interval_projection` | `test_build_edit_projection_context_fields`, `test_project_edit_intervals_for_analysis_batch`, `test_project_edit_intervals_for_analysis_forces_edit_type` | 17 |
| `test_note_edit_focus` | `test_edit_projection_context_*`, `test_edit_projection_batch_*`, `test_edit_projection_parity_*` (×2) | 46 |

**Native link rule:** any test TU that `#include`s `NoteEditFocus.cpp` must also `#include` `IntervalProjection.cpp` (applied in `test_edit_apply`, `test_note_edit_session_undo`, `test_note_edit_focus`; `test_note_edit_fader_feedback` already had it).

---

## Verification (2026-07-05)

```bash
pio test -e native -f test_note_edit_focus    # 46/46 PASS
pio test -e native -f test_interval_projection # 17/17 PASS
pio test -e native                             # 439/441 — see below
```

**Pre-existing native noise (not introduced by Phase 2):**

| Suite | Issue |
|-------|--------|
| `test_faders` | Empty / nothing-to-build |
| `test_note_overlap_engine` | Empty / nothing-to-build |

All other native suites green after `IntervalProjection.cpp` link fix.

---

## Explicitly deferred (do not do in Phase 3 by accident)

| Item | Owner phase |
|------|-------------|
| `projectDisplayNotes()` / `reconstructNotes` delegate | **Phase 3** |
| `resolveLinearNoteSpanForOverlap` full retirement / thin-wrap grep gate | **Phase 5.1** |
| `isInflatedDisplaySpan` edit-path retirement | **Phase 5.1** |
| Overlap orchestrator calling `projectEditIntervalsForAnalysis` at geometry tick | **`edit-session-action-geometry`** after UIP Phase 5 |
| `PlaybackCursor` retirement | **Phase 4.10** |
| HITL UIP matrix | **Phase 5.5** |

---

## Start here — Phase 3 (Display projection)

**Load:**

- `openspec/changes/unified-interval-projection/design.md` — Display projection vs rendering (coordinate model §7), D16, D22
- `openspec/changes/unified-interval-projection/tasks.md` §3
- `docs/plans/unified_interval_projection_enhancement.md`
- Brownfield: `src/Utils/NoteUtils.cpp` (`reconstructNotes`), `include/Utils/DisplayWindowUtils.h`

**Tasks:**

1. **3.1** `projectDisplayNotes()` — Stage 2 display selection + head/tail rendering + live capture open-tail
2. **3.2** Thin `reconstructNotes` → delegate; preserve outward `DisplayNote` API
3. **3.3** `DisplayWindowUtils` → `TickInterval` intersection on post-projection list
4. **3.3a** `DetailedWindowContext` → `TickInterval`
5. **3.4–3.5** Extend `test_noteutils_reconstruct`, `test_display_window_utils`

**Exit gate:**

```bash
pio test -e native -f test_noteutils_reconstruct
pio test -e native -f test_display_window_utils
pio test -e native
```

---

## Paste block — next session

```
/opsx:apply unified-interval-projection
Phase 3 only — tasks.md §3 (Display projection)

Branch: derived-note-overlap-logic

Load:
- openspec/changes/unified-interval-projection/design.md — Display sections, D16, D22
- openspec/changes/unified-interval-projection/tasks.md §3
- docs/plans/unified_interval_projection_phase2_edit_projection_handoff.md
- docs/plans/unified_interval_projection_enhancement.md

Brownfield:
- src/Utils/NoteUtils.cpp (reconstructNotes)
- include/Utils/DisplayWindowUtils.h
- test/test_noteutils_reconstruct/
- test/test_display_window_utils/

Do NOT start edit-session-action-geometry firmware.
```

---

## References

- [`design.md`](../../openspec/changes/unified-interval-projection/design.md) — D4, D5, D6, Edit projection API
- [`derived_note_overlap_logic_handoff.md`](derived_note_overlap_logic_handoff.md) — still paused until UIP Phases 1–5 + Phase 6 sync
- Runtime: [`CURRENT_WORK.md`](../runtime/CURRENT_WORK.md), [`PROJECT_STATE.md`](../runtime/PROJECT_STATE.md)
