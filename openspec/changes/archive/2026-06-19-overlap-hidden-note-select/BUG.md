# BUG — Hidden overlap notes selectable; display/delete drift from session store

**Change:** `overlap-hidden-note-select`  
**Status:** Phase 1 signed off (**AC1–AC5** pass on capture **`233328`**, 2026-06-19)  
**Evidence:** `captures/host_midi_automation_edit_baseline_20260619_212149_serial.log` + JSON report

**Related:**

- [note-edit-modification-session](../note-edit-modification-session/) — **overlapNotes** owner (**Hidden** / **Shortened** / **visible**)
- [change-length-commit-rematerialize](../change-length-commit-rematerialize/) — rematerialize Track A/B (**signed off** `212149`)
- [edit-record-display-length-mode](../edit-record-display-length-mode/) — length-mode vs position edit (partial fix 2026-06-19)
- [note-move-pitch-overlap-flaky](../note-move-pitch-overlap-flaky/) — overlap round-trip AC (partial)

**Vocabulary:** [Naming-Vocabulary-Teensy-Looper.mdc](../../.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc) — **overlap note**, **hidden** store state; not **victim** / **neighbor note**.

---

## User report (2026-06-19)

After **lengthening** a long note and **moving** other notes over it (overlap shortens/hides correctly in preview):

1. **Display** shows a different layout than serial verification / valid edit passes imply.
2. **Delete** (NOTELEN double): intended note is **not** deleted; the **long note disappears**; an **earlier moved note resets** to its pre-edit position.
3. Selecting a **third note** to move: fader-1 briefly selects a note that was **fully inside** the long note (should be **hidden**); **bracket stops there then hops** to the correct selection.

User hypothesis: **hidden overlap note** state is not **invalidated for selection** during the note-edit session.

---

## Expected behavior

| Step | Expected |
|------|----------|
| Move short note over lengthened note | Long note **shortened** in **NoteEditSession.store**; inner **contained** overlap notes **hidden** (pairs removed from store, tracked in **overlapNotes**) |
| Display during edit | Piano roll matches **session store** reconstruction (same as serial `#CAP` / REVT inventory) |
| Fader-1 select after overlap moves | Nav slots include only **selectable** notes — **not** **Hidden** overlap notes |
| Bracket / select sync | Bracket lands on user-selected note **without hop** from ghost hidden slot |
| Delete selected note B | **DeleteNote** targets B’s live **NoteRef**; long mover and other committed moves **unchanged** |
| After delete | **Hidden** overlap notes stay hidden unless restore rules say otherwise; moved notes **stay** at edited positions |

---

## Actual behavior

### 1. Display vs serial mismatch

- HITL `212149`: overlap round-trip + change-length store **pass** on serial; user observes display **not** matching those passes (manual / piano roll).
- **NOTE_EDIT** display uses `track.getCachedNotes()` → full `reconstructNotes(editAwareMidiEvents())` with **no** **overlapNotes** filter ([`DisplayManager.cpp`](../../../src/DisplayManager.cpp) ~240–243).
- Select nav uses the same uncached note list ([`SelectNavigation.cpp`](../../../src/Utils/SelectNavigation.cpp) ~19–61).

If **Hidden** pairs are removed from store but **baselineMap** / cache / ghost reconstruction reintroduces notes, or if display lags after `commitEditAction` rematerialize, UI diverges from serial truth.

### 2. Delete wrong target — long note gone, moved note reset

@ **111.702s** — HITL step `delete B @ fixture step 4` (expect **pitch 64, start≈200**):

```
MIDI Encoder: Deleting note pitch=67, start=8, end=391
MIDI Encoder: Deleted 2 MIDI events for note
commitEditAction trace: editId=6 ...
Note selection changed: 2 -> -1
```

Immediately before delete, pre-commit logged **ChangeLength** on **M67 start=8** (lengthened M0), not B.

**Actual:** Long mover **M67@8** deleted from store; B@200 remains in reconstruction. Earlier moves **lost** on `commitEditAction` rematerialize when pending edits applied to **wrong focus**.

Root cause cluster (to confirm in design):

| ID | Claim |
|----|-------|
| **H1** | `deleteSelectedNote` runs `commitAllPendingNoteEditActions` while **focus** still describes **previous mover** (M0), not fader-selected B |
| **H2** | `rebuildNoteEditFocusAtSelect(selectedIdx)` rebuilds **commitBaseline** from **Takes+Edits replay**, not live session — **selectedIdx** indexes into **stale** reconstructed list |
| **H3** | Delete target taken from **focus.commitBaseline** after misaligned rebuild, not from **capture-at-press** NoteRef for slot B |

