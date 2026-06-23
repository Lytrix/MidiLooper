## Context

**Today — three separate concepts (often conflated):**

| Concept | Storage | Fader path today | Encoder path today |
|---------|---------|------------------|-------------------|
| **Main session mode** | `NoteEditManager::currentMainEditMode` NOTE_EDIT vs LOOP_EDIT | B2.31 **`cycleEditMode`** | N/A |
| **Edit FSM state** | `EditManager::currentState` (select/start/pitch/length/home or **null**) | **Not updated** by fader 1 | Set only after **`enterEditMode`** |
| **Note target** | `EditManager::selectedNoteIdx` (-1 = empty) | Fader 1 sets/clears | Encoder nav in select state |

**Fader 1 on a note** (`handleSelectFaderInput`): requires **`MAIN_MODE_NOTE_EDIT`** only — does **not** enter **`EditSelectNoteState`**. It sets **`selectedNoteIdx`**, **`bracketTick`**, rebuilds focus; on note-to-note reselect calls **`commitAllPendingNoteEditActions`**. Sets **`startEditingEnabled = false`** and starts grace before coarse/fine/pitch faders run.

**Fader 1 empty step**: **`selectedNoteIdx = -1`**, **`clearLastFader1SelectRef`**, **`rebuildNoteEditFocusAtSelect(track, -1)`** — no commit, no session undo push.

**Move faders** (coarse/fine): require **`MAIN_MODE_NOTE_EDIT`**, **`startEditingEnabled`**, and **`selectedNoteIdx >= 0`** — **do not** check FSM **`currentState`**. Geometry move works with **`currentState == nullptr`** (typical DROID-only path).

**Session undo today**: **`pushSessionUndoBeforeMutation`** on every fader move and onEnter in start/length/pitch states — too dense. Each push clones the full materialized session store via **`cloneShared()`**. **`pool-budget`** task group 9 replaces clones with **EditChange + focus** entries (design D10). Display shows global **`U:`** during edit.

**GPIO**: **`ButtonManager`** exists; **`main.cpp`** never calls **`setup`/`update`**. Pins in **`Globals.h`**: encoder 29–31; buttons 36–37 only named (RECORD/PLAY); no pin doc for full 4-button layout.

## Goals / Non-Goals

**Goals:**

- One **geometry edit kind** model shared by fader and encoder paths — **`NoteEditSessionState`** as single owner.
- Option B: GPIO → **`MidiButtonActions`** (shared with MIDI/DROID).
- Revive GPIO module as **`GpioButtonManager`** with pins **36–39** + encoder **29–31**.
- Session undo + **E/U** + HITL validation.

**Non-Goals:**

- Coupling **`cycleNoteEditType`** internals to fader grace/feedback (session-state owns kind only).
- Loop cut on deselect empty.
- Session snapshot format — **`pool-budget`** D10 (**EditChange** + **focus**; ships in task group 9 after this change).

## Decisions

### D1 — `NoteEditSessionState` (single owner)

**Problem today:** `selectedNoteIdx`, `currentState`, and fader gates drift apart — fader move works with **`currentState == nullptr`** while encoder FSM may say something else.

**Owner struct** on **`EditManager`** (native-testable; nested under `NoteEditSession` in `include/NoteEditSession.h` or dedicated `include/NoteEditSessionState.h`):

```cpp
enum class NoteEditKind { Select, Add, Delete, Move, Pitch, Length };

struct NoteEditSelection {
  bool hasNote = false;
  NoteRef ref{};
  int displayIdx = -1;
  uint32_t bracketTick = 0;
};

struct NoteEditSessionState {
  NoteEditKind kind = NoteEditKind::Select;
  NoteEditSelection selection{};
};
```

**Not owned here:** `MAIN_MODE_NOTE_EDIT`, fader grace, overlap focus, undo push logic (observers on transitions).

**Transition API** (all paths call these, then **`syncNoteEditSessionStateToUi(track)`**):

