# Tasks — Note edit session consumer contract

**Change:** `overlap-hidden-note-select`  
**Status:** Phase 1 complete — Slice E signed off (2026-06-19)  
**Apply:** `/opsx:apply` after clarifications resolved  
**Review:** [architecture-review.md](./architecture-review.md) §10  
**Clarifications:** [CLARIFICATIONS.md](./CLARIFICATIONS.md) — **C1–C18 locked** (2026-06-19)  
**New chat:** [HANDOFF-BRIEF.md](./HANDOFF-BRIEF.md)

---

## Phase 0 — Architecture + clarifications

- [x] 0.1 [architecture-review.md](./architecture-review.md) — gap analysis, §10 retirement ladder
- [x] 0.2 Proposal + design expanded to architecture milestone
- [x] 0.3 Naming: `filterSelectableDisplayNotes`; no invented module nouns
- [x] 0.4 [CLARIFICATIONS.md](./CLARIFICATIONS.md) — open decisions with task blockers
- [x] 0.5 Complete consumer audit — [PRE-EXECUTION.md](./PRE-EXECUTION.md) §11 + **C18** grep gate
- [ ] 0.6 Mark interim spot fixes as debt in parent [tasks.md](../note-edit-modification-session/tasks.md) §9.3
- [x] 0.7 **C1–C13** resolved — user accepted all defaults (2026-06-19)
- [x] 0.8 [PRE-EXECUTION.md](./PRE-EXECUTION.md) checklist + investigations 1.1–1.3

**Gate:** Phase 1.2 code **unblocked**.

---

## Phase 1 — Read side + delete boundary (architecture §10 Step 1)

**Decisions:** C1–C6, C9, C11 locked in [CLARIFICATIONS.md](./CLARIFICATIONS.md)

### 1. Investigation (read-only)

- [x] 1.1 HITL `212149` @ 111.702s — see [CLARIFICATIONS.md](./CLARIFICATIONS.md) Investigation 1.1
- [x] 1.2 @ 89.332s index/tick mismatch — DNTE pitch 60@584 vs tick 200 — Investigation 1.2
- [x] 1.3 @ 81.303s store/frame=6, visualCache=7 — Investigation 1.3
- [ ] 1.4 **C7** code path: `ButtonManager` → `EditManager::onEncoderTurn` (device confirm in Phase 2b)

### 2. `filterSelectableDisplayNotes` (**C3**, **C4**, **C5**)

- [x] 2.1 Implement `filterSelectableDisplayNotes` — rules in [PRE-EXECUTION.md](./PRE-EXECUTION.md) §4
- [x] 2.2 Per **C2**: `noteRefFromFilteredDisplayNote` + `filteredDisplayNoteIndexForNoteRef` (fader-1 **focus.moving** wiring → task 3.3)
- [x] 2.3 Native (`test_note_edit_focus` or `test_select_navigation`): **Hidden** excluded; **Shortened** per **C3**; inner per **C5**

### 3. Select navigation + bracket (**C2**, **C11**)

- [x] 3.1 `NoteEditManager::buildSelectNavigationSlots` → filtered **DisplayNote** list when `NoteEditSession.active`
- [x] 3.2 `handleSelectFaderInput` / `resolveNoteIdxAtSlot`: use filtered list; moving-note disambiguation via **focus.last** not **movingNote** (prep for Phase 2a)
- [x] 3.3 On fader-1 select: **`rebuildNoteEditFocusForDisplayNote`** (PRE-EXEC §1) — not filtered index into `rebuildNoteEditFocusFromStore`
- [x] 3.4 After overlap hide: invalidate selection if **NoteRef** / index no longer in filtered list
- [x] 3.5 `NoteMovementUtils::finalReconstructAndSelect`: resolve index via **focus.last** + filter (**C11**)
- [x] 3.6 HITL AC1–AC2 — **`233328`**

### 4. Display (**C4**)

- [x] 4.1 `DisplayManager::resolveDisplayNotes` NOTE_EDIT → filter into **`liveDisplayNotes`** (PRE-EXEC §3)
- [x] 4.2 AC4 parity — **`233328`** DISP frames

### 5. Delete boundary (**C6**)

- [x] 5.1 Capture delete **NoteRef** from selection **before** any commit (per **C2**)
- [x] 5.2 **Rewrite** `deleteSelectedNote` per **C6** / PRE-EXEC §2 (no patch on spot-fix chain)
- [x] 5.3 HITL AC3 + AC5 — **`233328`**

### 6. Phase 1 sign-off

- [x] 6.1 `pio test -e native` — 110/110 green (2026-06-19)
- [x] 6.2 HITL edit baseline AC1–AC5 — **`233328`** (**C16** — parent sub-verifiers non-gating)
- [x] 6.3 Grep gate per **C18** / PRE-EXEC §11 — `NoteEditManager` + no positive `rebuildNoteEditFocusAtSelect`
- [x] 6.4 Update [BUG.md](./BUG.md) AC rows — **`233328`**

