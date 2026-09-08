# NOTE_EDIT playing display stall — implementation

**Status:** DEC-054 Stage 1 cadence FAIL [`204458`](../../captures/session_20260908_204458.log); select hitch closed by DEC-055 Stage 1 PASS [`212850`](../../captures/session_20260908_212850.log)  
**Kind:** refinement  
**Evidence:** [`201106`](../../captures/session_20260908_201106.log), [`204458`](../../captures/session_20260908_204458.log), [`212850`](../../captures/session_20260908_212850.log)  
**Decision:** [DEC-054](../DECISION_LOG.md#dec-054--note-edit-playing-display-stall-remainder-units)  
**Design:**
[`note_edit_display_playing_stall_architecture.md`](note_edit_display_playing_stall_architecture.md)

Stage 2 is not authorized until Stage 1 device capture.

Does **not** start: hydrate Stage 4a, raise `N`, OpenSpec 4.5 / 4.6.

---

## Debugging boundary

```text
Display 30 ms admission / paint phases
    ← closed (paint 18–25 ms in 201106)
PlayingEditGeometryDefer (move / length / pitch)
    ← closed (0 GEOM_APPLY in 201106)
processKindBoundaryUndoWarm
    ← Fix 1
openNoteEditSession / closeNoteEditPass / exitEditMode
    ← Fix 2
```

---

## Stages

### Stage 1 — coalesce and post-display undo warm

`applySelectNav` still calls `scheduleKindBoundaryUndoWarm()`.
`processKindBoundaryUndoWarm` does **not** run to completion on the
select/control-surface turn.

- Keep one pending flag.
- Stamp armed time on each select identity change.
- Process only after `kDeferredNoteEditPlaybackRefreshIdleMs` (80 ms) with no
  newer select change.
- Call process after that turn’s display admission (`finishAdmittedDisplayPaintTurn`
  / prefix display), not before it in `ControlSurfaceManager::update()`.
- Cache-hit push path is unchanged.

**Bound gate (required in the same commit as firmware):**

```text
kindBoundaryUndoWarm:
    - one coalesced warm
    - starts only after display admission
    - execution bound measured on representative worst case
    - if bound is exceeded:
          remain pending
          do not publish partial undo state
```

- Register the row in `ExecutionBounds.md` as **empirically bounded**.
  Seed measurement: `201106` **91.7–133.2 ms** (137 rows).
- Relocating the warm after display does not close this gate.
- If Stage 1 device capture shows that worst-case bound is unacceptable
  for the 30 ms display cadence, Stage 1 is not closed — Option B
  (slice `clone()` / `assignFrom`) is required before Stage 2.

**Invariant:** During PLAYING select motion, `DFRAME` 30-frame gaps stay in the
pre-NOTE_EDIT band (~1.4–1.5 s). Twelve stacked warms must not recur.
A single settled warm is a classified remainder unit, not an implicit
display-safe step.

**Native:** select changes while a warm is pending collapse to one process
after settle. Process is not invoked from `ControlSurfaceManager::update()`.
Overrun leaves pending and does not publish `kindBoundaryUndoCache_`.

**Device:** recapture PLAYING + NOTE_EDIT + select fader. Compare `DFRAME`
gaps and `UNDO_WARM,warm,complete` count to `201106`. Record one-warm
duration vs the inventoried bound. Stage 1 firmware is in tree; this
stage is not closed until that capture. `UNDO_WARM,warm,overrun` means
the seed bound rejected publication.

**Firmware shipped (device open):** `scheduleKindBoundaryUndoWarm` stamps
armed time and invalidates the cache. `processKindBoundaryUndoWarm`
keeps pending until 80 ms settle, builds a local entry, and publishes
only when `kindBoundaryUndoWarmMayPublish` is true. `ControlSurfaceManager::update()`
no longer calls process. `main.cpp` calls it after a completed display
frame. Inventory row 23. Native:
`test_kind_boundary_undo_warm_coalesce_and_settle`,
`test_kind_boundary_undo_warm_overrun_does_not_publish`.

### Stage 2 — pending NOTE_EDIT open and close

Button / cycle / `exitEditMode` queue pending open or close on `EditManager`.
They do not call `openNoteEditSession` / `closeNoteEditPass` /
`bakeNoteEditSessionStoreToPasses` on the gesture turn.

- Display keeps the last composed frame.
- Open: LOOP_EDIT remains authoritative until rematerialize finishes, then
  `EditSession.active` and NOTE_EDIT flip together.
- Close: the old NOTE_EDIT session remains authoritative until bake
  commits, then session and LOOP_EDIT flip together.
  `EditSession.active = false` while bake is pending is forbidden.
- Open/close units get the same bound-gate treatment as Stage 1 (inventory
  row, measured bound, overrun = remain pending, no partial publication).
- Add inventory rows in the same commit.

**Invariant:** Mode-switch gesture does not occupy the display-admission turn.
Session is never half-open to faders. Close never drops the authoritative
session before bake commits.

**Native:** queued open does not set `active` until the process unit finishes;
queued close keeps `active` and edits until bake completes.

**Device:** PLAYING cycle LOOP_EDIT↔NOTE_EDIT. `DFRAME` gaps stay in the
pre-switch band. `NOTE_EDIT_OPEN` appears after the gesture, not as an 82 ms
block on the button turn.

### Option B — rejected

Slice `clone()` / `assignFrom` is **rejected**. DEC-055 removes the full
copy from the select working set instead. First kind-boundary E:00 restore
is DEC-055 Stage 2, not a `clone()` slice.

---

## Files (expected)

| File | Stage | Change |
|---|---|---|
| `src/EditManager/NoteEditSessionUndo.cpp` | 1 | Settle + process after display |
| `src/EditManager/NoteEditSelection.cpp` | 1 | Keep schedule; stamp armed time |
| `src/ControlSurfaceManager.cpp` | 1 | Remove warm from `update()` |
| `src/main.cpp` | 1 | Process warm after display admission |
| `include/EditManager.h` | 1–2 | Pending / armed fields |
| `docs/Authority/Architecture/ExecutionBounds.md` | 1–2 | Inventory rows |
| `src/EditManager/NoteEditSessionLifecycle.cpp` | 2 | Queue open/close |
| `src/ControlSurface/NoteEditButtonInput.cpp` | 2 | Queue instead of immediate open |
| `src/EditManager/EditNoteStateCoordinator.cpp` | 2 | `enterEditMode` / `exitEditMode` queue |
| `src/EditManager/EditSessionDepart.cpp` | 2 | Same pending close/open |

---

## Tests

| Gate | What |
|---|---|
| Native Stage 1 | One pending warm after N select changes; process after settle; not from `ControlSurfaceManager::update()` |
| Native Stage 2 | Pending open does not set `active`; pending close does not drop edits |
| RAM1 | `teensy41-capture-serial` locals ≥ 4000 |
| Device Stage 1 | PLAYING select faders: `DFRAME` 30-frame gap in the ~1.4–1.5 s band; fewer `UNDO_WARM` than `201106` (12) |
| Device Stage 2 | PLAYING mode cycle: no 82 ms button-turn open; `NOTE_EDIT_OPEN` after process |

---

## Pre-implementation review

### Ready

- Owners and `201106` anchors named
- Reuse: pending + `process*`, 80 ms settle, display admission order

### Resolved (DEC-054)

| Topic | Decision |
|---|---|
| Owner | Extend `EditManager`. Control surface only marks pending |
| New scheduler | No |
| Stage order | Warm first; Stage 2 after Stage 1 capture |
| Whole-warm after display | Empirically bounded remainder unit + overrun policy required in the Stage 1 commit |
| Slice `clone()` | Not yet |
| Close | Old NOTE_EDIT stays authoritative until bake commits |
| Playing geometry | Unchanged |

### Open before coding

None for Stage 1. Stage 2 waits for Stage 1 device capture.

### Proceed?

YES — Stage 1 only. Bound-gate inventory row and overrun policy in the same
commit. Do not start Stage 2, hydrate Stage 4a, raise `N`, `T_RT_reserve`,
4.5, or 4.6.
