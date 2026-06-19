# Tasks — lengthen-overlap-neighbor-restore

**Change:** `lengthen-overlap-neighbor-restore` (legacy folder name)  
**Bug:** [BUG.md](./BUG.md)  
**Implementation:** [note-edit-modification-session/tasks.md](../note-edit-modification-session/tasks.md)

All firmware tasks live in the parent change. **Track A/B bug plan:** [change-length-commit-rematerialize](../change-length-commit-rematerialize/tasks.md). This file is **evidence + HITL AC only**.

---

## Evidence (for PR 4 sign-off)

- [ ] Capture `host_midi_automation_edit_baseline_20260619_144458_serial.log` attached to BUG patch history.
- [ ] Task 0.1–0.2 in parent tasks (P0 `1535`, serial trace).

## HITL gates (must pass after implementation)

- [x] `_verify_long_over_short_pitch_restore`: `m0_home_ok`, `inner_p0_ok` — capture `203729`
- [ ] `_verify_long_over_short_pitch_restore`: `inner_a_contained_at_home` — still false on `203729` (A visible at home via split verifier; not rematerialize/M0-home scope)
- [x] `_verify_change_length_store_rebuild`: Track A+B parity — capture `203729` (verifier checkpoint fix in baseline script)
- [x] `_verify_note_length_integrity`: shortened overlap notes restore — capture `203729`

## Out of scope here

- Insert/reorder (`_verify_delay_move_insert_reorder`) unless same root cause proven.
- Display / length-mode (`edit-record-display-length-mode`).
