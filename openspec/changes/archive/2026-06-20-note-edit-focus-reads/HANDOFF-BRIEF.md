# Handoff — Phase 2a: focus reads replace movingNote UI reads

**Change:** `note-edit-focus-reads`  
**Status:** Ready for `/opsx:apply`  
**Branch:** `refactor/timeline-data-model`

---

## One line

Stop reading **`movingNote.*`** for live edit UI in **NoteEditManager** + **MidiFaderProcessor**;
use **`focus.last`** / **`focus.active`** only. Keep one **bridge write** to **movingNote** for
**NoteMovementUtils** until Phase 2c.

---

## Read first (minimal)

| # | File | Why |
|---|------|-----|
| 1 | This file | Scope + grep |
| 2 | [PRE-EXECUTION.md](./PRE-EXECUTION.md) | Locks, bridge, file list |
| 3 | [tasks.md](./tasks.md) | 5 tasks |
| 4 | `openspec/specs/overlap-hidden-note-select/spec.md` | Shipped Phase 1 contract |

**Skip unless blocked:** full archived `architecture-review.md`, `212149` logs, Phase 2b–3 tasks.

---

## Current debt (grep snapshot 2026-06-19)

### `NoteEditManager.cpp` — migrate reads → **focus.last**

| Area | ~lines | Today |
|------|--------|-------|
| Fader coarse/fine/pitch | 1305–1311, 1450–1454, 1594–1598 | `movingNote.active` → copy geometry |
| `resolveNoteIdxAtSlot` | 1077–1095 | **movingNote** fallback (delete; focus path exists 1058–1075) |
| Overlap move entry | 157–158 | `seedMovingNoteForOverlapEdit` → **bridge** only |
| Select / delete / length mode | 468–469, 1234–1246, 1739–1746 | clear **movingNote** — simplify to focus policy |

### `MidiFaderProcessor.cpp` — migrate

| Area | ~lines | Today |
|------|--------|-------|
| Fader switch | 80–83 | `movingNote.active` |
| `commitMovingNote` | 252–262 | **movingNote** + `getCachedNotes()` |

### Leave alone (Phase 2b+)

- `EditStartNoteState.cpp` — encoder + **deletedNotes** (~400 lines)
- `NoteMovementUtils.cpp` — overlap writer + **deletedNotes** (~200 lines touching **movingNote**)
- `EditManager.h` — `MovingNoteIdentity` struct (2d)

---

## Implementation order

1. **2a.2** — delete `resolveNoteIdxAtSlot` movingNote fallback (smallest, low risk)
2. **2a.1** — fader handlers → `focus.last`
3. **2a.3** — select/mode clears; bridge in overlap move entry
4. **2a.4** — `MidiFaderProcessor`
5. **2a.5** — remove dead sync helpers; document bridge in header

Verify: `pio test -e native` after each step.

---

## Sign-off (cheap)

```bash
pio test -e native
.venv/bin/python scripts/verify_overlap_hidden_ac.py \
  captures/host_midi_automation_edit_baseline_20260619_233328_serial.log
rg 'movingNote\.(note|lastStart|lastEnd|active)' src/NoteEditManager.cpp  # → bridge only or 0
rg 'movingNote' src/MidiFaderProcessor.cpp  # → 0
```

Parent `edit.ok=false` remains **non-gating** (**C16**).

---

## After 2a

| Next | Change |
|------|--------|
| **2b** | Encoder → `applyNoteEditChange` (parent §8.1) — propose separately or extend tasks |
| **2c** | Remove **deletedNotes** from `NoteMovementUtils` |
| **2d** | Delete `MovingNoteIdentity` |

---

## Suggested first message

> `/opsx:apply note-edit-focus-reads` — Phase 2a only. Read `HANDOFF-BRIEF.md` + PRE-EXEC §1–§4.
> Replace **movingNote** UI reads with **focus.last** in NoteEditManager + MidiFaderProcessor.
> Do not touch EditStartNoteState or NoteMovementUtils overlap body.
