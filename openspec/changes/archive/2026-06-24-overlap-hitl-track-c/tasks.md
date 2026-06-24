# Overlap HITL Track C — tasks

**Change:** `overlap-hitl-track-c`  
**Evidence:** [BUG.md](./BUG.md) — capture `20260620_161431` (investigation); pass `20260623_232352`  
**Last updated:** 2026-06-24

| Gate | Status |
|------|--------|
| §0 Investigation | **Done** — BUG.md hypotheses H1–H3 |
| §1 Verifier fixes | **Done** — log replay green on `161431` |
| §2 Session undo pushes | **Done** — kind-boundary session stack + native move-back insert |
| §3 Sign-off | **Done** — native green + HITL verify replay `20260623_232352` |

---

## 0. Investigation — done

- [x] 0.1 Map three failing verifier groups to issue keys on `161431` JSON
- [x] 0.2 Serial trace insert move-over / move-back / restore logs (BUG.md Track 1)
- [x] 0.3 Serial trace overlap round-trip home: pre-move recon @112 vs post-move DNTE @16 (BUG.md Track 2)
- [x] 0.4 Confirm split-overlap shares Track 2 home snapshot (BUG.md Track 3)

---

## 1. Verifier fixes (HITL script)

- [x] 1.1 **Track 1A** — `_verify_delay_move_insert_reorder`: accept `Restoring hidden overlap note` (and/or inventory pitch 60 @ insert tick after move-back)
- [x] 1.2 **Track 2/3 H1** — `_snapshot_overlap_round_trip_home`: post-move inventory via `Moved note events` / `#CAP,DNTE` at `m0_tick`
- [x] 1.3 **Track 2** — Home checkpoint uses overlap round-trip home snapshot (before short-over-long B shorten)
- [x] 1.4 Log-replay gate: `161431` — all four sub-verifiers `ok: true`

---

## 2. Firmware + baseline — in-edit session undo/redo (Track 1B)

*User locked: in-edit undo = **NoteEditSessionUndoStack**.*

- [x] 2.1 `pushSessionUndoBeforeMutation` before coarse/fine move, length change, and pitch change in `NoteEditManager.cpp`
- [x] 2.2 Live HITL: confirm `EditSession undo` / `EditSession redo` serial markers on capture `20260623_232352` (session stack routing; not global pass undo during in-edit step)
- [x] 2.3 `_verify_delay_move_insert_reorder` in-edit redo: session markers + insert @ insert tick in post-redo recon (no note-count delta gate)
- [x] 2.4 Native: `test_edit_apply` session undo/redo before **saveNoteEditPass** for move-back insert scenario

---

## 3. Sign-off

- [x] 3.1 `pio test -e native` — all green (168/168)
- [x] 3.2 Live HITL edit baseline — verify replay on `captures/host_midi_automation_edit_baseline_20260623_232352_serial.log`; sub-verifiers all `ok: true`
- [x] 3.3 Update [BUG.md](./BUG.md) patch history with pass capture id `20260623_232352`
- [x] 3.4 `openspec validate overlap-hitl-track-c`; `/opsx:archive`

---

## Deferred (out of this change)

- `short_over_long_forward` / `short_over_long_restore` — AC5/AC6; separate if still false after §3
- `inner_a_contained_at_home` — archived lengthen-overlap follow-up
