# hitl-modular-scenarios — tasks

## 1. OpenSpec artifacts

- [x] 1.1 proposal, design, tasks, spec deltas

## 2. Track A — Modular runner

- [x] 2.1 `scripts/hitl/` package + scenario registry
- [x] 2.2 `scripts/host_midi_hitl.py` CLI (`--preset`, `--scenarios`, `--verify-serial-log`)
- [x] 2.3 Legacy scripts delegate to presets (thin `main` shim optional — presets via hitl)
- [x] 2.4 `edit_minimal` scenario stub (prelude + enter/exit markers)

## 3. Track B — Session undo overdub (firmware)

- [x] 3.1 `SessionUndoEntry.overdubPassIdAtPush`
- [x] 3.2 `pushSessionUndoAfterOverdubDuringNoteEdit` from `endOverdubSession`
- [x] 3.3 `sessionUndo` / `sessionRedo` disable/enable overdub pass + rematerialize
- [x] 3.4 Native test in `test_note_edit_session_undo`
- [x] 3.5 Serial log markers

## 4. Track C — edit_overdub_during_note_edit

- [x] 4.1 Phase runner in `scripts/hitl/scenarios/edit_overdub_during_note_edit.py`
- [x] 4.2 `_verify_edit_overdub_during_note_edit` verifier
- [x] 4.3 Wire scenario into registry + hitl CLI

## 5. Track D — Docs

- [x] 5.1 `docs/plans/hitl_modular_scenarios_enhancement.md`
- [x] 5.2 Update HITL cursor rules
- [x] 5.3 Merge `note-edit-session-undo` delta to main spec
- [x] 5.4 `openspec validate hitl-modular-scenarios`; `pio test -e native`
