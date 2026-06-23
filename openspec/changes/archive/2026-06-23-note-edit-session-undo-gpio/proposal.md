## Why

Note-edit session undo pushes on every fader tick and mislabels sidebar **`U:`** during edit; the GPIO encoder module is dormant while DROID faders carry note edit alone. We need **geometry-boundary session undo** (**`E:`**), correct display, **`select_move_pitch_length`** on the panel encoder, and a revived **GPIO control surface** routed through a shared action layer (Option B) — without coupling encoder cycle to fader paths.

M8 **`NoteEditSession`** / pass storage is shipped; this change completes post-M8 edit **UX and
control routing** before archive cleanup of stale **`m8-edit`** task wording.

See [`docs/DELIVERABLE_TRACKING.md`](../../docs/DELIVERABLE_TRACKING.md) (GPIO base module dormant) and [`docs/PROJECT_INTENT.md`](../../docs/PROJECT_INTENT.md) (encoder + 4 buttons, surface-agnostic actions).

**Related changes:**

- **`pool-budget`** — global **`U:`** undo depth, pass reclaim, **`saveNoteEditPass`** heap admission.
  Session **`E:`** small snapshots (**EditChange** + **focus**, design D10): **`pool-budget`** task group 9
  (after this change).
- **`loop-ownership-hardening`** — slot/SD/capture correctness; parallel-safe with this change.

## What Changes

- **Session undo gestures**: push **`NoteEditSessionUndoStack`** only on transitions between geometry edit kinds (**add**, **delete**, **move**, **pitch**, **length**); not on select/nav, same-kind repeats, or length-mode toggle alone. **Add** and **move** each get their own **E** step.
- **Display**: in note edit show sidebar **`E:nn`**; after exit show global **`U:nn`**. **`E:00`** = no geometry edits to undo.
- **Undo/redo landing**: restore store; always reset to **select** edit type; bracket at last selected note; sync faders; **`lengthEditingMode = false`**; focus refresh on undo and redo.
- **Deselect empty** (fader 1 on empty step): nav only — **`selectedNoteIdx = -1`**, no session push; move/pitch/length faders require **`selectedNoteIdx >= 0`**.
- **`NoteEditSessionState`**: single owner of **edit kind** (select/add/delete/move/pitch/length) + **selection** (none/selected); fader and encoder paths update it through one API; **`syncNoteEditSessionStateToUi`** keeps FSM, **`selectedNoteIdx`**, and program change aligned, while **`cycleNoteEditType`** uses a deterministic cycle that resets away from fader-driven kind drift.
- **Encoder (GPIO)**: rename dead **`EditManager::cycleEditMode`** → **`cycleNoteEditType`** with order **`select_move_pitch_length`**; restore per-state accel via **`processEncoderMovement`**; wire **`GpioButtonManager`** in **`main.cpp`**.
- **DROID B2.31**: **`NoteEditManager::cycleEditMode`** (NOTE_EDIT ↔ LOOP_EDIT) unchanged; rename MIDI **`NOTE_UNDO`** → **`NOTE_EDIT_MODE`** for note 38.
- **Option B routing**: GPIO encoder press/turn and GPIO buttons call shared handlers in **`MidiButtonActions`** (or extracted **`EditControlActions`**) — not parallel edit logic in **`GpioButtonManager`**.
- **Session open trigger (locked)**: switching loop/edit mode into **`MAIN_MODE_NOTE_EDIT`** opens note edit session-state with select default + bracket-first/nearest auto-select.
- **GPIO rollout (locked)**: wire **`GpioButtonManager`** behind a compile flag in this PR for hardware bring-up.
- **GPIO pin map**: encoder **29/30/31**; four buttons **36/37/38/39** with the same action families as the four MIDI buttons: (1) record/play/delete, (2) track select/mute/delete, (3) loop/edit mode switch, (4) play/stop.
- **Pitch entry (locked for this PR)**: keep fader 4, encoder cycle-to-pitch, and hold-to-pitch together.
- **HITL**: extend edit baseline with **`E:`/`U:`** and undo/redo checkpoints through the run.

## Non-goals

- Loop geometry / cut-loop deselect undo (future).
- DROID encoder (none); faders update **`NoteEditSessionState`** kind directly (do not invoke **`cycleNoteEditType`**).
- **`m8-edit`** §3.3 **`editFlat_`** cleanup (parallel, not blocked).
- Full Drumboy Pro port (only action-layer seam).
- Global **`U:`** depth / pass reclaim — **`pool-budget`**.
- **`NoteEditSessionUndoStack`** **EditChange** + **focus** entries — **`pool-budget`** task group 9
  (replaces full **cloneShared** stacks; see **`pool-budget`** design D10).

## Capabilities

### New Capabilities

- **`note-edit-session-state`**: **`NoteEditSessionState`** single owner (edit kind + selection); transition API; **`syncNoteEditSessionStateToUi`**; deterministic encoder cycle independent from fader-kind drift.
- **`note-edit-session-undo`**: Geometry-kind boundary session undo, **E/U** display, undo/redo focus + fader sync, deselect semantics.
- **`note-edit-gpio-controls`**: **`GpioButtonManager`**, pin map 36–39 + encoder 29–31, Option B action routing, **`cycleNoteEditType`**, encoder accel, enter/exit note edit from panel.

### Modified Capabilities

- **`note-edit-modification-session`**: Session undo push policy (per-move → per-kind boundary); select vs geometry edit distinction.

## Impact

- **Code**: `EditManager` + **`NoteEditSessionState`**, `NoteEditManager`, `DisplayManager`, `MidiButtonActions`, `MidiButtonConfig`, `MidiConfig.h`, `ButtonManager` → **`GpioButtonManager`**, `main.cpp`, hold-to-pitch (kept in this PR).
- **Tests**: native session-undo matrix; HITL edit baseline verifier extensions.
- **Docs**: `docs/Guides/MIDI_CONFIG_GUIDE.md` (note 38 rename); GPIO pin table in `Globals.h`;
  cross-link **`pool-budget`** D9 for **`E:`** vs committed **editPass** storage.

## Open decisions (post-PR)

- Final winner among cycle / fader 4 / hold (kept together in this PR for hardware comparison).
