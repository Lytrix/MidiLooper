# Handoff brief — Note edit session consumer contract

**Change:** `overlap-hidden-note-select`  
**Branch:** `refactor/timeline-data-model` (typical)  
**Date:** 2026-06-19  
**Status:** Phase 1 Slice A complete — Slice B next

Use this file to continue in a **new chat**. Run `/opsx:apply overlap-hidden-note-select` or implement [tasks.md](./tasks.md) directly.

---

## One-line goal

Unify **NoteEditSession** read/write: one filter for NOTE_EDIT UI (`filterSelectableDisplayNotes`), one overlap writer (`applyNoteEditChange` + **focus.overlapNotes**), retire parallel **movingNote.deletedNotes** / unfiltered **getCachedNotes()** consumers.

---

## Read first (in order)

| # | File | Why |
|---|------|-----|
| 1 | [HANDOFF-BRIEF.md](./HANDOFF-BRIEF.md) | This brief + verification checklist |
| 2 | [CLARIFICATIONS.md](./CLARIFICATIONS.md) | Locked C1–C15 + **Investigation outcomes** (`212149`) |
| 3 | [PRE-EXECUTION.md](./PRE-EXECUTION.md) | API locks, slice order, scope fences |
| 4 | [tasks.md](./tasks.md) | Granular tasks Phase 0–3 |
| 5 | [architecture-review.md](./architecture-review.md) §10 | Retirement ladder |
| 6 | [BUG.md](./BUG.md) | HITL AC + evidence |

Parent change: [note-edit-modification-session](../note-edit-modification-session/) (overlap owner partial; consumers leaky).

---

## What’s done (do not redo)

- [x] Architecture review + retirement ladder (§10)
- [x] Clarifications **C1–C15** — user accepted all defaults (2026-06-19)
- [x] Investigations **1.1–1.3** from `captures/host_midi_automation_edit_baseline_20260619_212149_serial.log`
- [x] PRE-EXECUTION checklist §1–§7 (except optional 0.5 consumer audit, 0.6 parent debt markers)
- [x] Naming: use **`filterSelectableDisplayNotes`** — not `audibleNotes`, not `NoteEditSessionView` ([Naming-Vocabulary-Teensy-Looper.mdc](../../.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc))

---

## Locked decisions (implementation must follow)

| ID | Decision |
|----|----------|
| C1 | **baselineMap** from committed `applyEdits(takes, edits)`; **focus.last** from live store |
| C2 | **NoteRef** on fader-1 select; `selectedNoteIdx` = derived **filtered** index |
| C3 | **Shortened** overlap notes stay in select nav |
| C4 | **Hidden** notes never drawn on piano roll |
| C5 | Exclude **innerUnderMovingNote** from filter |
| C6 | Delete: capture **NoteRef** → if ≠ focus: overlap-only commit → rebuild on target → **DeleteNote** |
| C7–C13 | See [CLARIFICATIONS.md](./CLARIFICATIONS.md) |
| C14 | New API **`rebuildNoteEditFocusForDisplayNote`** — never filtered index into `rebuildNoteEditFocusFromStore` |
| C15 | **`SelectNavSlot.noteIdx`** = filtered index when session active |

---

## Investigation summary (`212149`)

| Task | Finding |
|------|---------|
| **1.1 Delete** @ 111.702s | `ChangeLength` on note **67@8** then **Delete** same — not B (**64@200**). Stale focus + wrong index. |
| **1.2 Select** @ 89.332s | Log says tick **200** but `#CAP DNTE` shows index **3 = pitch 60@584** — unfiltered index ≠ bracket tick. |
| **1.3 Cache** @ 81.303s | Session/frame = **6** notes; **visualCache** = **7** (stale). Filter + index contract still required. |

---

## Implementation order (PRE-EXEC §5)

| Slice | Tasks | Verify before next slice |
|-------|-------|---------------------------|
| **A** | 2.1–2.3 `filterSelectableDisplayNotes` + native tests | `pio test -e native` |
| **B** | 4.1 display → `liveDisplayNotes` | Manual / DISP frame count |
| **C** | 3.1–3.5 select + `rebuildNoteEditFocusForDisplayNote` + `finalReconstructAndSelect` | HITL AC1–AC2 |
| **D** | 5.1–5.3 **rewrite** `deleteSelectedNote` (C6) | HITL AC3, AC5 (`212149` delete segment) |
| **E** | 6.x full sign-off | Full edit baseline HITL |

**Do not** mix Phase 2a (**movingNote** removal) into Phase 1.

---

## Primary files to touch (Phase 1)

| File | Change |
|------|--------|
| `include/NoteEditFocus.h`, `src/NoteEditFocus.cpp` | `filterSelectableDisplayNotes`, `rebuildNoteEditFocusForDisplayNote`, index helpers |
| `src/DisplayManager.cpp` | NOTE_EDIT → filter into `liveDisplayNotes` |
| `src/NoteEditManager.cpp` | `buildSelectNavigationSlots`, `handleSelectFaderInput`, **rewrite** `deleteSelectedNote` |
| `src/Utils/SelectNavigation.cpp` / `.h` | Document filtered `noteIdx` (C15) |
| `src/Utils/NoteMovementUtils.cpp` | `finalReconstructAndSelect` — NoteRef + filtered index (C11) |
| `test/test_note_edit_focus/` | Filter + innerUnderMovingNote cases |

