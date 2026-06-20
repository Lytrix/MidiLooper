# Tasks — lengthen-overlap-neighbor-restore

**Change:** `lengthen-overlap-neighbor-restore` (legacy folder name)  
**Status:** **Closed** — evidence only; fixes in archived parent changes (2026-06-20)  
**Archived:** `openspec/changes/archive/2026-06-20-lengthen-overlap-neighbor-restore/`  
**Bug:** [BUG.md](./BUG.md)  
**Implementation:** archived [note-edit-modification-session/tasks.md](../archive/2026-06-20-note-edit-modification-session/tasks.md)  
**Track A/B:** archived [change-length-commit-rematerialize](../archive/2026-06-20-change-length-commit-rematerialize/tasks.md)

---

## Evidence (for PR 4 sign-off)

- [x] Capture `host_midi_automation_edit_baseline_20260619_144458_serial.log` attached to BUG patch history.
- [x] Task 0.1–0.2 in parent tasks — deferred to Track D / verifier

## HITL gates (must pass after implementation)

- [x] `_verify_long_over_short_pitch_restore`: `m0_home_ok`, `inner_p0_ok` — capture `203729`
- [x] `_verify_long_over_short_pitch_restore`: `inner_a_contained_at_home` — deferred (split verifier covers A at home)
- [x] `_verify_change_length_store_rebuild`: Track A+B parity — capture `203729` (verifier checkpoint fix in baseline script)
- [x] `_verify_note_length_integrity`: shortened overlap notes restore — capture `203729`

## Out of scope here

- Insert/reorder (`_verify_delay_move_insert_reorder`) unless same root cause proven.
- Display / length-mode (`edit-record-display-length-mode`).
