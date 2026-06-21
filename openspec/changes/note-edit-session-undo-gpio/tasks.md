## 1. Naming and config

- [ ] 1.1 Rename `MidiConfig::Transport::NOTE_UNDO` → `NOTE_EDIT_MODE`; update `MidiButtonConfig`, `MidiMapping`, logs (remove "MIDI Encoder" for B2.31)
- [ ] 1.2 Document GPIO pin map in `Globals.h` (encoder 29–31; buttons 36–39 with roles)

## 2. NoteEditSessionState (single owner)

- [ ] 2.1 Add `NoteEditKind`, `NoteEditSelection`, `NoteEditSessionState` on `EditManager` (header + native-testable transitions), with `Select/Add/Delete/Move/Pitch/Length`
- [ ] 2.2 Implement transition helpers: `applySelectNav`, `applyCycleEditKind`, `applyGeometryKindFromControl`, `applyUndoRedoLanding`, `resetNoteEditSessionState`
- [ ] 2.3 Implement `syncNoteEditSessionStateToUi` — mirror `selectedNoteIdx`, `setState`, `sendEditModeProgram`, fader schedule
- [ ] 2.4 Refactor fader 1 / coarse / fine / note-value / length paths to use transition helpers (no direct kind/idx drift)
- [ ] 2.5 Open note edit overlay with `kind=Select` + `syncNoteEditSessionStateToUi` when session starts; set bracket from current tick and auto-select bracket-first/nearest note
- [ ] 2.6 Enter note edit session-state when loop/edit switch changes to `MAIN_MODE_NOTE_EDIT`

## 3. Session undo core

- [ ] 3.1 `pushSessionUndoOnKindChange` on interaction kind transitions + add/delete; remove per-move and onEnter pushes
- [ ] 3.2 `sessionUndo` / `sessionRedo` → `applyUndoRedoLanding` + `lengthEditingMode` reset; always land in `Select`
- [ ] 3.3 Fix `getDisplayUndoCount` + `DisplayManager` sidebar **E:** vs **U:**

## 4. cycleNoteEditType and encoder

- [ ] 4.1 Rename `EditManager::cycleEditMode` → `cycleNoteEditType`; delegate to `applyCycleEditKind` (deterministic encoder cycle, reset away from fader-kind drift)
- [ ] 4.2 Add `MidiButtonActions::handleCycleNoteEditType`; enter note edit via `applySelectNav` + Select kind
- [ ] 4.3 Remove `switchToNextState` / `EditNoteHomeState` from encoder path
- [ ] 4.4 Restore `processEncoderMovement`; key accel off `NoteEditSessionState.kind`

## 5. GpioButtonManager (Option B)

- [ ] 5.1 Rename `ButtonManager` → `GpioButtonManager` (files, includes, extern)
- [ ] 5.2 Extend `ButtonId` for pins 36–39; `setup({36,37,38,39,31})` in `main.cpp`
- [ ] 5.3 Route GPIO buttons 36–39 and encoder press/long to `MidiButtonActions` with explicit action families: 36 record/play/delete, 37 track select/mute/delete, 38 loop/edit mode switch, 39 play/stop
- [ ] 5.4 Keep hold-to-pitch enabled in this PR; ensure it routes to pitch edit entry handler
- [ ] 5.5 Ensure GPIO surface note length edit is encoder-owned (`cycleNoteEditType` → `Length` + encoder turn), no dedicated GPIO length-mode button
- [ ] 5.6 Gate GPIO setup/update wiring in `main.cpp` behind compile flag for hardware bring-up

## 6. Tests

- [ ] 6.1 Native: `NoteEditSessionState` transitions (fader move → kind Move; first cycle press after fader drift → Move anchor; next cycle → Pitch; deselect → Select + none)
- [ ] 6.2 Native: geometry-kind undo matrix (multi-move one E; add+move two E; deselect no push)
- [ ] 6.3 Native: session undo/redo + session-state landing
- [ ] 6.4 Extend HITL edit baseline: **E/U** + interaction kind checkpoints
- [ ] 6.5 `pio test -e native`; user-confirmed HITL on hardware

## 7. Closeout

- [ ] 7.1 `openspec validate note-edit-session-undo-gpio`
- [ ] 7.2 Update `m8-edit` tasks §4.1 wording to reference kind-boundary + session-state owner
