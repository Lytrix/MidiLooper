# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-02 (note-edit-stable-note-id Phase B shipped)

---

## Now implementing

Set revision persistence + load/save overlay UX on branch `load-save-sets-loops`:

- **Primary OpenSpec:** `set-revision-persistence` (remaining overlay items; LoopPick parked)
- Supporting: `load-save-overlay-display-regression`, `save-status-display`, `workspace-session-persistence`
- **Next persistence slice (after branch merge):** `transport.bin` / `global.bin` workspace split — new OpenSpec when scoped
- Handoff: [set_revision_persistence_handoff.md](../plans/set_revision_persistence_handoff.md)

## Registered — not yet implementing

- **`note-edit-stable-note-id`** — Phase B **shipped**: `NoteId` on `MidiEvent`, `EditorSelection`, SD v6, co-location complete. Native 336 PASS; HITL NoteId sweep + §7.17.8 capture pending device.
  - Handoff: [note_edit_stable_note_id_phase_a_handoff.md](../plans/note_edit_stable_note_id_phase_a_handoff.md)
- **`note-edit-fader-feedback-regression`** — Phase A selection refactor shipped (§7.16); §7.17 F4 session-entry fix shipped; **Phase 8 RC11** (F2 loop-relative tick) still open.
  - Handoff: [note_edit_fader_feedback_phase8_handoff.md](../plans/note_edit_fader_feedback_phase8_handoff.md)

## Explicitly NOT implementing

- D13 arrangement jam **capture** — future roadmap only ([ROADMAP.md](ROADMAP.md))
- `currentset-savedset-storage-layout` — parked; superseded by revision model
- JamRecorder, M10 Scenes, playback hardening — not this sprint
- New `*Manager` classes for persistence — extend `StorageManager` (DEC-008)

## Current target

1. Remaining `set-revision-persistence` `tasks.md` items (overlay slice)
2. Optional: `transport.bin` / `global.bin` OpenSpec when user scopes post–DEC-012

## Completion conditions (DEC-012 — done)

- [x] Tier 0–3 per [storage_session_state_refactor handoff](../plans/storage_session_state_refactor_open_items_handoff.md)
- [x] HITL overlay matrix (MIDI watchable presets) — 2026-06-29
- [x] `pio test -e native` (292 tests)
- [x] Specs in `openspec/specs/{revision-load,storage-session-jobs,storage-session-layout}/`
- [x] OpenSpec archived: `openspec/changes/archive/2026-06-29-storage-session-state-refactor/`

## Blocked by

- None for revision/overlay track
