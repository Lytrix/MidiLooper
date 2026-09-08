# NOTE_EDIT overlay delta — implementation

**Status:** Hydrate 1 device functional PASS [`224533`](../../captures/session_20260908_224533.log); DEC-055 Stage 3 device PASS [`221906`](../../captures/session_20260908_221906.log)  

**Kind:** refinement  
**Decision:** [DEC-055](../DECISION_LOG.md#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot)  
**Design:** [`note_edit_overlay_delta_architecture.md`](note_edit_overlay_delta_architecture.md)  
**Evidence:** [`224533`](../../captures/session_20260908_224533.log) Hydrate 1 functional PASS, [`221906`](../../captures/session_20260908_221906.log) Stage 3 PASS, [`220445`](../../captures/session_20260908_220445.log) Stage 2 PASS, [`212850`](../../captures/session_20260908_212850.log) Stage 1 PASS, [`204458`](../../captures/session_20260908_204458.log) (pre-fix fail)

Does **not** start: hydrate 2–5 / 4a, DEC-054 Stage 2, Option B, rename
`NoteEditCurrentState`, live-capture fold change, later **E:** overlay-delta
entries, open / focus-rebuild fill removal.

---

## Debugging boundary

```text
Select clone / UNDO_WARM on applySelectNav
    ← Stage 1
First kind-boundary hasUndoCurrentState / E:00 restore
    ← Stage 2
NoteGeometryResolver visual-cache current-state fill
    ← Stage 3
open / focus-rebuild ensureCurrentStateVisibleRowsFromVisualCache
    ← later
SelectNavigation neighborhood / fader tick-linear
    ← Hydrate 1
open rematerialize
    ← hydrate 4a
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

## Stage 3 — Analyze does not fill current state from visual cache

```text
NoteGeometryResolver::resolve
  → overlay / evaluation scope from existing current state + visualCache
  → no ensureCurrentStateVisibleRowsFromVisualCache
```

Paint still composes `visualCache` + overlay. Open, focus rebuild, delete
empty-state fallback, and live-capture fold still call the fill.

**Invariant:** Geometry resolve does not insert every visible cache note
into `NoteEditCurrentState`.

**Native:** `analyzeFillsCurrentStateFromVisualCache()` is false.

**Device:** PLAYING + NOTE_EDIT + first Move. Stage 2 `push,e00` stays.
`GEOM_APPLY,resolve` still `1,3,0` on a real Move. Select `DFRAME` stays
in the Stage 1/2 band.

**Device PASS [`221906`](../../captures/session_20260908_221906.log):**
`NOTE_EDIT_OPEN` `current=1` `root_rev=7` **77** notes after
`PLAYING -> STOPPED`. First Move (POSITION EDIT, kind=3) logs
`UNDO_WARM,push,e00,734,77,3` then `GEOM_APPLY,resolve,9669,1,3,0`.
First-Move `phase,setup` **303–322** µs vs [`220445`](../../captures/session_20260908_220445.log)
**478–726** µs. Select-only `DFRAME` before that Move: min **1561** ms,
median **1648** ms, max **2268** ms (open window). Later kind-boundaries
still clone: 7 `focus_snap` at **37–137** ms (`push,cache_miss`).

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

### Files (Stage 3)

| File | Change |
|---|---|
| `include/NoteEditSessionState.h` | `analyzeFillsCurrentStateFromVisualCache()` = false |
| `src/EditManager/NoteGeometryResolver.cpp` | Skip visual-cache current-state fill |
| `test/test_note_edit_session_state/` | Pin the policy |
| this plan + CURRENT_WORK | Stage 3 authorized |

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

---

## Pre-implementation review (Stage 3)

### Ready

- Analyze fill is one call in `NoteGeometryResolver::resolve`
- `resolveForCausingNote` routes through `resolve`
- Open / focus rebuild / delete / live-capture fold stay

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Stage 3 scope | Resolver does not fill current state from visual cache |
| Open fill | Hydrate 4a |
| Focus-rebuild fill | Display span align; stays |
| Later **E:** overlay-delta | Not this stage |

### Open before coding

None.

### Proceed?

YES — Stage 3 only.

---

## Pre-implementation review (Hydrate 1)

### Ready

- `SelectNavigation::buildSelectNavigationSlots` owns slot emission
- Fader `resolveFader1SelectTarget` / `applyNoteSelectFromFader1Pitchbend` rebuild from pitchbend

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Neighborhood | ±1 16th around `selectedTick`, wrap |
| Fader | Tick-linear over `navLength`, then neighborhood |
| LCR / hydrate 2–5 | Not this stage |

### Open before coding

None.

### Proceed?

YES — Hydrate 1 only.

---

## Hydrate 1 device ([`224533`](../../captures/session_20260908_224533.log))

Neighborhood select and first `push,e00,761,88,3` hold. Fast fader 2 / bar
sweep leftover: `hostdrain` **1.45 s** on stacked Move `GEOM_APPLY,resolve`
(**29 ms**; reconstruct/analyze/overlay). Select-only `DFRAME` stays in the
Stage 3 band. Do not reopen Hydrate 1 for that leftover.
