## 1. Naming and config

- [x] 1.1 Rename `MidiConfig::Transport::NOTE_UNDO` → `NOTE_EDIT_MODE`; update `MidiButtonConfig`, `MidiMapping`, logs (remove "MIDI Encoder" for B2.31)
- [x] 1.2 Document GPIO pin map in `Globals.h` (encoder 29–31; buttons 36–39 with roles)

## 2. NoteEditSessionState (single owner)

- [x] 2.1 Add `NoteEditKind`, `NoteEditSelection`, `NoteEditSessionState` on `EditManager` (header + native-testable transitions), with `Select/Add/Delete/Move/Pitch/Length`
- [x] 2.2 Implement transition helpers: `applySelectNav`, `applyCycleEditKind`, `applyGeometryKindFromControl`, `applyUndoRedoLanding`, `resetNoteEditSessionState`
- [x] 2.3 Implement `syncNoteEditSessionStateToUi` — mirror `selectedNoteIdx`, `setState`, `sendEditModeProgram`, fader schedule
- [x] 2.4 Refactor fader 1 / coarse / fine / note-value / length paths to use transition helpers (no direct kind/idx drift)
- [x] 2.5 Open note edit overlay with `kind=Select` + `syncNoteEditSessionStateToUi` when session starts; set bracket from current tick and auto-select bracket-first/nearest note
- [x] 2.6 Enter note edit session-state when loop/edit switch changes to `MAIN_MODE_NOTE_EDIT`

## 3. Session undo core

- [x] 3.1 `pushSessionUndoOnKindChange` on interaction kind transitions + add/delete; remove per-move and onEnter pushes
- [x] 3.2 `sessionUndo` / `sessionRedo` → `applyUndoRedoLanding` + `lengthEditingMode` reset; always land in `Select`
- [x] 3.3 Fix `getDisplayUndoCount` + `DisplayManager` sidebar **E:** vs **U:**

## 4. cycleNoteEditType and encoder

- [x] 4.1 Rename `EditManager::cycleEditMode` → `cycleNoteEditType`; delegate to `applyCycleEditKind` (deterministic encoder cycle, reset away from fader-kind drift)
- [x] 4.2 Add `MidiButtonActions::handleCycleNoteEditType`; enter note edit via `applySelectNav` + Select kind
- [x] 4.3 Remove `switchToNextState` / `EditNoteHomeState` from encoder path
- [x] 4.4 Restore `processEncoderMovement`; key accel off `NoteEditSessionState.kind`

## 5. GpioButtonManager (Option B)

- [x] 5.1 Rename `ButtonManager` → `GpioButtonManager` (files, includes, extern)
- [x] 5.2 Extend `ButtonId` for pins 36–39; `setup({36,37,38,39,31})` in `main.cpp`
- [x] 5.3 Route GPIO buttons 36–39 and encoder press/long to `MidiButtonActions` with explicit action families: 36 record/play/delete, 37 track select/mute/delete, 38 loop/edit mode switch, 39 play/stop
- [x] 5.4 Keep hold-to-pitch enabled in this PR; ensure it routes to pitch edit entry handler
- [x] 5.5 Ensure GPIO surface note length edit is encoder-owned (`cycleNoteEditType` → `Length` + encoder turn), no dedicated GPIO length-mode button
- [x] 5.6 Gate GPIO setup/update wiring in `main.cpp` behind compile flag for hardware bring-up

## 6. Tests

- [x] 6.1 Native: `NoteEditSessionState` transitions (fader move → kind Move; first cycle press after fader drift → Move anchor; next cycle → Pitch; deselect → Select + none)
- [x] 6.2 Native: geometry-kind undo matrix (multi-move one E; add+move two E; deselect no push)
- [x] 6.3 Native: session undo/redo routing helpers (`test_edit_session_state_serial_verify.py`)
- [x] 6.4 Extend HITL edit baseline: **E/U** + interaction kind checkpoints (`_verify_session_state_enter`, `_verify_session_undo_redo_routing`)
- [x] 6.5 `pio test -e native`; user-confirmed HITL on hardware (`20260622_023118`, `20260622_023340`)

## 7. Closeout

- [x] 7.1 `openspec validate note-edit-session-undo-gpio`
- [x] 7.2 Update `m8-edit` tasks §4.1 wording to reference kind-boundary + session-state owner
