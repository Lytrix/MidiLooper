# NOTE_EDIT overlay delta — implementation

**Status:** DEC-055 Stage 2 device PASS [`220445`](../../captures/session_20260908_220445.log)  
**Kind:** refinement  
**Decision:** [DEC-055](../DECISION_LOG.md#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot)  
**Design:** [`note_edit_overlay_delta_architecture.md`](note_edit_overlay_delta_architecture.md)  
**Evidence:** [`220445`](../../captures/session_20260908_220445.log) Stage 2 PASS, [`212850`](../../captures/session_20260908_212850.log) Stage 1 PASS, [`204458`](../../captures/session_20260908_204458.log) (pre-fix fail)

Does **not** start: hydrate 1–5, DEC-054 Stage 2, Option B, rename
`NoteEditCurrentState`, live-capture fold change, later **E:** overlay-delta
entries.

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

**Device PASS [`212850`](../../captures/session_20260908_212850.log):**
`NOTE_EDIT_OPEN` `current=1` `root_rev=4` **79** notes. Select pitchbend
lines **462**. `UNDO_WARM` / `focus_snap` / `GEOM_APPLY` = **0**. `DFRAME`
every 30 frames during select: min **1237** ms, median **1251** ms, max
**1789** ms (session-open window `fidx` 660→690). Paint **8116–24401** µs.
Compare [`204458`](../../captures/session_20260908_204458.log) after-open:
median **1645** ms, max **4594** ms, **23** `warm,complete`.

---

## Stage 2 — first kind-boundary E:00 restore

```text
undoCount == 0
  → first geometry kind-boundary
  → buildOpenedSnapshotKindBoundaryUndoEntry
  → hasUndoCurrentState == false
  → no current-state clone
  → no full-cache fill
undo
  → applySessionUndoEntry (opened snapshot + empty editRows)
  → buildFromSessionStore from restored store
```

`undoCount > 0` still uses `buildSessionUndoEntry` + clone. Live-capture fold
stays its existing entry. Select still does not schedule warm.

**Invariant:** The first **E:** push after open does not call
`NoteEditCurrentState::clone()`. Undo of that entry restores session store
from `openedSnapshotRevision` without a stored current-state copy.

**Native:** `firstKindBoundaryUsesOpenedSnapshotRestore(0)` is true.
Opened-snapshot entry has `hasUndoCurrentState == false` and restores
span NoteIds from the opened snapshot.

**Device:** PLAYING + NOTE_EDIT + one first Move (or Pitch/Length). Pass:
no `UNDO_WARM,phase,focus_snap` on that first kind-boundary; `push,e00`
(or equivalent) with `hasUndoCurrentState` false. Select-only cadence from
Stage 1 stays. Later kind-boundaries may still clone once.

**Device PASS [`220445`](../../captures/session_20260908_220445.log):**
`NOTE_EDIT_OPEN` `current=1` `root_rev=4` **79** notes after transport
`STOPPED -> PLAYING`. Select pitchbend lines **211**. First Move
(POSITION EDIT, kind=3) logs `UNDO_WARM,push,e00,1358,79,3` then
`GEOM_APPLY,undo,ok,1864`. `focus_snap` / `warm,complete` = **0**. Later
same-kind `undo,ok` is 0–2 µs (no second kind-boundary). Select-only
`DFRAME` 30-frame gaps: min **1482** ms, median **1579** ms, max **2089** ms
(open window `fidx` 750→780). Paint **10611–25800** µs. Compare
[`212850`](../../captures/session_20260908_212850.log) select-only:
median **1251** ms, max **1789** ms; [`204458`](../../captures/session_20260908_204458.log)
after-open max **4594** ms with **23** `warm,complete`.

---

## Files (Stage 1)

| File | Change |
|---|---|
| `include/NoteEditSessionState.h` | `selectNavSchedulesKindBoundaryUndoWarm()` = false |
| `src/EditManager/NoteEditSelection.cpp` | Do not schedule warm on select |
| `test/test_note_edit_session_state/` | Pin the policy |
| this plan + architecture + CURRENT_WORK | Stage 1 device PASS |

### Files (Stage 2)

| File | Change |
|---|---|
| `include/NoteEditSessionState.h` | `firstKindBoundaryUsesOpenedSnapshotRestore` |
| `include/NoteEditSessionUndo.h` | `buildOpenedSnapshotKindBoundaryUndoEntry` |
| `src/EditManager/NoteEditSessionUndoStack.cpp` | E:00 entry builder |
| `src/EditManager/NoteEditSessionUndo.cpp` | First kind-boundary uses E:00; restore rebuilds current state from store |
| `test/test_note_edit_session_state/` | Pin undoCount == 0 policy |
| `test/test_note_edit_session_undo/` | Pin no-clone entry + snapshot restore |
| this plan + CURRENT_WORK | Stage 2 authorized |

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

---

## Pre-implementation review (Stage 2)

### Ready

- `applySessionUndoEntry` already restores from
  `tryCopyNoteEditOpenedSnapshotEvents` when `hasUndoCurrentState == false`
- `buildSessionUndoEntry` clones only when `currentStateAtPush` is non-empty
- `snapshotFocusForSessionUndo` already trims to mover + overlap closure
- Stage 1 select no longer schedules warm, so first Move is a cache-miss today

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Stage 2 scope | First kind-boundary at `undoCount == 0` only |
| Later kind-boundaries | Still clone |
| Live-capture fold | Unchanged |
| Restore current state | Rebuild from restored store (`buildFromSessionStore`); not a push clone |
| Legacy `openedOnCurrentSnapshot == false` | Existing rematerialize miss |

### Open before coding

None.

### Proceed?

YES — Stage 2 only.
