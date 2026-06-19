# Pre-execution refinements — save tokens during `/opsx:apply`

**Change:** `overlap-hidden-note-select`  
**Status:** Locked with [CLARIFICATIONS.md](./CLARIFICATIONS.md) (2026-06-19)  
**Purpose:** Resolve these **before** or **at the start of** Phase 1 so implementation does not rework APIs or burn HITL loops.

---

## 1. Critical — focus rebuild must not use filtered index into unfiltered list

**Risk:** High token cost / wrong-note bugs.

Today:

- `rebuildNoteEditFocusFromStore(..., selectedNoteIdx)` indexes **`reconstructNotes(committed)`** — full unfiltered list.
- Phase 1 makes `selectedNoteIdx` / `SelectNavSlot.noteIdx` index **`filterSelectableDisplayNotes`** — shorter list.
- Passing filtered index into the existing function selects the **wrong note** whenever any **Hidden** / **innerUnderMovingNote** entry was removed.

**Lock before coding task 3.3:**

Add **`rebuildNoteEditFocusForDisplayNote(Track&, const DisplayNote& liveSelected)`** (or `NoteRef` after `findBaselineRefForNote`):

```text
1. applyEdits → committed flat → fill focus.baselineMap (unchanged)
2. baselineRef = findBaselineRefForNote(focus, channel, liveSelected.pitch, liveSelected.startTick, liveSelected.endTick)
3. focus.moving = baselineRef; commitBaseline from baselineMap[baselineRef]
4. focus.last = live geometry from liveSelected (or session.store scan)
5. focus.active = true; overlapNotes.clear() on new select (unchanged policy)
```

Deprecate index-based rebuild for fader-1 select; keep index overload only for legacy encoder paths until Phase 2b.

**Existing helper:** `findBaselineRefForNote` in `NoteEditFocus.cpp` — use it, do not invent parallel matching.

---

## 2. Critical — delete path is a full rewrite, not a patch

**Risk:** Layering C6 on spot-fix chain wastes review tokens.

Current `deleteSelectedNote` order (wrong for C6):

```368:370:src/NoteEditManager.cpp
    editManager.commitAllPendingNoteEditActions(track);
    editManager.rebuildNoteEditFocusAtSelect(track, selectedIdx);
    editManager.commitPendingOverlapNoteEdits(track);
```

**Lock:** Replace with C6 sequence in one edit; do not add a third pre-commit branch.

Also: capture delete target from **filtered** list **before** any commit; use **live** end tick from store for pair erase after focus aligned.

Callers: `MidiButtonActions.cpp`, `BarStepButtonHandler.cpp` — same function, no duplicate logic.

---

## 3. Display buffer — reuse `DisplayManager::liveDisplayNotes`

**Risk:** Return-type churn (`const vector&` from `getCachedNotes` vs owned filter result).

NOTE_EDIT branch already invalidates and uses member buffers elsewhere in `resolveDisplayNotes`. **Lock:** populate **`liveDisplayNotes`** from `filterSelectableDisplayNotes(sessionEvents, focus, loopLength)` and return const ref — same pattern as live recording branch.

Avoid adding a second display cache on **NoteEditFocus**.

---

## 4. Filter matching rules — write down before task 2.1

**Lock explicit rules (no discovery during HITL):**

| Case | Exclude from filter? |
|------|----------------------|
| **overlapNotes** state **Hidden** | Yes — match `OverlapNote.ref` or baseline triple via `findOverlapNoteEntry` |
| **innerUnderMovingNote** (C5) | Yes |
| **Shortened** | No — show at live shortened gate |
| Moving note | No — always present in store |

Start from **`reconstructNotes(session.store)`**, not cache.

**Open question (cheap to settle):** match hidden by **baseline** `(pitch, start, end)` from `OverlapNote.ref` only, or also live `DisplayNote` start when shortened? **Default:** use `findOverlapNoteEntry` / baseline triple already on **OverlapNote**; add one native case for shortened-vs-hidden boundary.

