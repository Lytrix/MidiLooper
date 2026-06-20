# Tasks — note-edit-modification-session

**Change:** `note-edit-modification-session`  
**Status:** **Closed** — A1/B1 overlap engine + native matrix shipped (2026-06-20)  
**Archived:** `openspec/changes/archive/2026-06-20-note-edit-modification-session/`  
**Design:** [design.md](./design.md) (structs, overlap shorten, test matrix)

**Evidence / AC:** [note-move-pitch-overlap-flaky/BUG.md](../note-move-pitch-overlap-flaky/BUG.md),
[lengthen-overlap-neighbor-restore/BUG.md](../lengthen-overlap-neighbor-restore/BUG.md)

---

## 0. Investigation (before PR 4 HITL gate)

**Rematerialize bug plan:** [change-length-commit-rematerialize](../change-length-commit-rematerialize/) (Track A/B **signed off** on capture `203729`; §4 closure complete).

- [ ] 0.1 P0 `original_end=1535` @592 — deferred to **change-length-commit-rematerialize** / HITL verifier
- [ ] 0.2 Trace `144458` serial — deferred (evidence doc optional)

---

## 1. Focus + baseline at select (A1 foundation)

- [x] 1.1 Add `NoteEditFocus` on `NoteEditSession` per design § Data structures.
- [x] 1.2 On fader-1 select: rebuild `baselineMap` from store; set `commitBaseline`, `movingNoteRange`, `last`, clear `overlapNotes`.
- [x] 1.3 Length edit: update `movingNoteRange.end` only; not `commitBaseline.end`.
- [x] 1.4 Native (`test_note_edit_focus`): A1 split after length edit.

---

## 2. Overlap notes (one owner state)

- [x] 2.1 Add `OverlapNote` + `OverlapNoteStoreState`; **overlapNotes** on fader / `applyNoteEditChange` path (partial — `movingNote.deletedNotes` + unused `sessionHiddenOverlapNotes` / `sessionShortenedOverlapNotes` remain until **8.1**).
- [x] 2.2 On first impact: copy from `baselineMap`; set **Hidden** or **Shortened** per design § Overlap note shortening.
- [x] 2.3 Implement state transition table (design § `overlapNotes` state transitions).
- [x] 2.4 Retire duplicate pitch overlap in `NoteEditManager`.

---

## 3. Unified overlap engine

- [x] 3.1 `NoteMovementUtils::applyNoteEditChange` (move | length | pitch) — single entry for overlap classify + apply.
- [x] 3.2 `restoreOverlapNotesNoLongerOverlapping()` after every step; patch store for `overlapNotes` + moving note only.
- [x] 3.3 Inner vs lane merge uses `movingNoteRange`.
- [x] 3.4 Native: shared-release A@403–496 + lengthened M0 @496.
- [x] 3.5 Native: shorten head → move away → full gate restored; shorten then &lt;49 ticks → **Hidden**.

---

## 4. Pre-commit resolve (B1)

- [x] 4.1 Resolve `overlapNotes` → store (impacted entries only) before `commitAllPendingNoteEditActions`.
- [x] 4.2 Build ordered `EditChangeList` per design § Pre-commit emission.
- [x] 4.3 Wire commit boundaries table (reselect, exit edit, overdub start).
- [x] 4.4 Native: pre-commit store diff ⊆ mover + `overlapNotes`.

---

## 5. EditApply replay parity

- [x] 5.1 Native (`test_note_edit_focus` + `test_edit_apply`): lengthen → move over P0 → pitch → move back.
- [x] 5.2 Fix `EditApply` ordering if replay drifts.
- [x] 5.3 `test_edit_apply`: overlap **DeleteNote** + **ChangeLength** before **MoveNote** / **ChangePitch**.

---

## 6. Native test suite cleanup

- [x] 6.1 Add `test/test_note_edit_focus/` (Unity) — primary focus/overlap tests.
- [x] 6.2 After 6.1 green: remove `test_delete_restore`, `test_shorten_delete_restore`; add `test_ignore` or delete folders.
- [x] 6.3 `pio test -e native` green.

---

## 7. HITL + docs

**Track A/B (ChangeLength rematerialize + M0 home):** [change-length-commit-rematerialize/tasks.md](../change-length-commit-rematerialize/tasks.md).

- [x] 7.1 HITL edit baseline + `--verify-serial-log`: `_verify_long_over_short_pitch_restore`, `_verify_change_length_store_rebuild`. **Partial (Track A+B)** — green on capture `203729` (see [change-length-commit-rematerialize/tasks.md](../change-length-commit-rematerialize/tasks.md) §2–§4). Full `edit.ok` still false: **Track C** insert/reorder (7.4).
- [x] 7.2 Review dedup `_verify_split_overlap_note_round_trip` — deferred (script keeps both verifiers)
- [x] 7.4 `_verify_delay_move_insert_reorder` — deferred to **change-length-commit-rematerialize** Track C
- [ ] 7.5 `docs/Guides/MOVE_NOTE_LOGIC.md` — deferred doc refresh

---

## 8. Follow-up (not PR 1–4)

- [x] 8.1 `EditStartNoteState` encoder path → `applyNoteEditChange` (DRY with faders). Shipped in archived [note-edit-focus-reads](../archive/2026-06-20-note-edit-focus-reads/) **2b** (2026-06-19).

## 9. Session consumer contract (child change)

**Owner:** [overlap-hidden-note-select](../archive/2026-06-19-overlap-hidden-note-select/) — Phase 1 archived 2026-06-19.

- [x] 9.1 Phase 1: **`filterSelectableDisplayNotes`** — display, select, delete (HITL **`233328`**, AC1–AC5).
- [x] 7.3 Rename HITL issue keys `*_victim_*` → `*_overlap_note_*` (script only; keep aliases one release).
- [x] 9.2 Phase 2 complete (**2d**). HITL recheck → archived **note-edit-hitl-focus-restore** (**B1** + **B2**).
- [x] 9.3 Interim spot-fix debt documented in archived overlap architecture-review
- [x] 9.4 [CLARIFICATIONS.md](../archive/2026-06-19-overlap-hidden-note-select/CLARIFICATIONS.md) **C1–C13** locked (2026-06-19).

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
