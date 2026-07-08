# Loop undo ownership refinement

**Kind:** Architecture refinement  
**Date:** 2026-07-08  
**Status:** Accepted guidance — Phase 1 shipped as compatibility layer; Phase 2 deferred

---

## Purpose

Sidebar `U:nn` showed track-wide undo depth and did not change when switching loops. Phase 1 fixes display and routing by filtering the existing track-wide `GlobalUndoStack` per selected loop. Longer term, undo ownership should move from **Track** to **Loop** to match Capture, Passes, Playback, and Persistence.

---

## Ownership direction

**Today (Phase 1 compatibility):**

```
Track
 └── GlobalUndoStack
      └── filtered by loop slot index via TrackUndo::*ForLoop
```

**Target (Phase 2):**

```
Track (coordinator)
 ├── Loop 0 + GlobalUndoStack
 ├── Loop 1 + GlobalUndoStack
 └── Loop N + GlobalUndoStack
```

**Principle:** Slot is a UI address; **Loop** is the musical object. Prefer `loop.undo()` / `loop.undoDepth()` over `undoForSlot(track, slotIndex)`.

Phase 1 bridges with `TrackUndo::undoForLoop(track, loop)` — callers resolve `Loop&` once; `TrackUndo` still owns the stack.

---

## Phase 1 (shipped)

- `countAppliedUndoEntriesForSlot` / `countRedoEntriesForSlot` on `GlobalUndoStack`
- `TrackUndo::undoDepthForLoop`, `canUndoForLoop`, `undoForLoop` (+ redo/clear variants)
- Sidebar `U:` via `EditManager::getDisplayUndoCount(track, loop)`
- `MidiButtonActions::handleUndo` / `handleRedo` gate on selected loop stack tip
- Native: `test_global_undo_slot_scope`

---

## Phase 2 (future)

Move `GlobalUndoStack` member from `Track` to `Loop`. Touch: `TrackUndo`, `PassReclaim`, `RuntimeBundleFooter` persistence, `test_redo_functionality`. Requires design session before implementation (ownership change).

---

## Related

- [`docs/Guides/control-surface/Loops.md`](../Guides/control-surface/Loops.md) — double/triple undo/redo
- [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — global undo routing
- [DEC-024](#dec-024-loop-owned-undo-ownership-direction) — decision log entry