---

## 5. Phase 1 slice order — minimize rework

**Recommended PR / commit order:**

| Slice | Tasks | HITL subset |
|-------|-------|-------------|
| **A** | 2.1–2.3 filter + native tests | none |
| **B** | 4.1 display | manual eyeball |
| **C** | 3.1–3.5 select + `finalReconstructAndSelect` | AC1–AC2 |
| **D** | 5.1–5.3 delete rewrite | AC3, AC5 (`212149` segment) |
| **E** | 6.x full sign-off | full edit baseline |

Do **not** start Phase 2a (**movingNote** removal) in the same slice as Phase 1 — overlapping touch points in `NoteEditManager.cpp`.

---

## 6. Investigation tasks — do read-only first (one session, ~no device)

| Task | Source | Outcome |
|------|--------|---------|
| 1.1 | `captures/host_midi_automation_edit_baseline_20260619_212149_serial.log` | Confirm commit-before-rebuild line numbers for delete bug |
| 1.2 | Same log — SelectNav / overlap lines | Confirm hop is **innerUnderMovingNote** vs **Hidden** |
| 1.3 | Compare store reconstruct vs cache after hide | If identical, filter is for **inner** + index contract only; if not, fix invalidation first |

Record outcomes in task checkboxes or one line in [BUG.md](./BUG.md) — avoids re-reading 8k-line logs during implement.

---

## 7. HITL token budget

Full `host_midi_automation_edit_baseline.py` run is ~100s+ serial. During development:

```bash
# After slice C or D only — pass verify flags your script supports; else full run at E only
.venv/bin/python scripts/host_midi_automation_edit_baseline.py ... --verify-serial-log captures/....log
```

**Lock:** Native green after every slice; **full HITL** only at tasks 3.6, 5.3, 6.2 — not after every file edit.

Serial capture: one terminal `capture_session.py` per HITL attempt — do not debug without serial (repo rule).

---

## 8. Scope fences — do not expand Phase 1

| Temptation | Defer to |
|------------|----------|
| Remove **movingNote** reads in select handler | Phase 2a (use **focus.last** for disambiguation only if trivial) |
| Port **EditStartNoteState** | Phase 2b |
| Fix length-mode FSM | C13 / after Phase 1 |
| **EditSelectNoteState** / encoder bracket `getCachedNotes` | Phase 2 or end of Phase 1 grep if session-active |
| Consolidate commit boundary module | Phase 3 |
| Revert spot-fix delete ordering “just to test” | Never — replace wholesale per §2 |

---

## 9. Native test placement

**Lock:** extend **`test_note_edit_focus`** for filter + **innerUnderMovingNote** + **Hidden** (same Unity patterns as overlap state tests). Touch **`test_select_navigation`** only if slot building moves logic without **NoteEditSession** mock.

Avoid new test file unless necessary — parent already targets `test_note_edit_focus` as primary.

---

## 10. Optional — add to CLARIFICATIONS if disagree

| ID | Proposal | Default |
|----|----------|---------|
| **C14** | New API name: `rebuildNoteEditFocusForDisplayNote` vs overload on existing | New name (action + scope) |
| **C15** | `SelectNavSlot.noteIdx` documented as **filtered** index while session active | Yes — comment in `SelectNavigation.h` |

No user sign-off required if defaults OK; otherwise say before `/opsx:apply`.

---

## Checklist (Phase 0.8)

- [x] §1 focus rebuild API named in task 3.3 (`rebuildNoteEditFocusForDisplayNote`)
- [x] §2 delete = rewrite agreed (C6 confirmed @ 212149 111.702s)
- [x] §4 filter rules in task 2.1 (+ innerUnderMovingNote per 1.2)
- [x] §5 slice order in PR plan
- [x] §6 investigations done from `212149` capture — see CLARIFICATIONS **Investigation outcomes**
- [x] §7 HITL only at slice boundaries
- [x] C14/C15 locked (2026-06-19)
