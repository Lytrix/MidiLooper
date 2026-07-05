# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-04 (derived-note-overlap-logic branch)

---

## Now implementing

**Unified interval projection** — single wrap/linearization engine before derived overlap pipeline:

- **Primary OpenSpec:** [`unified-interval-projection`](../../openspec/changes/unified-interval-projection/) — full-stack Edit + Display + Playback migration (Phases 1–5)
- Handoff: [unified_interval_projection_enhancement.md](../plans/unified_interval_projection_enhancement.md)
- Design: [`design.md`](../../openspec/changes/unified-interval-projection/design.md)

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

## Explicitly NOT implementing

- D13 arrangement jam **capture** — future roadmap only ([ROADMAP.md](ROADMAP.md))
- `currentset-savedset-storage-layout` — parked; superseded by revision model
- JamRecorder, M10 Scenes, playback hardening — not this sprint
- New `*Manager` classes for persistence — extend `StorageManager` (DEC-008)
- **`edit-session-action-geometry` firmware** — until UIP complete

## Current target

1. **`unified-interval-projection` Phase 0** — OpenSpec sign-off; DECISION_LOG on first code land
2. **Phases 1–5** — core engine, Edit/Display/Playback migration, HITL
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
