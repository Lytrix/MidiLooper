# Architectural refinement — Decoupling Slot identity from Loop identity

**Kind:** Long-term architecture refinement  
**Date:** 2026-07-14  
**Status:** Future direction — **not** part of DEC-024 Phase 2 implementation  
**Related:** [loop_undo_ownership_refinement.md](loop_undo_ownership_refinement.md), [loop_undo_ownership_phase2_audit_refinement.md](loop_undo_ownership_phase2_audit_refinement.md), [DEC-024](../DECISION_LOG.md#dec-024--loop-owned-undo-ownership-direction)

---

## Motivation

The current firmware largely treats **Slot** and **Loop** as the same runtime object:

```
slot == loop
```

This has worked while every slot permanently owns exactly one loop.

Future functionality may require **more loops than physical slots**, for example:

- keeping deleted loops available for later recovery;
- maintaining a library of recorded ideas;
- loading older loops back into an empty slot;
- scene or playlist management;
- projects with 16, 32+ stored loops while exposing only a limited number of physical buttons.

These use-cases suggest Slot and Loop should eventually become separate concepts.

---

## Architectural direction

**Today (implicit):**

```
Track
 ├── Slot 0 (Loop)
 ├── Slot 1 (Loop)
 └── Slot N (Loop)
```

**Target (future):**

```
Workspace
 └── Loop Library
      ├── Loop #1
      ├── Loop #2
      ├── Loop #17
      └── Loop #42

Track
 ├── Slot Assignment 0 ──► Loop #17
 ├── Slot Assignment 1 ──► Loop #42
 ├── Slot Assignment 2 ──► Loop #8
 └── Slot Assignment 3 ──► empty
```

A **Slot** is a routing or playback assignment. A **Loop** is the persistent musical object.

---

## Ownership (future)

```
Workspace
    owns Loop Library

Loop
    owns Capture, Passes, Playback state, Undo history, Geometry, Metadata

Track
    coordinates Loops (Slot assignments, playback coordination; selection via TrackManager)
```

Track no longer owns musical data or undo history directly.

---

## Slot vs Loop identity today

Both identifiers exist on `UndoEntry` ([`include/GlobalUndoStack.h`](../../include/GlobalUndoStack.h)):

```cpp
UndoEntry {
    slotIndex;   // compatibility field during migration
    loopId;      // persistent musical identity
}
```

**Current apply routing** ([`TrackUndo::applyUndoEntry`](../../src/TrackUndo.cpp)):

```
slotIndex → track.getLoop(slotIndex)    // 1:1 pool era
```

**Future routing (not Phase 2):**

```
slotIndex → SlotAssignment → loopId → Loop   // library / reassignment era
```

During Phase 2 migration, **`slotIndex` remains on wire and in apply** as a compatibility field. **`loopId` is the persistent musical identity** — retained on every push and in persistence. Phase 2 does not implement SlotAssignment but must not block future resolution through it.

---

## Future identity model

### Slot ID

Physical / UI assignment — may change over time:

- active playback position;
- selected button;
- LED/display routing;
- temporary assignment.

### Loop ID

Musical object — stable for the lifetime of the loop:

- persistence;
- undo ownership;
- pass ownership;
- snapshots;
- metadata;
- cross-system references.

### Relationship (future)

Instead of `slotIndex == loopId`, evolve toward:

```
SlotAssignment { slotIndex; loopId; }

slotIndex → assignedLoopId → Loop
```

Assignment may change without affecting the Loop.

---

## Undo implications

### Ownership invariant

**Undo history belongs to the Loop.**

Changing Slot assignment must **never** modify or invalidate Loop history.

Per-loop undo aligns with Loop-owned history:

```
Loop #42
    Passes
    Geometry
    Undo History
```

If Loop #42 is reassigned to another slot, undo history stays with the loop. **Undo belongs to Loop, not Slot** — consistent with DEC-024 Phase 2 direction.

---

## Delete semantics (future)

| Operation | Slot | Loop |
|-----------|------|------|
| **Unload slot** | Loses assignment | Remains in library |
| **Delete loop** | Assignment cleared if any | Removed from library |

Enables recovery of previously recorded material.

---

## Persistence implications (future)

```
Workspace
 ├── Loop Library (Loop #1, #2, …)
 └── Track
       └── Slot Assignments
```

Supports loop archives, load/unload, favorites, playlists, and more stored loops than hardware buttons.

Normative direction overlaps parked OpenSpec: [`workspace-session-persistence`](../../openspec/changes/workspace-session-persistence/), [`set-revision-persistence`](../../openspec/changes/set-revision-persistence/).

---

## Guidance for DEC-024 Phase 2 (current work)

Phase 2 **must not** implement the Loop Library model. It **only** moves undo stack ownership to `Loop`. The **1:1 Slot → Loop** relationship is unchanged.

### Phase 2 non-goals

- Loop library
- Slot reassignment
- Multiple slots referencing one Loop
- Loop archival
- Workspace loop catalog

### Phase 2 does (compatibility only)

Avoid new assumptions that permanently couple `slotIndex` and `loopId`:

| Do | Avoid |
|----|--------|
| Treat **Loop** as owner of musical state and undo history | Slot-owned undo APIs or slot-index-as-primary identity in new code |
| Keep runtime APIs oriented around `Loop&` where practical | Removing `loopId` from wire format before long-term identity is decided |
| Preserve `loopId` on `UndoEntry` and in persistence | **`loopId` = persistent musical identity** |
| Retain `slotIndex` on wire during migration | **`slotIndex` = compatibility field** (apply/reclaim/split in 1:1 era) |
| Plan reclaim/apply against **Loop** stacks | Naming that implies Track or Slot owns pass/snapshot data |
| Do not require `loopId` validation against slot on apply yet | Future apply may use SlotAssignment → `loopId` → Loop |
| **Do not move UI selection onto Track or Loop** | **TrackManager** continues to resolve selected Loop — unchanged in Phase 2 |

Task 0 audit **R5**: keep both fields; `slotIndex` is not the long-term identity anchor.

---

## Long-term principle

> A Slot is part of the user interface. A Loop is part of the musical domain.  
> Slots may change. Loops persist. Ownership follows the Loop, not the Slot.

---

## Related

- [multi-loop-slots spec](../../openspec/specs/multi-loop-slots/spec.md) — current 8-slot model
- [slot-selection-focus](../../openspec/changes/slot-selection-focus/) — selected vs active slot
- [phase-3-multi-loop.md](phase-3-multi-loop.md) — jam/scenes (D13+ deferred)
