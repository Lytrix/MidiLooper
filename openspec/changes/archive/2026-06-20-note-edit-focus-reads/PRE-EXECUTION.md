# Pre-execution — Phase 2a token locks

**Change:** `note-edit-focus-reads`  
Read [HANDOFF-BRIEF.md](./HANDOFF-BRIEF.md) first. Do **not** re-read the full archived
change unless blocked.

---

## 1. Single read source (lock)

| Need | Use | Do not use |
|------|-----|------------|
| Live mover pitch/start/end during fader edit | `focus.last` when `focus.active` | `movingNote.note`, `lastStart`, `lastEnd` |
| Mover range for overlap | `focus.movingNoteRange` + `movingNoteRangeDisplayEnd` | `movingNote.origStart` in UI |
| Selected note when no live focus | `selectableDisplayNotesForEditUi[selectedIdx]` | `getCachedNotes()[idx]` |
| Same-tick select disambiguation | `focus.last` | `movingNote` fallback (delete in 2a.2) |
| New tick select | `candidates.front()` (**C17**) | stale `focus.last` |

---

## 2. Grep gates

**Must pass after 2a:**

```bash
# No UI reads of movingNote geometry in NoteEditManager
rg 'movingNote\.(note|lastStart|lastEnd|origStart|origEnd|active)' src/NoteEditManager.cpp

# Allowed: bridge sync before overlap util (0–2 call sites)
rg 'syncMovingNoteFromFocus|seedMovingNoteForOverlapEdit' src/NoteEditManager.cpp

# MidiFaderProcessor: no movingNote reads
rg 'movingNote' src/MidiFaderProcessor.cpp
# Expect: 0 matches after 2a.4
```

**Do not grep-ban `movingNote` in `NoteMovementUtils.cpp`** — Phase 2c.

---

## 3. Bridge pattern (until 2c)

`NoteMovementUtils::moveNoteWithOverlapHandling` still checks `manager.movingNote.active`
(entry ~line 30). Phase 2a **does not** rewrite that function.

In `NoteEditManager::moveNoteToPositionWithOverlapHandling` only:

```text
if (focus.active) syncMovingNoteFromFocus(movingNote, focus);
else seedMovingNoteForOverlapEdit(currentNote);  // rare: session without focus
```

No fader handler may **read** back from **movingNote** after this.

---

## 4. File touch list (only these)

| File | Action |
|------|--------|
| `src/NoteEditManager.cpp` | 2a.1–2a.3 |
| `src/MidiFaderProcessor.cpp` | 2a.4 |
| `src/EditManager.cpp` | Remove dead sync; keep bridge |
| `include/EditManager.h` | Comment: `syncMovingNoteFromFocus` = bridge only |

**Do not open:** `EditStartNoteState.cpp`, `EditLengthNoteState.cpp`, `EditPitchNoteState.cpp`,
`NoteMovementUtils.cpp` (except if compile forces a one-line include — avoid).

---

## 5. Verification budget

| Step | Cost |
|------|------|
| Every edit batch | `pio test -e native` |
| Sign-off | `verify_overlap_hidden_ac.py captures/...233328_serial.log` |
| Optional | One edit-baseline run — **not** full parent `edit.ok` |

Do **not** re-derive Phase 1 AC from scratch; use verifier script.

---

## 6. Inherited locks (archive — do not reopen)

From `archive/2026-06-19-overlap-hidden-note-select/CLARIFICATIONS.md`:

- **C14** — `rebuildNoteEditFocusForDisplayNote` for fader-1 select
- **C17** — new tick = first at step
- **C18** — no `rebuildNoteEditFocusAtSelect(idx≥0)`; filtered inventory in faders

Phase 1 demotion stands. Phase 2a adds: **no `movingNote` UI reads**.
