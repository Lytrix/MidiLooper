# Tasks — note edit focus reads (Phase 2a–2d)

**Change:** `note-edit-focus-reads`  
**Status:** **Closed** — Phase 2a–2d shipped; native 110/110 (2026-06-20)  
**Archived:** `openspec/changes/archive/2026-06-20-note-edit-focus-reads/`  
**Handoff:** [HANDOFF-BRIEF.md](./HANDOFF-BRIEF.md)  
**Locks:** [PRE-EXECUTION.md](./PRE-EXECUTION.md)

---

## 2a.1 — `NoteEditManager` fader identity → **focus.last**

- [x] Coarse / fine / pitch fader handlers: build live **DisplayNote** from **`focus.last`**
  when `focus.active`; else filtered `selectableDisplayNotesForEditUi[selectedIdx]`
- [x] Remove `if (editManager.movingNote.active) { currentNote = movingNote... }` blocks
- [x] `moveNoteToPositionWithOverlapHandling`: drop `seedMovingNoteForOverlapEdit` when
  `focus.active`; use **bridge** `syncMovingNoteFromFocus` once before overlap util call only

## 2a.2 — `resolveNoteIdxAtSlot` cleanup

- [x] Delete **movingNote** fallback (lines ~1077–1095 in `NoteEditManager.cpp`)
- [x] When `!focus.active`: `candidates.front()` (**C17** from archive CLARIFICATIONS)

## 2a.3 — Select / mode / delete clears

- [x] Replace `movingNote.active = false` + `deletedNotes.clear()` on select with focus-only
  policy where Phase 1 already clears focus; do not add new **movingNote** writes
- [x] Length-mode toggle: stop `seedMovingNoteForOverlapEdit`; rely on **focus.last** after
  `commitAllPendingNoteEditActions`

## 2a.4 — `MidiFaderProcessor`

- [x] Fader switch commit: `focus.active` instead of `movingNote.active`
- [x] `commitMovingNote`: match note via **focus.last** + `selectableDisplayNotesForEditUi`
  (or no-op if !focus.active) — not `getCachedNotes` + **movingNote**

## 2a.5 — Retire redundant sync helpers

- [x] Remove **`syncFocusLastFromMovingNote`** if zero callers after 2a.1–2a.4
- [x] Keep **`syncMovingNoteFromFocus`** as **bridge only** (document in `EditManager.h`)
- [x] Remove **`seedMovingNoteForOverlapEdit`** only if no callers remain; else leave for 2c
  (delegates to **bridge**; encoder path until 2b)

## Sign-off

- [x] `pio test -e native` — 110/110 pass
- [x] Grep gate PRE-EXEC §2 — 0 UI geometry reads in `NoteEditManager.cpp`; 0 `movingNote` in `MidiFaderProcessor.cpp`
- [x] `verify_overlap_hidden_ac.py` on `233328` serial log (AC1–AC5 pass)
- [x] Optional: one smoke edit-baseline run (not full parent green) — **`235109`**: AC3/AC5 fail; see [edit-focus-selection-drift/BUG.md](../edit-focus-selection-drift/BUG.md) (recheck after 2b–2d)

---

## Explicitly deferred

- HITL focus/restore bugs → archived **note-edit-hitl-focus-restore** (shipped 2026-06-20)

---

## 2b — Encoder port (parent §8.1)

- [x] 2b.1 `EditStartNoteState::onEncoderTurn` → `applyNoteEditChange(Move)` + **bridge**
- [x] 2b.2 Remove duplicate overlap helpers from `EditStartNoteState.cpp` (~400 lines)
- [x] 2b.3 `EditPitchNoteState::onEncoderTurn` → `applyNoteEditChange(Pitch)` + **focus.last**
- [x] 2b.4 `EditLengthNoteState`: **DisplayNote** from **focus.last** via `liveEditDisplayNoteAtSelect`
- [x] 2b.5 Shared read helpers on `EditManager`: `selectableDisplayNotesAtEditSelect`, `liveEditDisplayNoteAtSelect`
- [x] 2b.6 HITL overlap round-trip — deferred; selection drift tracked in archived **note-edit-hitl-focus-restore**

## 2b sign-off

- [x] `pio test -e native` — 110/110
- [x] Grep: no **deletedNotes** writes in `EditStartNoteState.cpp` (Phase 2c retires writer in utils)
- [ ] Optional HITL smoke after upload — deferred (parent HITL non-gating per overlap C16)

---

## 2c — Retire **deletedNotes** overlap writer (utils)

- [x] 2c.1 Remove **deletedNotes** fallback in `moveNoteWithOverlapHandling`
- [x] 2c.2 Remove **deletedNotes** pitch reindex in same function
- [x] 2c.3 `changeLengthWithOverlapHandling`: read geometry from **focus.last**; no **movingNote** seed block
- [x] 2c.4 `moveNoteWithOverlapHandling`: read geometry from **focus.last** (bridge fallback only)
- [x] 2c.5 HITL recheck — **`000429`**: AC3/AC5 fail; consolidated into archived **note-edit-hitl-focus-restore**

## 2c sign-off

- [x] `pio test -e native`
- [x] Grep: no `movingNote.deletedNotes` in `NoteMovementUtils.cpp`
- [x] HITL edit baseline **`20260620_000429`** — AC1/2/4 pass; AC3/AC5 fail (selection drift, not new in 2c)

---

## 2d — Retire **MovingNoteIdentity** / bridge (focus-only overlap)

- [x] 2d.1 Remove `deletedNotes`, `sessionHiddenOverlapNotes`, `sessionShortenedOverlapNotes` from `EditManager.h`
- [x] 2d.2 Remove unused `deletedEvents`, `wrapCount`, `movementDirection`, `undoSnapshotPushed`
- [x] 2d.3 Remove **`movingNote`** / `MovingNoteIdentity`; **`ensureNoteEditFocusForLiveEdit`** replaces bridge
- [x] 2d.4 Remove **`createDeletedNote`** from `MidiEventUtils.*`
- [x] 2d.5 Delete `test_delete_restore`, `test_shorten_delete_restore`
- [x] 2d.6 HITL recheck — **`001421`**, **`001758`**: B1/B2 fixes in archived **note-edit-hitl-focus-restore**

## 2d sign-off

- [x] `pio test -e native` — 110/110
- [x] Grep: no `movingNote` / `MovingNoteIdentity` / `deletedNotes` in NOTE_EDIT firmware paths (except **focus.movingNoteRange**)
- [ ] Optional HITL smoke after upload — deferred (parent HITL non-gating per overlap C16)