| Helper | Caller | Effect |
|--------|--------|--------|
| **`applySelectNav`** | Fader 1, encoder in **Select** | Update **selection** + bracket; **kind** stays **Select** |
| **`applyCycleEditKind`** | Encoder short → **`cycleNoteEditType`** | Advance encoder cycle order deterministically and apply that kind; do not follow incidental fader-kind updates |
| **`applyGeometryKindFromControl`** | Fader coarse/fine/4/length + add/delete entry | Set **kind** to Add/Delete/Move/Pitch/Length before mutation |
| **`applyUndoRedoLanding`** | **sessionUndo** / **sessionRedo** | **kind = Select**; restore **selection** from restored store |
| **`resetNoteEditSessionState`** | **exitEditMode** | Clear to defaults |

**`syncNoteEditSessionStateToUi`:**

- Mirror **`selection.displayIdx`** → **`selectedNoteIdx`**
- **`setState`** matching **kind** (`EditSelectNoteState`, `EditStartNoteState`, …)
- **`sendEditModeProgram`**
- Schedule fader feedback when selection or kind changes

**Cycle reset behavior (key behavior):**

- User moves with faders → **`applyGeometryKindFromControl(Move)`** → session-state **kind = Move**
- Next **`cycleNoteEditType`** uses encoder cycle order, not fader drift; first cycle step after fader-origin kind updates resolves to **Move**
- Encoder turn uses **`sessionState.kind`** for accel + **`onEncoderTurn`** handler

**Deselect empty:** **`applySelectNav`** with no note → **selection.none**, **kind** stays **Select**, no undo push.

**Default on enter note edit:** **kind = Select**; set session-state bracket from **current tick**; then auto-select note by bracket-first/nearest rule (if a note exists at bracket tick use it, else select nearest note to current tick, else selection.none).

Session-open trigger (locked): switching **`MAIN_MODE_LOOP_EDIT -> MAIN_MODE_NOTE_EDIT`** via loop/edit mode switch SHALL open note edit session-state and run the same default-enter initialization above.

### D2 — Session undo push (`pushSessionUndoOnKindChange`)

Track **`lastCommittedEditKind`** (or last pushed kind). Before first mutation in kind **K**, if **K ≠ lastCommittedEditKind** and **K** is a geometry kind → **`pushSessionUndoBeforeMutation`**, then set last to **K**.

Geometry kinds: **add**, **delete**, **move**, **pitch**, **length**.

- Same-kind repeats: no push.
- **Add** then **move**: two pushes (separate kinds).
- Length **toggle** alone: no push; first **length** geometry after **move** phase: push.
- **Select/nav**: never a kind; never pushes.

Fader and encoder each reach kind changes via **`NoteEditSessionState`** transitions; **one undo stack**.

### D3 — Undo/redo landing

After **`sessionUndo`** / **`sessionRedo`**:

1. Restore store snapshot.
2. **`applyUndoRedoLanding`** → **kind = Select**; restore **selection**
3. **`lengthEditingMode = false`**
4. **`syncNoteEditSessionStateToUi`** (focus rebuild + fader sync)

On **`exitEditMode`**: **`resetNoteEditSessionState`**; clear session undo stack; global **`U:`** from pass close.

### D4 — Display

- **`noteEditSession.active` && in note edit overlay**: sidebar **`E:nn`** = **`undoStack.undoCount()`** (including **select** nav phase).
- Not in note edit: **`U:nn`** = **`TrackUndo`** pass count.
- **`E:00`**: no geometry undo steps.

### D5 — Option B action routing

```
GpioButtonManager.update()
  ├─ encoder turn → NoteEditManager::processEncoderMovement (accel) → EditManager::onEncoderTurn
  ├─ encoder short → MidiButtonActions::handleCycleNoteEditType()
  ├─ encoder long  → MidiButtonActions::handleExitEditMode()
  ├─ encoder hold  → keep temporary pitch in this PR
  └─ GPIO buttons 36–39 → MidiButtonActions (same action families as MIDI 36–39)
```