### 3. Hidden inner note still in select navigation

- `buildSelectNavigationSlots` enumerates **all** `getCachedNotes()` indices — no check of **overlapNotes** **Hidden**.
- `resolveNoteIdxAtSlot` prefers **movingNote** identity at tick — can disagree with user slot intent when multiple notes share a step.
- User-visible **bracket hop**: first sync to ghost index (hidden inner), then `scheduleOtherFaderUpdates` / second pass corrects.

---

## Repro

**HITL (automated partial):**

```bash
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy MIDI" --midi-in "Teensy MIDI" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 --record-bars 2 --start-transport \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 500
```

Segment: after `_run_delay_move_insert_reorder` setup — **delete B @ step 4**; serial must show delete **pitch=64 start≈200**, not M67@8.

**Manual (user report):**

1. Record loop; enter note edit.
2. Lengthen M0 (NOTELEN length mode); fader-1 select another note; move over M0 (overlap shortens long note).
3. Move one or more additional notes (same session, no exit).
4. Fader-1 select a **third** note — observe bracket hop at former **inner** note position.
5. NOTELEN double delete on intended note — observe long note vanish + earlier move reset.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership of **selectable inventory**? | **Yes** — must align **NoteEditFocus.overlapNotes** with select + display (new helper) |
| **Hidden** ≠ store absent? | **Hidden** = removed from session flat + tracked in **overlapNotes**; MUST NOT be selectable until **visible** / restored |
| State transition change? | **Yes** — delete and fader-1 select must use **same** filtered inventory; delete pre-commit scoped to **selected** note |

---

## Acceptance criteria (serial + HITL)

### AC1 — Hidden overlap excluded from select

- After contained hide during move, fader-1 slot at inner note tick MUST NOT list **Hidden** overlap note index.
- Serial: no `Select fader: selected note N` where N is **Hidden** **overlapNotes** baseline (new log or verifier).

| Status | Capture | Notes |
|--------|---------|-------|
| **Pass** | `233328` | No select on unrestored Hidden; 2 hide events, restores before re-select at same ticks |

### AC2 — No bracket hop on third-note select

- Single fader-1 select after overlap chain: bracket tick == selected note start; no second corrective select within 500 ms unless user moves fader.

| Status | Capture | Notes |
|--------|---------|-------|
| **Fail** | `212149` @ 89.332s | Index/tick mismatch pre-fix |
| **Pass** | `233328` | 6 DNTE-backed selects aligned; no index≠tick class |

### AC3 — Delete targets selected note

- `Deleting note pitch=%d, start=%lu` MUST match fader-selected note (fixture B: pitch **64**, start **200** ±16th tolerance).
- Post-delete reconstruction: B absent; lengthened M0 (or committed mover) **present** at edited length/position.

| Status | Capture | Notes |
|--------|---------|-------|
| **Fail** | `212149` @ 111.702s | Deleted M67@8; ChangeLength on mover before delete |
| **Pass** | `223829`, **`233328`** | `Deleting note pitch=64, start=208`; no pre-delete ChangeLength on mover |

### AC4 — Display matches session store

- In NOTE_EDIT, display note list equals `reconstructNotes(session store)` minus **Hidden** overlap notes (same filter as AC1).

| Status | Capture | Notes |
|--------|---------|-------|
| **Pass** | `233328` | 11 `#CAP DISP` frames; frameNotes≤flatEvents (filtered path) |

### AC5 — Moved notes survive delete

- Notes committed or live-moved before delete remain at edited ticks in post-delete `applyEdits` replay (D@872 move, etc. — fixture-specific).

| Status | Capture | Notes |
|--------|---------|-------|
| **Pass** | `233328` | M67@16–688 + D@880 survive; session_store 14→12 (B only); no 212149-class reset |

---

## Patch history

| Date | Result |
|------|--------|
| 2026-06-19 | OpenSpec `overlap-hidden-note-select` opened from user report + `212149` capture |
| 2026-06-19 | Prior partial fixes: delete pre-commit chain, length-mode reset on select, hot-path materialize removed — **delete/select/display still broken** |
| 2026-06-19 | Phase 1 A–D: filter, display, select (`rebuildNoteEditFocusForDisplayNote`), C6 delete rewrite; **C18** demoted API grep gate |
| 2026-06-19 | HITL **`233328`**: **Phase 1 sign-off** — AC1–AC5 pass (`verify_overlap_hidden_ac.py`); parent `edit.ok=false` non-gating (**C16**) |
