# NOTE_EDIT playing display stall — design session

**Status:** DEC-054 Stage 1 firmware in tree — device recapture open  
**Kind:** architecture  
**Evidence:** [`201106`](../../captures/session_20260908_201106.log)  
**Implementation plan:**
[`note_edit_display_playing_stall_refinement.md`](note_edit_display_playing_stall_refinement.md)  
**Parent scheduling contract:**
[`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md)  
**Bounds inventory:**
[`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md)  
**Decision:** [DEC-054](../DECISION_LOG.md#dec-054--note-edit-playing-display-stall-remainder-units)

Does **not** start: hydrate Stage 4a, raise snapshot idle `N`, OpenSpec 4.5 / 4.6,
interval reservation, `RuntimeWorkBudget::admit`.

---

## Formal reassessment

| Field | Content |
|---|---|
| **Reason triggered** | Execution-bound / interval-path work: `openNoteEditSession` can run from MIDI button dispatch (row 20 nest). `processKindBoundaryUndoWarm` runs in `ControlSurfaceManager::update()` on the `loop()` prefix between `handleMidiInput()` entries. Both are content-sized and have no inventory row. Converting them is a design session ([DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property)). Mode-switch defer also changes **when** `EditSession.active` flips. |
| **Current architecture** | `EditManager` owns session lifecycle and session undo. `ControlSurfaceManager` owns NOTE_EDIT input and already defers playing move/length/pitch (`PlayingEditGeometryDefer`). `DisplayManager` already admits frames at `LCD::DISPLAY_UPDATE_INTERVAL` (30 ms) and phases Resolve / Draw / Transfer. |
| **Proposed evolution** | **Extend** `EditManager`. `ControlSurfaceManager` only requests / marks pending. `EditManager` owns pending state, execution, and publication. Keep the last composed display frame. Admit one **registered** remainder unit after display admission. No new Manager. No `RuntimeWorkBudget`. |
| **Migration impact** | Firmware + native + HITL/device `DFRAME` gaps. No SD schema. Undo entry shape unchanged. |
| **Recommendation** | DEC-054 accepted with the Stage 1 bound gate. Stage 1 firmware only after the whole-warm remainder unit is inventoried with a measured bound and overrun policy. Stage 2 after Stage 1 capture. |
| **Approval required** | YES — granted 2026-09-08 with the bound-gate amendment |

Architecture checkpoint:

| Question | Answer |
|---|---|
| Ownership change? | NO — `EditManager` keeps session and session-undo. `DisplayManager` still only paints. |
| State-transition change? | YES — `EditSession.active` / LOOP_EDIT↔NOTE_EDIT flip only after the deferred unit completes. Button/fader turns only set pending. |
| Unbounded MIDI-interval owner? | YES today. The design removes those complete operations from the button/fader turn and admits them as inventoried remainder units. |

---

## Problem (device fact)

Display already targets 30 ms. In `201106`, paint itself stays under that budget.
The cooperative `loop()` does not reach paint while NOTE_EDIT open and select
undo-warm run to completion.

`DFRAME` fields: notes, frame µs, frame index (every 30 frames), resolve, draw,
transfer.

| Clock | Fact |
|---|---|
| PLAYING before NOTE_EDIT | `DFRAME` gaps **1.43–1.47 s** / 30 frames (~48 ms/frame including wait) |
| 149.806–149.888 s | Edit-mode short press → `openNoteEditSession` → `EditSession opened` (**82 ms**) |
| 153.500–160.869 s | `DFRAME` **7.37 s** for 30 frames while select faders move |
| 154.264–165.365 s | Twelve `UNDO_WARM,warm,complete` **91.7–133.2 ms** each. `focus_snap` is **91–132 ms** |
| Whole file | **0** `GEOM_APPLY` lines |

Paint during those `DFRAME`s: frame **18–25 ms**, resolve **1.5–2.0 ms**,
draw **2.0–2.6 ms**, transfer **~4.5 ms**.

`shouldForceNoteEditDisplayUpdate()` cannot preempt a 90–133 ms owner on the
same turn.

---

## Current owners

```text
MIDI button / cycle
    → EditManager::openNoteEditSession   (blocking)
    → EditManager::exitEditMode
         → closeNoteEditPass / bake
         → closeNoteEditSession          (blocking)

Select fader (this capture)
    → applySelectNav
         → scheduleKindBoundaryUndoWarm
    → ControlSurfaceManager::update
         → processKindBoundaryUndoWarm   (blocking buildSessionUndoEntry)
         → (then) maybeUpdateDisplayForNoteEditSelection

Move / length / pitch while PLAYING
    → PlayingEditGeometryDefer           (already queued)
```

`buildSessionUndoEntry` logs `focus_snap` around
`snapshotFocusForSessionUndo` **and** `NoteEditCurrentState::clone()` /
`assignFrom` (full `rows_` copy). In `201106` that phase is 91–132 ms for
**137** current-state rows. Later phases (`baseline_probe`, `edit_rows`) are
small on a select-only warm.

`kDeferredNoteEditPlaybackRefreshIdleMs` (**80 ms**) already settles deferred
playback-cache refresh. Playing geometry already uses pending + `process*`.

These two stalls have **no** `ExecutionBounds.md` inventory row.

---

## Ownership

```text
ControlSurfaceManager
    → request / mark pending

EditManager
    → owns pending state
    → owns execution
    → owns publication
```

`DisplayManager` paints the last composed frame. It does not own edit state.

## Invariant

```text
While transport is running, a NOTE_EDIT mode switch or select-fader
kind-boundary undo warm must not occupy the display-admission turn.
Display keeps the last composed frame and continues 30 ms admission.

A remainder unit is admitted only after it has an ExecutionBounds row
with a measured bound and an overrun policy. Overrun keeps the work
pending and does not publish partial undo or session state.

Open: LOOP_EDIT remains authoritative until rematerialize finishes,
then EditSession.active and NOTE_EDIT flip together.

Close: the old NOTE_EDIT session remains authoritative until bake
commits. Then session and LOOP_EDIT flip together.
EditSession.active = false while bake is still pending is forbidden.

Playing move/length/pitch stay on PlayingEditGeometryDefer.
```

---

## Options

### Fix 1 — select-fader undo warm

| Option | What | Reject |
|---|---|---|
| **A — Coalesce + post-display process (Stage 1)** | Keep one `kindBoundaryUndoWarmPending_`. Stamp armed time on each select change (same pattern as `kDeferredNoteEditPlaybackRefreshIdleMs`). After settle **and** after that turn’s display admission, run one whole `processKindBoundaryUndoWarm` only if that whole-warm is registered as an empirically bounded remainder unit with an overrun policy. | Moving the warm after display is not itself a bound. |
| B — Slice `clone()` / `assignFrom` | Convert the 91–132 ms step into a resumable unit | Not Stage 1 unless the registered whole-warm bound is unacceptable for the 30 ms display cadence. A local µs budget does not bound `clone()`. |
| C — Drop warm during PLAYING | Push undo only on kind change / stop | Changes undo availability while playing. Not this design. |
| D — New scheduler / `RuntimeWorkBudget` | Global admit class | Rejected by DEC-043 / scheduling contract. |

### Fix 2 — mode switch open / close

| Option | What | Reject |
|---|---|---|
| **E — Pending open/close, session flips when the unit completes (Stage 2)** | Button/cycle sets `pending` only. Last `DFRAME` keeps painting. Remainder steps rematerialize / bake. Old session stays authoritative until the unit commits, then `EditSession.active` and LOOP_EDIT↔NOTE_EDIT flip together. | — |
| F — Flip UI immediately, rematerialize in background | Faders and SELECT would see a half-open session | `openNoteEditSession` today assigns store, current state, and default SELECT together. |
| G — Keep blocking open/close; only fix warm | Leaves the 82 ms mode-switch stall on the button turn (interval nest) | User asked for both fixes. |

---

## Decision (DEC-054)

1. Extend `EditManager`. `ControlSurfaceManager` only requests / marks
   pending. Do not add a Manager or scheduling class.
2. Select kind-boundary undo warm: one pending flag; settle 80 ms
   (`kDeferredNoteEditPlaybackRefreshIdleMs`); process after display
   admission on that turn. Do not invent a second settle policy.
3. Stage 1 does **not** authorize firmware merely because the warm is
   moved after display admission. The existing whole-warm operation must
   first be registered as an empirically bounded remainder unit with an
   explicit measured bound and overrun policy:

   ```text
   kindBoundaryUndoWarm:
       - one coalesced warm
       - starts only after display admission
       - execution bound measured on representative worst case
       - if bound is exceeded:
             remain pending
             do not publish partial undo state
   ```

   Seed measurement: `201106` one warm **91.7–133.2 ms** (137 rows).
   If Stage 1 device capture shows that worst-case bound is unacceptable
   for the 30 ms display cadence, Stage 1 is not closed — Option B
   (slice `clone()` / `assignFrom`) is required before Stage 2.
4. NOTE_EDIT open and close (Stage 2, after Stage 1 capture):
   button/cycle/exit only queue pending work. Display keeps the last
   composed frame. Old session remains authoritative until the unit
   commits, then session and mode flip together. `EditSession.active =
   false` while bake is pending is forbidden.
5. Playing move/length/pitch stay on `PlayingEditGeometryDefer`.
6. Inventory rows land in the same commit as the firmware that admits
   the unit.
7. Interval reservation, `RuntimeWorkBudget::admit`, hydrate Stage 4a,
   raise `N`, `T_RT_reserve`, and OpenSpec 4.5 / 4.6 stay unauthorized.

### Alternatives considered

| Alternative | Rejected because |
|---|---|
| Faster display interval | `201106` paint is already 18–25 ms |
| Force extra NOTE_EDIT paints | Cannot preempt `processKindBoundaryUndoWarm` |
| Treat post-display whole-warm as bounded without a gate | Relocates a 91–133 ms unit; does not classify it |
| Slice `clone()` in Stage 1 | Not yet — only if the registered whole-warm bound fails the display cadence |
| Optimistic session flip | Half-open session on faders |
| `active = false` while bake pending | Close would lose the authoritative NOTE_EDIT session |

---

## Historical review

| Source | Finding |
|---|---|
| DEC-043 | Bound class is gated. No `admit(WorkClass)`. |
| DEC-027 | `DeferredJobScheduler` stays load-frame only. Do not route NOTE_EDIT session work through it. |
| DEC-053 | Snapshot idle yield/reserve is a different owner. Do not reuse `T_RT_reserve` here. |
| Scheduling contract | Convert pathological units; do not add interval reservation. |
| `PlayingEditGeometryDefer` | Reuse pending + `process*` for geometry. Select warm is a different owner method. |
| `kDeferredNoteEditPlaybackRefreshIdleMs` | Reuse settle pattern and the 80 ms constant. |

Reuse decision: **YES** — extend `EditManager` pending/`process*` and the existing display-admission order.

---

## Out of scope

- Hydrate Stage 4a / raise `N`
- OpenSpec 4.5 / 4.6
- Changing `SessionUndoEntry` restore semantics
- Bounding `NoteUtils::reconstructDisplayNotes` (inventory row 8)
- Interval reservation

---

## Open before firmware

None. DEC-054 accepted 2026-09-08 with the Stage 1 bound gate.
Stage 1 authorized. Stage 2 waits for Stage 1 device capture.