**Later (Phase 2):** `EditStartNoteState.cpp`, `EditManager.h` (**movingNote** delete), `MidiFaderProcessor.cpp`.

---

## Verification checklist (full)

Copy into new chat and tick off as you go.

### Phase 0 — Planning (done unless noted)

- [x] C1–C15 locked
- [x] Investigations 1.1–1.3 recorded
- [x] PRE-EXEC §1–§7 acknowledged
- [ ] 0.5 Consumer audit complete (architecture-review §5 + C9 grep list)
- [ ] 0.6 Parent [tasks.md](../note-edit-modification-session/tasks.md) §9.3 debt markers

### Phase 1 — Slice A (filter)

- [x] `filterSelectableDisplayNotes` implemented (PRE-EXEC §4 rules)
- [x] Native: **Hidden** excluded; **Shortened** included; **innerUnderMovingNote** excluded
- [x] `pio test -e native` green

### Phase 1 — Slice B (display)

- [x] `DisplayManager::resolveDisplayNotes` NOTE_EDIT uses filter → `liveDisplayNotes`
- [ ] No NOTE_EDIT path returns unfiltered `getCachedNotes()` without filter (C9)

### Phase 1 — Slice C (select)

- [ ] `buildSelectNavigationSlots` uses filtered list when `NoteEditSession.active`
- [ ] `rebuildNoteEditFocusForDisplayNote` on fader-1 select (C14)
- [ ] `resolveNoteIdxAtSlot` / bracket use filtered list + **focus.last** (prep for Phase 2a)
- [ ] `finalReconstructAndSelect` uses NoteRef + filtered index (C11)
- [ ] HITL AC1–AC2: no select on **Hidden**; no index/tick mismatch like `212149` @ 89.332s

### Phase 1 — Slice D (delete)

- [ ] `deleteSelectedNote` **rewritten** per C6 (not patched spot-fix chain)
- [ ] Delete captures **NoteRef** before any commit
- [ ] HITL AC3, AC5: delete **pitch=64 @ ~200**; long mover **67@8** survives (`212149` @ 111.702s fixed)

### Phase 1 — Sign-off (Slice E)

- [ ] `pio test -e native` — all green
- [ ] Full HITL: `scripts/host_midi_automation_edit_baseline.py` + `--verify-serial-log` on fresh capture
- [ ] AC1–AC5 in [BUG.md](./BUG.md) marked with capture id
- [ ] Grep gate: no bare `getCachedNotes()` in NOTE_EDIT / session-active paths (task 6.3)
- [ ] No regression: change-length store + overlap round-trip verifiers still green

### Phase 2a — focus replaces movingNote reads

- [ ] All `movingNote.*` reads → **focus.last** / **focus.active** in `NoteEditManager`, `MidiFaderProcessor`
- [ ] Remove `syncMovingNoteFromFocus` / `syncFocusLastFromMovingNote`
- [ ] Native + smoke HITL

### Phase 2b — Encoder port (C7)

- [ ] `EditStartNoteState::onEncoderTurn` → `applyNoteEditChange(Move)`
- [ ] `EditPitchNoteState` → `applyNoteEditChange(Pitch)`
- [ ] `EditLengthNoteState` uses **focus.last** for DisplayNote source
- [ ] Delete overlap helpers + **deletedNotes** from `EditStartNoteState.cpp`

### Phase 2c — Fader overlap cleanup

- [ ] Remove **deletedNotes** fallback in `moveNoteWithOverlapHandling`
- [ ] HITL lengthen-overlap-neighbor-restore AC

### Phase 2d — Legacy delete

- [ ] Remove **MovingNoteIdentity**, session overlap vectors from `EditManager.h`
- [ ] Delete `test_delete_restore`, `test_shorten_delete_restore` (C12)
- [ ] Grep: no **deletedNotes** in NOTE_EDIT firmware

### Phase 3 — Boundary hardening

- [ ] `/opsx:sync` parent design baselineMap source (C1)
- [ ] Single commit-boundary module (C10)
- [ ] Grep ban `commitEditAction` outside boundary helpers

---

## HITL commands (reference)

**Serial capture** (separate terminal):

```bash
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801
```

**Edit baseline** (after slices C/D or full E):

```bash
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 500 \
  --verify-serial-log captures/<your_capture>.log
```

**Native tests** (every slice):

```bash
pio test -e native
```

**Firmware build** (when flashing): `pio run -e teensy41-capture-serial` — ask before upload.

---

## Pass criteria quick reference (BUG.md)

| AC | Check |
|----|-------|
| AC1 | **Hidden** not in fader-1 select slots |
| AC2 | No bracket hop / index≠tick (see 89.332s DNTE vs select log) |
| AC3 | Delete targets selected **NoteRef**, not long mover |
| AC4 | Display matches filtered session inventory |
| AC5 | Earlier moved notes survive delete + commit |

---

## Out of scope (defer)

- Track C insert/reorder (parent §7.4)
- [edit-record-display-length-mode](../edit-record-display-length-mode/) until after Phase 1 (C13)
- Phase 2+ in same PR as Phase 1

---

## Suggested first message for new chat

> Continue OpenSpec change **`overlap-hidden-note-select`**. Read `openspec/changes/overlap-hidden-note-select/HANDOFF-BRIEF.md` and implement Phase 1 Slice A (`filterSelectableDisplayNotes` + native tests). C1–C15 locked; investigations 1.1–1.3 done on capture `212149`.
