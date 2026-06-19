# Proposal — note edit focus reads (Phase 2a)

**Change:** `note-edit-focus-reads`  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/)  
**Continues:** [overlap-hidden-note-select Phase 2a](../archive/2026-06-19-overlap-hidden-note-select/tasks.md) (archived 2026-06-19)

## Problem

Phase 1 unified **read** inventory (`filterSelectableDisplayNotes`). Fader UI in
`NoteEditManager` and `MidiFaderProcessor` still reads **`movingNote.*`** for live edit
geometry — duplicates **focus.last** and caused Phase 1 disambiguation debt
(`resolveNoteIdxAtSlot` movingNote fallback).

## Scope (2a only)

- **Read** live mover geometry from **`NoteEditFocus`** (`focus.active`, `focus.last`,
  `focus.movingNoteRange`) in **NoteEditManager** fader paths and **MidiFaderProcessor**.
- Remove **movingNote** fallback in `resolveNoteIdxAtSlot`.
- Remove **`syncFocusLastFromMovingNote`** / redundant dual sync where **focus** is source.
- **One bridge write:** before calling overlap utils, **`syncMovingNoteFromFocus`** only
  (writer path for `NoteMovementUtils` until Phase 2c) — no UI reads from **movingNote**.

## Out of scope (do not touch in this change)

| Phase | Files / work |
|-------|----------------|
| **2b** | `EditStartNoteState.cpp`, `EditPitchNoteState.cpp`, `EditLengthNoteState.cpp` encoder |
| **2c** | `NoteMovementUtils.cpp` **deletedNotes** / overlap write retirement |
| **2d** | Delete `MovingNoteIdentity` from `EditManager.h` |
| **Phase 3** | Commit boundary module |

## Verification

- `pio test -e native`
- Re-run AC1–AC5 verifier on existing capture or one smoke HITL — must not regress Phase 1
- Grep: no `movingNote.(note|lastStart|lastEnd|active)` **reads** in `NoteEditManager.cpp`
  except documented bridge comment block

## References

- Shipped spec: `openspec/specs/overlap-hidden-note-select/spec.md`
- Archive: `openspec/changes/archive/2026-06-19-overlap-hidden-note-select/`
- AC verifier: `scripts/verify_overlap_hidden_ac.py`
