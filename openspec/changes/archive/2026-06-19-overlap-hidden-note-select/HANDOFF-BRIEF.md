# Handoff brief — Note edit session consumer contract

**Change:** `overlap-hidden-note-select`  
**Branch:** `refactor/timeline-data-model` (typical)  
**Date:** 2026-06-19  
**Status:** Phase 1 **complete** (Slice E signed off on capture **`233328`**)

Use this file to continue in a **new chat**. Run `/opsx:apply overlap-hidden-note-select` or implement [tasks.md](./tasks.md) directly.

---

## One-line goal

Unify **NoteEditSession** read/write: one filter for NOTE_EDIT UI (`filterSelectableDisplayNotes`), one overlap writer (`applyNoteEditChange` + **focus.overlapNotes**), retire parallel **movingNote.deletedNotes** / unfiltered **getCachedNotes()** consumers.

---

## Read first (in order)

| # | File | Why |
|---|------|-----|
| 1 | [HANDOFF-BRIEF.md](./HANDOFF-BRIEF.md) | This brief + verification checklist |
| 2 | [CLARIFICATIONS.md](./CLARIFICATIONS.md) | Locked **C1–C18** + investigation outcomes |
| 3 | [PRE-EXECUTION.md](./PRE-EXECUTION.md) | API locks, **§11 demoted APIs / grep gate** |
| 4 | [tasks.md](./tasks.md) | Granular tasks Phase 0–3 |
| 5 | [architecture-review.md](./architecture-review.md) §10 | Retirement ladder |
| 6 | [BUG.md](./BUG.md) | HITL AC + evidence |

Parent change: [note-edit-modification-session](../note-edit-modification-session/) (overlap owner partial; consumers leaky).

---

## What's done (do not redo)

- [x] Architecture review + retirement ladder (§10)
- [x] Clarifications **C1–C18** — user accepted defaults (2026-06-19)
- [x] Investigations **1.1–1.3** from `212149`; delete fix verified on **`223829`**
- [x] PRE-EXEC §1–§7, **§11** consumer grep gate documented
- [x] **Slice A–D:** filter, display, select, delete rewrite (C6), demoted API comments in headers
- [x] Naming: **`filterSelectableDisplayNotes`** — not `audibleNotes`, not `NoteEditSessionView`

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
| C14 | **`rebuildNoteEditFocusForDisplayNote`** — never filtered index into `rebuildNoteEditFocusFromStore` |
| C15 | **`SelectNavSlot.noteIdx`** = filtered index when session active |
| C16 | Phase 1 sign-off = **AC1–AC5** only; parent insert/reorder / overlap round-trip verifiers **non-gating** |
| C17 | New tick select = **first filtered note at step** |
| C18 | **Demoted APIs** — see PRE-EXEC §11; no positive-index `rebuildNoteEditFocusAtSelect` |
| C7–C13 | See [CLARIFICATIONS.md](./CLARIFICATIONS.md) |

---

## Demoted paths (must stay retired — C18)

| Old (conflict) | Replacement | Firmware status |
|----------------|-------------|-----------------|
| `rebuildNoteEditFocusAtSelect(track, idx≥0)` | `rebuildNoteEditFocusForDisplayNote` | **Gone** — only `-1` clear remains |
| `getCachedNotes()[selectedIdx]` in NOTE_EDIT faders | `selectableDisplayNotesForEditUi` | **Migrated** (incl. pitchbend) |
| Unfiltered select slots | `selectableDisplayNotesForEditUi` | **Done** |
| Unfiltered NOTE_EDIT display (session active) | `liveDisplayNotes` + filter | **Done** |
| Delete: commit-all → rebuild-at-index → overlap | C6 `deleteSelectedNote` | **Done** |
| `rebuildNoteEditFocusFromStore` from fader-1 UI | **Forbidden** — C14 | Header comment only |

**Still deferred (Phase 2 — do not copy old patterns when porting):** encoder FSM states, `EditManager::selectNextNote`, `MidiFaderProcessor`, parts of `NoteMovementUtils`.

---

## Investigation summary

| Capture | Task | Finding |
|---------|------|---------|
| **`212149`** | 1.1 Delete @ 111.702s | Wrong target M67@8; stale focus + unfiltered index |
| **`212149`** | 1.2 Select @ 89.332s | Index 3 ≠ tick 200 — unfiltered list |
| **`212149`** | 1.3 Cache @ 81.303s | visualCache stale; frame path OK after filter |
| **`223829`** | 5.3 Delete | **AC3 fixed:** `Deleting note pitch=64, start=208`; no pre-delete ChangeLength on mover |
| **`223829`** | 6.2 Full baseline | `edit.ok` still false — insert/reorder, `m0_home`, split overlap (**C16 non-gating**) |

