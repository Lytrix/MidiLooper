# Tasks — change-length-commit-rematerialize

**Change:** `change-length-commit-rematerialize`  
**Design:** [design.md](./design.md) · **Evidence:** [BUG.md](./BUG.md)  
**Parent:** [note-edit-modification-session/tasks.md](../note-edit-modification-session/tasks.md) (PR4 **7.x**, **0.x**)

Run `/opsx:apply` on this file after parent overlap engine tasks **1.x–6.x** are complete.

---

## 0. Investigation (Track A)

- [x] 0.1 Reproduce on capture `195830`: grep `Edit committed ChangeLength` → next M0 `Final note` / REVT inventory; record line numbers in BUG.md.
- [x] 0.2 Trace length-mode commit path: `NoteEditManager` length off → `commitAllPendingNoteEditActions` → `commitEditAction` → `saveEdit` → `applyEditsToFlat` → `loadFromFlat` (confirm order on device).
- [x] 0.3 Compare **ChangeLength** **EditChange** `{ target, newEndTick }` in serial vs native `test_lengthen_commit_rematerialize_hitl_fixture` (channel, baseline end, new end).
- [x] 0.4 Rank H1–H5 in BUG.md; mark confirmed hypothesis when fix lands.
- [x] 0.5 Grep post-commit reconstruction emitter: `NoteUtils::reconstructNotes` via `Final note:` / `Reconstruction complete` — identify first snapshot used by verifier vs later session-scratch recon (see BUG.md § Serial trace).

---

## 1. Fix — ChangeLength rematerialize (Track A)

- [x] 1.1 Fix root cause in confirmed file(s) from §0 (minimal diff — `EditApply`, `EditManager`, and/or `Loop` only).
- [x] 1.2 Ensure post-commit `loopMidiEventsFromTakesAndEdits` reload matches `saveEdit` **Edits** (no stale session cache).
- [x] 1.3 Add native test mirroring full `commitEditAction` reload if host can reproduce device gap; keep `pio test -e native` green.
- [x] 1.4 Build `teensy41-capture-serial`; user-approved upload.

---

## 2. HITL — Track A sign-off

- [x] 2.1 Run edit baseline + serial capture; `--verify-serial-log` on new capture.
- [x] 2.2 `_verify_change_length_store_rebuild`: all `after_change_length_commit:*` and downstream parity issues empty (capture `203729`).
- [x] 2.3 Save capture JSON + serial under `captures/`; reference id in BUG.md patch history.

---

## 3. HITL — Track B (M0 home)

- [x] 3.0 **Gate:** post-commit M0 @ fixture home at record gate end after Track A fix — green on `203729`.
- [x] 3.1 Re-run same capture after Track A green: `_verify_long_over_short_pitch_restore` `m0_home_ok` true.
- [x] 3.2 `_verify_split_overlap_note_round_trip` `mover_at_home` true.
- [x] 3.3 Verifier checkpoint fix (not firmware): mid-move pitch uses over-step inventory; home native parity uses last `position_edit:*->home` snapshot (pre-move scratch recon skipped).

---

## 4. Parent / child bug closure

- [x] 4.1 Update parent task **7.1** status when Track A+B verifiers pass (note full baseline may still fail Track C).
- [x] 4.2 Close [lengthen-overlap-neighbor-restore/tasks.md](../lengthen-overlap-neighbor-restore/tasks.md) HITL gate rows tied to rematerialize + M0 home.
- [x] 4.3 Map [note-move-pitch-overlap-flaky/BUG.md](../note-move-pitch-overlap-flaky/BUG.md) AC4 in BUG patch history (AC5–AC7 remain open if still red).

---

## 5. Deferred (explicit — not this change)

- [ ] 5.1 **Track C** — parent **7.4** insert/reorder; new change if root cause ≠ Track A.
- [ ] 5.2 **Track D** — parent **0.1** P0 `1535` peak investigation.
- [ ] 5.3 Parent **7.2** verifier dedup; **7.5** `MOVE_NOTE_LOGIC.md` — after archive or in parallel.

---

## 6. Archive

- [ ] 6.1 Sign-off matrix in design.md satisfied (Track A + B **Yes** rows).
- [ ] 6.2 `/opsx:archive` this change; link archived spec in parent design if needed.

---

## Commands

```bash
pio test -e native
pio run -e teensy41-capture-serial
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy MIDI" --midi-in "Teensy MIDI" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 --record-bars 2 --start-transport \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 500 \
  --verify-serial-log captures/<capture>_serial.log
```
