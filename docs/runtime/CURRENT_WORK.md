# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-06 (internal heap PSRAM routing)

---

## Now implementing

**Internal heap PSRAM routing** — shipped 2026-07-06:

- Plan: [internal_heap_psram_routing_refinement.md](../plans/internal_heap_psram_routing_refinement.md) · capture helper: `scripts/parse_memory_capture.py`
- UIP cold vectors (`CanonicalNoteSpanVec`, `ProjectedIntervalVec`, `buildCanonicalSpansFromMidi`) → external memory pool
- `BaselineMap` / `OverlapNoteMap` / session undo `EntryVec` → `ExternalMemoryFirstAllocator`
- `rebuildNoteEditFocusFromStore` — `baselineMap` holds moving note only (overlap baselines via `overlapNotes` + undo snapshot)
- `MemoryPool::globalMidiEventPool` + `DisplayManager::liveDisplayEventBuffer` → PSRAM-first
- Native **467/467** PASS · `teensy41-capture-serial` build OK · **HITL** (F2/F3 reselect heap gate) pending on-device

**Unified interval projection** — single wrap/linearization engine before derived overlap pipeline:

- **Primary OpenSpec:** [`unified-interval-projection`](../../openspec/changes/unified-interval-projection/) — full-stack Edit + Display + Playback migration (Phases 1–5)
- Handoff: [unified_interval_projection_enhancement.md](../plans/unified_interval_projection_enhancement.md) · Phase 2: [unified_interval_projection_phase2_edit_projection_handoff.md](../plans/unified_interval_projection_phase2_edit_projection_handoff.md)
- Design: [`design.md`](../../openspec/changes/unified-interval-projection/design.md)
- **Phase 1 shipped** (2026-07-05): `IntervalProjection` core engine + `test_interval_projection` (17 cases).
- **Phase 2 shipped** (2026-07-05): Edit projection (`buildEditProjectionContext`, `projectEditIntervalsForAnalysis`); `resolveLinearNoteSpanForOverlap` delegates to engine; `test_note_edit_focus` +4 parity cases (50 total).
- **Phase 3 shipped** (2026-07-05): Display projection (`projectDisplayNotes`, `buildCanonicalSpansFromMidi` → engine); `DisplayWindowUtils` + `DetailedWindowContext` on `TickInterval`; `test_noteutils_reconstruct` 17 cases, `test_display_window_utils` 8 cases.
- **Phase 4 shipped** (2026-07-05): Playback projection (`projectPlaybackEventPhase`, rolling `projectionCycleStartTick`, queued start at grid); retired `PlaybackCursor` / `loopHeadWindow`; `test_interval_projection` +3 playback-order cases.
- **Phase 5 in progress** (2026-07-05): grep gate (D7 consumers → `IntervalProjection` helpers); `isInflatedDisplaySpan` moved to engine; native suite 449/449 PASS; LOOP_MIDI guide NOTE_EDIT § updated. **Remaining:** 5.5 HITL matrix.

## Paused — blocked by UIP

**Derived note overlap logic** — EditSessionAction geometry pipeline:

- **OpenSpec:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/) — **blocked** until UIP Phases 1–5 + HITL
- Handoff: [derived_note_overlap_logic_handoff.md](../plans/derived_note_overlap_logic_handoff.md)
- Resume at Phase 1 types after UIP Phase 6 sync

## Paused on parent branch (`load-save-sets-loops`)

Set revision persistence + load/save overlay UX:

- **OpenSpec:** `set-revision-persistence` — remaining **4.8–4.10** loop picker + HITL `set_revision_overlay`; **3.9** parked
- Handoff: [set_revision_persistence_handoff.md](../plans/set_revision_persistence_handoff.md)
- Not blocking UIP (orthogonal)

## Recently archived (2026-07-03)

- **`note-edit-fader-feedback-regression`** — HITL §7.23.6 PASS; spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.
- **`note-edit-stable-note-id`** — Phase B shipped; spec `openspec/specs/note-edit-stable-note-id/`.
- Handoffs: [note_edit_fader_feedback_next_steps_handoff.md](../plans/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../plans/note_edit_stable_note_id_phase_a_handoff.md)

## Side fix (orthogonal to persistence track)

- **`note-edit-tick-coordinates-and-audition`** — geometry wrap regression during pitch edit on long loops; Tier 1+2 shipped in firmware; HITL pending. Handoff: [note_edit_geometry_wrap_regression_bugfix.md](../plans/note_edit_geometry_wrap_regression_bugfix.md)

## Parallel track (does not block UIP 5.5 HITL)

**Slot selection focus** — canonical Departure → Transition → Arrival lifecycle for slot switch; session-type-agnostic edit rebind; independent `selectedSlotIndex` persistence:

- **OpenSpec:** [`slot-selection-focus`](../../openspec/changes/slot-selection-focus/) — **firmware shipped 2026-07-05**; manual §8 pending
- Handoff: [`slot_selection_focus_implementation_handoff.md`](../plans/slot_selection_focus_implementation_handoff.md)
- Native: `test_slot_switch_edit_sessions` (6 cases); `pio test -e native` 455/455 PASS
- **Remaining:** manual verification matrix §8 (NOTE_EDIT / LOOP_EDIT slot switch stopped + playing, reboot indices)

## Explicitly NOT implementing

- D13 arrangement jam **capture** — future roadmap only ([ROADMAP.md](ROADMAP.md))
- `currentset-savedset-storage-layout` — parked; superseded by revision model
- JamRecorder, M10 Scenes, playback hardening — not this sprint
- New `*Manager` classes for persistence — extend `StorageManager` (DEC-008)
- **`edit-session-action-geometry` firmware** — until UIP complete

## Current target

1. **`unified-interval-projection` Phases 1–4** — **done** (core + Edit + Display + Playback projection)
2. **Phase 5** — grep gate, HITL, integration
3. **Phase 6** — sync overlap OpenSpec; resume derived overlap handoff

## Paused (persistence track)

1. Remaining `set-revision-persistence` **4.8–4.10** overlay loop picker
2. Optional: `transport.bin` / `global.bin` OpenSpec when user scopes post–DEC-012

## Completion conditions (DEC-012 — done)

- [x] Tier 0–3 per [storage_session_state_refactor handoff](../plans/storage_session_state_refactor_open_items_handoff.md)
- [x] HITL overlay matrix (MIDI watchable presets) — 2026-06-29
- [x] `pio test -e native` (292 tests)
- [x] Specs in `openspec/specs/{revision-load,storage-session-jobs,storage-session-layout}/`
- [x] OpenSpec archived: `openspec/changes/archive/2026-06-29-storage-session-state-refactor/`

## Blocked by

- **`edit-session-action-geometry`** blocked by **`unified-interval-projection`** Phases 1–5