DROID B2.31 remains **`handleCycleEditMode`** → **`NoteEditManager::cycleEditMode`**.

Rename **`MidiConfig::Transport::NOTE_UNDO`** → **`NOTE_EDIT_MODE`** (MIDI note **38**). **`NOTE_REDO`** stays **39** (global transport on DROID).

### D6 — `cycleNoteEditType`

Rename **`EditManager::cycleEditMode`** → **`cycleNoteEditType`**.

Implementation: **`applyCycleEditKind(track)`** — advance encoder cycle order in **`select_move_pitch_length`** independent from incidental fader-origin kind drift, then **`syncNoteEditSessionStateToUi`**.

Reset rule: when the last kind update source is fader geometry, the next encoder cycle press starts from **Move** (cycle anchor), then continues **Move → Pitch → Length → Select** on subsequent encoder presses.

Kind-change undo push per D2 — not on cycle alone until first geometry mutation in new kind or leaving prior kind.

Remove **`switchToNextState`** home ↔ start toggle.

### D7 — GPIO pin map (`Globals.h`)

| Teensy pin | Role | Action routing |
|------------|------|----------------|
| 29 | Encoder A | — |
| 30 | Encoder B | — |
| 31 | Encoder button | short/long/hold |
| 36 | Button A | Record / play / delete action family |
| 37 | Button B | Track select / mute / delete action family |
| 38 | Button C | Loop / edit mode switch action family (`NOTE_EDIT_MODE`) |
| 39 | Button D | Play / stop action family |

**Note:** MIDI note numbers 36–39 and Teensy GPIO pins 36–39 are numerically aligned by convention; they are distinct namespaces (MIDI vs **`pinMode`**).

**`GpioButtonManager`**: rename from **`ButtonManager`**; **`ButtonId`** adds **BUTTON_C**, **BUTTON_D**; **`setup({36,37,38,39,31})`**. The 4 GPIO buttons are physical mappings only; shared action handlers own behavior.

### D8 — Encoder acceleration

Restore commented **`NoteEditManager::processEncoderMovement`** tables:

| FSM / kind | Fast spin multiplier |
|------------|---------------------|
| move | ×24 / ×8 / ×4 |
| length | ×8 / ×4 / ×2 |
| pitch | ×4 / ×3 / ×2 |
| select | ×4 / ×3 / ×2 |

Accel table keyed off **`NoteEditSessionState.kind`**, not **`currentState`** pointer alone.

Keep **`EditManager::onEncoderTurn`** step loop.

### D9 — Pitch entry (three paths, HITL)

1. Fader 4 (existing)
2. **`cycleNoteEditType`** → pitch state
3. Encoder hold 250 ms → **`enterPitchEditMode`** (legacy)

All three are kept in this PR. All use **pitch** kind for undo boundaries.

### D10 — Encoder length edit ownership

Encoder workflow SHALL own note length edit via **`cycleNoteEditType`** (**Length** kind + encoder turn). There is no dedicated GPIO length-mode button; that dedicated toggle remains only on the fader MIDI surface.

## Risks / Trade-offs

- **[Cycle reset surprise]** → After intentional fader Pitch/Length work, next encoder cycle may jump back to Move anchor; HITL validates ergonomics.
- **[Undo always lands in select]** → May require extra encoder presses; HITL gate.
- **[Pin 38 local vs B2.31 MIDI both call cycleEditMode]** → Intentional parity for base module.
- **[Hold-to-pitch vs cycle]** → Keep both until HITL prunes.

## Migration Plan

1. Native tests + **`pio test -e native`**
2. HITL edit baseline extension
3. Wire **`GpioButtonManager`** behind compile flag (hardware gating until bench validation)
4. User-confirmed Teensy upload

## Open Questions

- Retire hold-to-pitch after workflow test (post-PR decision).
