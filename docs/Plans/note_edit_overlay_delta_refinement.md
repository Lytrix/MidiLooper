# NOTE_EDIT overlay delta — implementation

**Status:** DEC-055 accepted — Stage 1 firmware in tree, device recapture open  
**Kind:** refinement  
**Decision:** [DEC-055](../DECISION_LOG.md#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot)  
**Design:** [`note_edit_overlay_delta_architecture.md`](note_edit_overlay_delta_architecture.md)  
**Evidence:** [`204458`](../../captures/session_20260908_204458.log)

Does **not** start: Stage 2 E:00 first-kind-boundary, hydrate 1–5, DEC-054
Stage 2, Option B, rename `NoteEditCurrentState`, live-capture fold change.

---

## Debugging boundary

```text
Select clone / UNDO_WARM on applySelectNav
    ← Stage 1
First kind-boundary hasUndoCurrentState / E:00 restore
    ← Stage 2
ensureCurrentStateVisibleRowsFromVisualCache
    ← later
open rematerialize / SelectNavigation neighborhood
    ← hydrate
```

---

## Stage 1 — eliminate select `clone()` / full-row warm

```text
SELECT
  → selection / focus update
  → lastPushedGeometryKind_ = Select when identity changes
  → no undo push
  → no current-state clone
  → no scheduleKindBoundaryUndoWarm
```

`shouldResetGeometryKindUndoOnSelectChange` still resets kind so the next
geometry mutation pushes **E:**. That push may still cache-miss clone until
Stage 2. Stage 1 must not change `pushSessionUndoOnKindChange` restore shape.

**Invariant:** During PLAYING select motion, firmware does not run
`buildSessionUndoEntry` / `NoteEditCurrentState::clone()` from select.
`UNDO_WARM,warm,complete` count during select-only motion is 0.

**Native:** `selectNavSchedulesKindBoundaryUndoWarm()` is false. Select
identity change still resets geometry kind undo.

**Device:** PLAYING + NOTE_EDIT + select fader back-and-forth. `DFRAME`
30-frame gaps return to the pre-NOTE_EDIT band (~1.4–1.5 s). No
`UNDO_WARM,warm,complete` in the select-only window. First Move may still
clone once (Stage 2).

---

## Stage 2 — not authorized here

First kind-boundary at `undoCount == 0`: `hasUndoCurrentState == false`;
restore uses opened snapshot. Separate capture.

---

## Files (Stage 1)

| File | Change |
|---|---|
| `include/NoteEditSessionState.h` | `selectNavSchedulesKindBoundaryUndoWarm()` = false |
| `src/EditManager/NoteEditSelection.cpp` | Do not schedule warm on select |
| `test/test_note_edit_session_state/` | Pin the policy |
| this plan + architecture + CURRENT_WORK | Stage 1 in tree / device open |

---

## Pre-implementation review

### Ready

- Select already must not push **E:**
- Only `applySelectNav` calls `scheduleKindBoundaryUndoWarm`
- Geometry still cache-miss clones on first kind change

### Resolved

| Topic | Decision |
|---|---|
| Stage 1 scope | Select clone/warm only |
| E:00 first-kind-boundary | Stage 2 |
| Option B | Rejected |
| `NoteEditCurrentState` name | Keep |

### Open before coding

None for Stage 1.

### Proceed?

YES — Stage 1 only.
