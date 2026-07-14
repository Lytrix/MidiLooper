# Loop undo ownership refinement

**Kind:** Architecture refinement  
**Date:** 2026-07-08  
**Status:** Accepted guidance — Phase 1 shipped as compatibility layer; Phase 2 deferred

---

## Purpose

Sidebar `U:nn` showed track-wide undo depth and did not change when switching loops. Phase 1 fixes display and routing by filtering the existing track-wide `GlobalUndoStack` per selected loop. Longer term, undo ownership should move from **Track** to **Loop** to match Capture, Passes, Playback, and Persistence.

---

## Ownership direction

### Before (today — Phase 1)

```
Track
 ├── Passes          (via Loop pool — coordinated at Track level)
 ├── Undo            (GlobalUndoStack on Track; filtered per slot)
 └── Loops           (pool; musical state largely on Loop already)
```

Undo execution: `TrackUndo`. Selection: `TrackManager` → selected `Loop&`.

### After (Phase 2 target)

```
Track
 └── Coordinates Loops

Loop
 ├── Passes
 ├── Capture
 ├── Undo
 ├── Geometry
 └── Playback state
```

Track remains coordinator only (playback, selection via TrackManager, transport). **TrackUndo** still executes undo/redo; **Loop** owns undo **history**.

**Principle:** Slot is a UI address; **Loop** is the musical object. Prefer `loop.undo()` / `loop.undoDepth()` over `undoForSlot(track, slotIndex)`.

### Ownership invariant

**Undo history belongs to the Loop.**

Changing Slot assignment must **never** modify or invalidate Loop history.

Phase 2 implements this invariant for the current 1:1 model; future SlotAssignment must preserve it when a Loop moves between slots or is unloaded from a slot.

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

**2026-07-14 review additions:** Phase 2 is a **reference ownership migration** — Task 0 audit **complete**. **Prerequisite before Stage 1:** record capture baseline geometry must be Loop-owned (only push-time Track dependency found). Persistence-first wire format, two-stage migration, **Loop owns history** / **TrackUndo owns execution**.

**Future (not Phase 2):** [slot_loop_identity_decoupling_refinement.md](slot_loop_identity_decoupling_refinement.md) — Slot vs Loop identity decoupling and Loop Library.

### Phase 2 prerequisite (blocking)

Record baseline geometry must become **Loop-owned** (or captured from `Loop` at record entry) before per-loop undo stack ownership is enabled. Today: `Track::recordCaptureBaselineGeometry_` → `pushRecordPassAdded`. Only push-time Track-local dependency from Task 0 audit. Detail: [loop_undo_ownership_phase2_audit_refinement.md](loop_undo_ownership_phase2_audit_refinement.md).

### Phase 2 non-goals

Phase 2 does **not** implement: Loop library, slot reassignment, multiple slots per Loop, loop archival, or workspace loop catalog. **1:1 Slot → Loop remains unchanged.** Scope is undo ownership on `Loop` only — to keep future Slot/Loop decoupling possible.

### Identity fields during migration

| Field | Phase 2 role |
|-------|----------------|
| `slotIndex` | **Compatibility field** — wire format, legacy stack split, apply/reclaim routing in 1:1 era |
| `loopId` | **Persistent musical identity** — on every push and in persistence |

Future runtime routing may resolve `Loop` through **SlotAssignment** rather than direct slot indexing. Phase 2 retains both fields and does not implement SlotAssignment.

### Selection ownership (unchanged)

**Selection ownership is unchanged.** `TrackManager` remains responsible for resolving the selected Loop. Phase 2 does not move UI selection into `Track` or `Loop`.

---

## Related

- [**loop_undo_ownership_phase2_handoff.md**](loop_undo_ownership_phase2_handoff.md) — **implementation handoff** (start next session here)
- [loop_undo_ownership_phase2_audit_refinement.md](loop_undo_ownership_phase2_audit_refinement.md) — Task 0 audit, migration safety, persistence-first
- [slot_loop_identity_decoupling_refinement.md](slot_loop_identity_decoupling_refinement.md) — future Slot vs Loop identity (out of Phase 2 scope)
- [`docs/Guides/control-surface/Loops.md`](../Guides/control-surface/Loops.md) — double/triple undo/redo
- [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — global undo routing
- [DEC-024](../DECISION_LOG.md#dec-024--loop-owned-undo-ownership-direction) — decision log entry
