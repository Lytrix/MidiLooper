# Tasks — note-edit-modification-session

**Change:** `note-edit-modification-session`  
**Design:** [design.md](./design.md) (structs, overlap shorten, test matrix)

**Evidence / AC:** [note-move-pitch-overlap-flaky/BUG.md](../note-move-pitch-overlap-flaky/BUG.md),
[lengthen-overlap-neighbor-restore/BUG.md](../lengthen-overlap-neighbor-restore/BUG.md)

---

## 0. Investigation (before PR 4 HITL gate)

- [ ] 0.1 P0 `original_end=1535` @592 in capture `144458`: valid AC vs verifier artifact — update BUG.md; demote or fix gate in `_verify_note_length_integrity`.
- [ ] 0.2 Trace `144458` serial: document `overlapNotes` / `deletedNotes` lifecycle at pitch / move-past / home (evidence doc in BUG patch history).

---

## 1. Focus + baseline at select (A1 foundation)

- [x] 1.1 Add `NoteEditFocus` on `NoteEditSession` per design § Data structures.
- [x] 1.2 On fader-1 select: rebuild `baselineMap` from store; set `commitBaseline`, `overlapFootprint`, `last`, clear `overlapNotes`.
- [x] 1.3 Length edit: update `overlapFootprint.end` only; not `commitBaseline.end`.
- [x] 1.4 Native (`test_note_edit_focus`): A1 split after length edit.

---

## 2. Overlap notes (one owner state)

- [x] 2.1 Add `OverlapNote` + `OverlapNoteStoreState`; replace `deletedNotes` / `sessionDeletedNotes` / `sessionShortenedVictims`.
- [x] 2.2 On first impact: copy from `baselineMap`; set **Hidden** or **Shortened** per design § Overlap note shortening.
- [x] 2.3 Implement state transition table (design § `overlapNotes` state transitions).
- [x] 2.4 Retire duplicate pitch overlap in `NoteEditManager`.

---

## 3. Unified overlap engine

- [x] 3.1 `NoteMovementUtils::applyNoteEditChange` (move | length | pitch) — single entry for overlap classify + apply.
- [x] 3.2 `restoreOverlapNotesNoLongerOverlapping()` after every step; patch store for `overlapNotes` + moving note only.
- [x] 3.3 Inner vs lane merge uses `overlapFootprint`.
- [x] 3.4 Native: shared-release A@403–496 + lengthened M0 @496.
- [x] 3.5 Native: shorten head → move away → full gate restored; shorten then &lt;49 ticks → **Hidden**.

---

## 4. Pre-commit resolve (B1)

- [ ] 4.1 Resolve `overlapNotes` → store (impacted entries only) before `commitPending*`.
- [ ] 4.2 Build ordered `EditChangeList` per design § Pre-commit emission.
- [ ] 4.3 Wire commit boundaries table (reselect, exit edit, overdub start).
- [ ] 4.4 Native: pre-commit store diff ⊆ mover + `overlapNotes`.

---

## 5. EditApply replay parity

- [ ] 5.1 Native (`test_note_edit_focus` + `test_edit_apply`): lengthen → move over P0 → pitch → move back.
- [ ] 5.2 Fix `EditApply` ordering if replay drifts.
- [ ] 5.3 `test_edit_apply`: overlap **DeleteNote** + **ChangeLength** before **MoveNote** / **ChangePitch**.

---

## 6. Native test suite cleanup

- [x] 6.1 Add `test/test_note_edit_focus/` (Unity) — primary focus/overlap tests.
- [ ] 6.2 After 6.1 green: remove `test_delete_restore`, `test_shorten_delete_restore`; add `test_ignore` or delete folders.
- [ ] 6.3 `pio test -e native` green.

---

## 7. HITL + docs

- [ ] 7.1 HITL edit baseline + `--verify-serial-log`: `_verify_long_over_short_pitch_restore`, `_verify_change_length_store_rebuild`.
- [ ] 7.2 Review dedup `_verify_split_victim_round_trip` vs long-over-short (merge or keep one).
- [ ] 7.3 Rename HITL issue keys `*_victim_*` → `*_overlap_note_*` (script only; keep aliases one release).
- [ ] 7.4 `_verify_delay_move_insert_reorder`: keep unless PR 4 proves same root cause.
- [ ] 7.5 `docs/Guides/MOVE_NOTE_LOGIC.md` — baseline at select, overlap note shorten table, A1/B1.

---

## 8. Follow-up (not PR 1–4)

- [ ] 8.1 `EditStartNoteState` encoder path → `applyNoteEditChange` (DRY with faders).

---

## PR slice order

| PR | Tasks |
|----|--------|
| 1 | 1.x + 2.1 (structs, baseline at select, shell) |
| 2 | 2.2–2.4, 3.x (overlapNotes + engine) |
| 3 | 4.x (pre-commit) |
| 4 | 5.x, 6.x, 7.x, 0.x |

## Dependencies

- **m8-edit** `NoteEditSession.store` + `commitEditAction`.
- **edit-record-display-length-mode** — orthogonal; HITL P0 stretch may fail until that lands.

## Out of scope

- CC / velocity in `overlapNotes`.
- Insert/reorder unless task 7.4 reclassifies root cause.