---

## Implementation order (PRE-EXEC §5)

| Slice | Tasks | Verify before next slice |
|-------|-------|---------------------------|
| **A** | 2.1–2.3 | `pio test -e native` |
| **B** | 4.1 display | Manual / DISP frame count |
| **C** | 3.1–3.5 select + focus rebuild | HITL AC1–AC2 |
| **D** | 5.1–5.3 delete rewrite | HITL AC3 (`223829`) |
| **E** | 6.x sign-off | AC1–AC5 + grep gate (**C16** scope) |

**Do not** mix Phase 2a (**movingNote** removal) into Phase 1.

---

## Primary files touched (Phase 1)

| File | Change |
|------|--------|
| `include/NoteEditFocus.h`, `src/NoteEditFocus.cpp` | Filter, helpers, demotion comment on `rebuildNoteEditFocusFromStore` |
| `include/EditManager.h`, `src/EditManager.cpp` | `rebuildNoteEditFocusForDisplayNote`; demotion on `rebuildNoteEditFocusAtSelect` |
| `src/DisplayManager.cpp` | NOTE_EDIT → `liveDisplayNotes` |
| `src/NoteEditManager.cpp` | Select, delete, faders, `selectableDisplayNotesForEditUi` |
| `src/Utils/SelectNavigation.cpp` / `.h` | C15 filtered `noteIdx` |
| `src/Utils/NoteMovementUtils.cpp` | `finalReconstructAndSelect` |
| `test/test_note_edit_focus/` | Filter cases |

---

## Verification checklist (full)

### Phase 0 — Planning

- [x] C1–C18 locked
- [x] Investigations 1.1–1.3 recorded
- [x] PRE-EXEC §1–§7, §11 acknowledged
- [x] 0.5 Consumer audit — PRE-EXEC §11 grep results
- [ ] 0.6 Parent [tasks.md](../note-edit-modification-session/tasks.md) §9.3 debt markers

### Phase 1 — Slice A–D

- [x] Filter + native tests
- [x] Display filter
- [x] Select + `rebuildNoteEditFocusForDisplayNote`
- [x] Delete C6 rewrite
- [x] Demoted APIs documented + grep clean in `NoteEditManager`

### Phase 1 — Sign-off (Slice E)

- [x] 6.1 `pio test -e native` — all green
- [x] 6.2 HITL AC1–AC5 on **`233328`** (**C16** scope)
- [x] 6.3 Grep gate per C18 / PRE-EXEC §11
- [x] 6.4 [BUG.md](./BUG.md) AC rows — **`233328`**
- [x] 3.6, 4.2, 5.3 AC tasks

**AC verifier:** `.venv/bin/python scripts/verify_overlap_hidden_ac.py captures/<capture>_serial.log`

**Parent baseline (`edit.ok=false`) on `233328` — non-gating per C16:**
`insert_missing_after_in_edit_redo`, `m0_not_home_after_round_trip`, split-overlap home — tracked in parent changes, not Phase 1 blockers.

---

## HITL commands (reference)

**Serial capture** (separate terminal):

```bash
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801
```

**Edit baseline** (Slice E — use **Teensy MIDI** port name):

```bash
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy MIDI" --midi-in "Teensy MIDI" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 500 \
  --verify-serial-log captures/<your_capture>.log
```

**Native tests:**

```bash
pio test -e native
```

**Grep gate (C18):**

```bash
rg 'rebuildNoteEditFocusAtSelect\([^,]+,\s*[^-]' src include --glob '*.{cpp,h}'
rg 'getCachedNotes' src/NoteEditManager.cpp
```

Firmware build: `pio run -e teensy41-capture-serial` — ask before upload.

---

## Pass criteria quick reference (BUG.md)

| AC | Check |
|----|-------|
| AC1 | **Hidden** not in fader-1 select slots |
| AC2 | No bracket hop / index≠tick |
| AC3 | Delete targets selected **NoteRef**, not long mover |
| AC4 | Display matches filtered session inventory |
| AC5 | Earlier moved notes survive delete + commit |

Phase 1 **done** when AC1–AC5 pass (**C16**); parent `edit.ok` sub-failures tracked separately.

---

## Out of scope (defer)

- Track C insert/reorder (parent §7.4)
- [edit-record-display-length-mode](../edit-record-display-length-mode/) until after Phase 1 (**C13**)
- Phase 2+ in same PR as Phase 1

---

## Suggested first message for new chat

> Phase 1 **done** for **`overlap-hidden-note-select`**. Next: Phase 2a (`movingNote` removal) or `/opsx:archive` if ready. Do not reopen demoted APIs (C18).
