# NOTE_EDIT selection tick alignment — bugfix handoff

**Date:** 2026-07-06  
**Trigger:** [`captures/session_20260706_013151.log`](../captures/session_20260706_013151.log) — coarse/fine move cleared `selectedNoteIdx`; faders 2–4 reported “No note selected”.

## Root cause

Geometry commit paths wrote **storage** ticks into `EditorSelection.selectedTick` via `storageTickToDisplayPhase` (origin 0), while lookup uses `displayStartTickFromStorage` with **`loopStartTick`**. On loops with non-zero `loopStartTick`, `filteredDisplayNoteIndexForSelection` returned -1 and `syncSelectedNoteIdxToFilteredInventory` cleared the index.

## Shipped fix

| Area | Change |
|------|--------|
| `NoteMovementUtils.cpp` | `bracketDisplayTickFromStorage` for move/pitch/length geometry; retry selection rebind in `finalReconstructAndSelect` |
| `EditManager.cpp` | `syncSelectedNoteIdxToFilteredInventory` corrects bracket tick from focus and avoids clearing index when note still present |
| `DisplayManager.cpp` | Highlight + note info resolve by `primaryNote` + display-phase bracket on the drawn note list |
| `test_note_edit_fader_feedback` | +3 regression cases (loopStart 0 / 11 / capture mirror) |

## Verification

- `pio test -e native` — full suite PASS (2026-07-06)
- Manual: NOTE_EDIT on 1536-tick loop — fader1 select → coarse/fine move → faders 2–4 edit (pending hardware)
- HITL edit baseline — pending after firmware flash