---

## Phase 2a — **focus** replaces **movingNote** reads (architecture §10 Step 2)

**Blocked until:** Phase 1 sign-off; **C8** locked (full **movingNote** delete)

- [ ] 2a.1 Replace `movingNote.*` reads in `NoteEditManager.cpp` with **focus.last** / **focus.active**
- [ ] 2a.2 Replace `movingNote` checks in `MidiFaderProcessor.cpp`
- [ ] 2a.3 Remove `seedMovingNoteForOverlapEdit` / dual sync if no callers need **movingNote**
- [ ] 2a.4 Delete `syncMovingNoteFromFocus`, `syncFocusLastFromMovingNote` from `EditManager.cpp`
- [ ] 2a.5 Native + HITL smoke: fader move/length/pitch unchanged

---

## Phase 2b — Encoder port (architecture §10 Step 3)

**Blocked until:** Phase 2a; **C7** locked (port encoder)

- [ ] 2b.1 Replace `EditStartNoteState::onEncoderTurn` with `applyNoteEditChange(Move)` adapter
- [ ] 2b.2 Delete overlap helpers from `EditStartNoteState.cpp` (`findOverlaps`, `applyShortenOrDelete`, **deletedNotes** restore)
- [ ] 2b.3 `EditPitchNoteState::onEncoderTurn` → `applyNoteEditChange(Pitch)`
- [ ] 2b.4 `EditLengthNoteState`: build **DisplayNote** from **focus.last** not `getCachedNotes()[idx]`
- [ ] 2b.5 HITL overlap round-trip + child [note-move-pitch-overlap-flaky](../note-move-pitch-overlap-flaky/) AC

---

## Phase 2c — Remove **deletedNotes** from fader move (architecture §10 Step 4)

**Blocked until:** Phase 2b or **C7=B** (encoder gated)

- [ ] 2c.1 Remove **deletedNotes** fallback block in `moveNoteWithOverlapHandling` (`NoteMovementUtils.cpp`)
- [ ] 2c.2 Remove **deletedNotes** pitch reindex in same function
- [ ] 2c.3 Stop seeding **movingNote** in `changeLengthWithOverlapHandling` — use **focus** only
- [ ] 2c.4 HITL [lengthen-overlap-neighbor-restore](../lengthen-overlap-neighbor-restore/) AC

---

## Phase 2d — Delete legacy structs (architecture §10 Step 5)

**Blocked until:** Phase 2c; **C8**, **C12**

- [ ] 2d.1 Remove `deletedNotes`, `sessionHiddenOverlapNotes`, `sessionShortenedOverlapNotes` from `EditManager.h`
- [ ] 2d.2 Remove unused `deletedEvents`, `wrapCount`, `movementDirection` per **C8** audit
- [ ] 2d.3 Remove **`movingNote`** field entirely if **C8** = full delete; else document retained fields
- [ ] 2d.4 Clean `createDeletedNote` / overlap-only utils if unreferenced
- [ ] 2d.5 Delete `test_delete_restore`, `test_shorten_delete_restore`; update `platformio.ini` **test_ignore** if needed (**C12**)
- [ ] 2d.6 Grep: no **deletedNotes** in NOTE_EDIT firmware paths

---

## Phase 3 — Boundary hardening (architecture §10 Step 6)

**Blocked until:** Phase 2d

- [ ] 3.1 `/opsx:sync` parent design — **baselineMap** = committed materialization (**C1**)
- [ ] 3.2 Consolidate commit triggers: select / exit / overdub / delete into one boundary module (**C10**)
- [ ] 3.3 Document + grep gate: no `commitEditAction` outside boundary helpers
- [ ] 3.4 Update architecture-review §8 success criteria — all rows green

---

## Parent linkage

- [ ] P.1 Parent [proposal.md](../note-edit-modification-session/proposal.md) — child row current
- [ ] P.2 Parent [tasks.md](../note-edit-modification-session/tasks.md) §8.1 → Phase 2b; §9 → this file

---

## Out of scope (unless investigation expands **C9**)

- `BarStepButtonHandler`, `LoopEditManager`, `MidiButtonActions` when not in NOTE_EDIT / no active session
- [edit-record-display-length-mode](../edit-record-display-length-mode/) — after Phase 1 (**C13**)
- Track C insert/reorder (parent §7.4)

---

## Interim spot fixes — do not extend

Replace with Phase 1–3; do not add variants:

- Hot-path `commitEditAction` removal (keep)
- `resetLengthEditingModeOnNoteSelect` (keep until Phase 1 select stable)
- Ad-hoc delete pre-commit chains (replace task 5.2)
