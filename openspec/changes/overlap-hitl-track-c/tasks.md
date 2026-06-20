# Overlap HITL Track C — tasks

**Change:** `overlap-hitl-track-c`  
**Evidence:** [BUG.md](./BUG.md) — capture `20260620_161431`  
**Last updated:** 2026-06-20

| Gate | Status |
|------|--------|
| §0 Investigation | **Done** — BUG.md hypotheses H1–H3 |
| §1 Verifier fixes | **Done** — log replay green on `161431` |
| §2 Session undo pushes | **Partial** — `pushSessionUndoBeforeMutation` on move/length/pitch; live HITL pending |
| §3 Sign-off | **Open** |

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
- [ ] 2.2 Live HITL: confirm `NoteEditSession undo` / `redo` serial markers (not global pass undo) on next device run
- [x] 2.3 `_verify_delay_move_insert_reorder` in-edit redo: session markers + insert @ insert tick in post-redo recon (no note-count delta gate)
- [ ] 2.4 Native: `test_edit_apply` session undo/redo before **saveNoteEditPass** for move-back insert scenario

---

## 3. Sign-off

- [ ] 3.1 `pio test -e native` — all green
- [ ] 3.2 Live HITL edit baseline — `serial_verification.edit.ok` true; sub-verifiers `insert_reorder`, `long_over_short_pitch`, `change_length_store`, `split_overlap_note_round_trip` all `ok: true`
- [ ] 3.3 Update [BUG.md](./BUG.md) patch history with pass capture id
- [ ] 3.4 `openspec validate overlap-hitl-track-c`; `/opsx:archive` when §1–§3 green

---

## Suggested `/opsx:apply` slices

```
/opsx:apply overlap-hitl-track-c — §1.1 insert restore log alias
/opsx:apply overlap-hitl-track-c — §1.2 home snapshot post-move selection
/opsx:apply overlap-hitl-track-c — §2 pass redo rematerialize
/opsx:apply overlap-hitl-track-c — §3 HITL sign-off
```

---

## Deferred (out of this change)

- `short_over_long_forward` / `short_over_long_restore` — AC5/AC6; separate if still false after §3
- `inner_a_contained_at_home` — archived lengthen-overlap follow-up
- In-edit undo semantics change (session stack vs global pass) — only if user rejects design §Open Q2 default
