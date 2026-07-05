# Unified interval projection — enhancement handoff

**Date:** 2026-07-05 (refined)  
**Branch:** `derived-note-overlap-logic`  
**OpenSpec:** [`openspec/changes/unified-interval-projection/`](../../openspec/changes/unified-interval-projection/)  
**Apply command:** `/opsx:apply unified-interval-projection`

---

## One-line goal

Replace duplicated wrap/linearization in display, playback, and edit with a **single interval projection engine**: canonical linear storage → `ProjectionContext` → generate equivalent intervals → select best interval → projected consumer input.

---

## Why before overlap

`edit-session-action-geometry` planned **`normalizeWrapToLinear`** as a third independent wrap path. UIP centralizes wrap math so overlap analyze receives normalized intervals only — **`wraps` retired**, no consumer-local unwrap.

**Blocks:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/) Phases 1–4 until UIP Phases 1–5 + HITL pass.

---

## Core principle

Storage is global; projection is local.

```
Canonical Note (NoteId, linear startTick, endTick)
        │
        ▼
ProjectionContext (origin, projectionCycleStartTick, loopStartTick, selectedTick, window, loopLength, type)
        │
        ▼
generateEquivalentIntervals → selectProjectedInterval
        │
        ├── projectDisplayNotes() → DisplayNote (head/tail adapter)
        └── projectNoteIntervals / projectEditIntervalsForAnalysis
```

Loop wrapping never mutates storage.

---

## Tick vocabulary

| Tick | Role |
|------|------|
| `currentTick` | Global transport (ClockManager) |
| `loopStartTick` | Persisted loop startpoint on `Loop` — derive projected ranges; Fader 1 LOOP_EDIT |
| `selectedTick` | Runtime select/bracket in projected interval — Fader 1/2/3, 16th buttons |
| `projectionCycleStartTick` | Rolling global cycle origin; `+= loopLength` on wrap |
| `queuedStartTick` | One-shot queued restart at grid commit (slot/bar/16th) |
| `startLoopTick` | Brownfield record/phase origin; maps during migration |

---

## Consumers (full-stack v1)

| Consumer | `ProjectionType` | Replaces |
|----------|------------------|----------|
| Playback | `Playback` | `ensurePlaybackWindowBuilt` ad-hoc `% loopLength` |
| Display | `Display` | `reconstructNotes` → **`projectDisplayNotes()`** |
| Edit | `Edit` | `normalizeWrapToLinear`, `resolveLinearNoteSpanForOverlap` |

**Reserved:** `Timeline` — long-loop piano-roll window (later).

---

## Module home

- `include/Utils/IntervalProjection.h`
- `src/Utils/IntervalProjection.cpp`
- Native: `test/test_interval_projection/`

---

## Delivery phases

See [`tasks.md`](../../openspec/changes/unified-interval-projection/tasks.md).

| Phase | Handoff |
|-------|---------|
| 2 — Edit projection | [`unified_interval_projection_phase2_edit_projection_handoff.md`](unified_interval_projection_phase2_edit_projection_handoff.md) (**shipped** 2026-07-05) |

---

## Agent execution notes (one chat per phase)

**Goal:** Keep context small — **start a new chat after each phase (or sub-phase) completes**. Composer 2.5 (or any agent) is sufficient when each session is scoped and tests gate progress.

### Session scope

| Session | OpenSpec tasks | Exit gate |
|---------|----------------|-----------|
| **1 — Core engine** | Phase 1 (1.1–1.6) | `pio test -e native -f test_interval_projection` green |
| **2 — Edit projection** | Phase 2 (2.1–2.5) | `pio test -e native -f test_note_edit_focus` wrap fixtures green |
| **3 — Display projection** | Phase 3 (3.1–3.5) | `test_noteutils_reconstruct` + `test_display_window_utils` green |
| **4 — Playback + cleanup** | Phase 4 (4.1–4.11) | Native playback-order test; grep: no `PlaybackCursor` |
| **5 — Integration** | Phase 5 (5.1–5.6) | Full `pio test -e native` + HITL Phase 5 checklist |
| **6 — Overlap unblock** | Phase 6 | Overlap OpenSpec synced; resume derived overlap handoff |
| **7+ — LTS / slices** | Phases 7–8 | One sub-phase per chat (7.0 review, 7.1 types, …) |

Do **not** span Phases 1→4 in one chat — context drift is the main failure mode.

### What to paste at session start

```
/opsx:apply unified-interval-projection
Phase N only — see tasks.md §N

Load:
- openspec/changes/unified-interval-projection/design.md (D-sections for this phase)
- openspec/changes/unified-interval-projection/tasks.md §N
- docs/plans/unified_interval_projection_enhancement.md (this file)
- docs/runtime/CURRENT_WORK.md + PROJECT_STATE.md
```

Add target test files and brownfield files listed in `tasks.md` for that phase.

### Before closing the chat

1. Run the phase exit gate (`pio test -e native` at minimum).
2. Mark completed tasks in [`tasks.md`](../../openspec/changes/unified-interval-projection/tasks.md).
3. Update [`CURRENT_WORK.md`](../runtime/CURRENT_WORK.md) if phase status changed.
4. Commit when ready (user request only).

### Human gates (agent cannot skip)

- **Architecture checkpoint** — ownership or state-transition change → design session, not patch.
- **Phase 5 HITL** — Teensy + serial capture; fix from logs in a follow-up chat if needed.
- **Formal triggers** — see [`ARCHITECTURE_REASSESSMENT.md`](../ARCHITECTURE_REASSESSMENT.md).

### Authority order

OpenSpec `design.md` + delta specs → `tasks.md` → this handoff → code. Tests are the runtime truth check.

---

## Key design decisions (D13–D23)

- **D13:** Full rolling `projectionCycleStartTick`; mid-cycle length change matches today
- **D14:** `queuedStartTick` at configurable grid; bar/slot mutually exclusive; last wins
- **D15:** Keep `loopStartTick`; add `selectedTick` for runtime selection (not a rename)
- **D16:** `projectDisplayNotes()` owned by IntervalProjection
- **D21:** Retire `PlaybackCursor` + `loopHeadWindow` (Phase 4)
- **D22:** Trim dead `PlaybackWindow` fields; refactor `DisplayWindowUtils` post-projection (Phases 3–4)
- **D23:** `window` is `TickInterval` frame — never `ProjectedNoteInterval`

Full decision log: [`design.md`](../../openspec/changes/unified-interval-projection/design.md) § Decisions.

---

## References

- [`design.md`](../../openspec/changes/unified-interval-projection/design.md)
- [`derived_note_overlap_logic_handoff.md`](derived_note_overlap_logic_handoff.md) — blocked until UIP Phases 1–5
