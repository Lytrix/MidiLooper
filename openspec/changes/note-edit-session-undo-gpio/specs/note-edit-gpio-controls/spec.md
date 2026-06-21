## ADDED Requirements

### Requirement: GpioButtonManager polls encoder and four buttons

The firmware SHALL rename **`ButtonManager`** to **`GpioButtonManager`**, call **`setup`** and **`update`** from **`main.cpp`**, and use Teensy pins: encoder **29/30/31**; buttons **36/37/38/39** per **`Globals.h`**.

#### Scenario: Main loop polls GPIO

- **WHEN** the main loop runs on hardware build
- **THEN** **`GpioButtonManager.update`** SHALL run each iteration

### Requirement: GPIO wiring is compile-flag gated

GPIO control-surface wiring SHALL be behind a compile flag for this PR so hardware bring-up can be validated before always-on activation.

#### Scenario: Compile flag disabled

- **WHEN** the compile flag is disabled
- **THEN** `main.cpp` SHALL skip `GpioButtonManager` setup/update wiring

### Requirement: Option B GPIO routes through MidiButtonActions

GPIO button presses and encoder push (short/long) SHALL invoke **`MidiButtonActions`** handlers, not duplicate edit/session logic inside **`GpioButtonManager`**.

#### Scenario: GPIO pin 38 session mode

- **WHEN** the user short-presses GPIO button on pin **38**
- **THEN** the firmware SHALL call the same action as DROID B2.31 **`handleCycleEditMode`**

#### Scenario: Encoder short press cycles note edit type

- **WHEN** the user short-presses the GPIO encoder button while in note edit
- **THEN** the firmware SHALL call **`handleCycleNoteEditType`** ( **`EditManager::cycleNoteEditType`** )

### Requirement: Hold-to-pitch stays enabled in this PR

Encoder hold-to-pitch behavior SHALL remain enabled in this PR alongside fader-4 and cycle-to-pitch entry.

#### Scenario: Encoder hold enters pitch

- **WHEN** the user holds encoder input for the hold threshold
- **THEN** pitch edit entry handler SHALL activate

### Requirement: GPIO button mapping mirrors MIDI button action families

GPIO pins **36/37/38/39** SHALL map to the same action families as MIDI buttons **36/37/38/39** and remain physical input mappings only.

#### Scenario: GPIO pin 36 action family

- **WHEN** GPIO pin **36** is triggered
- **THEN** actions SHALL route through the record / play / delete family handlers

#### Scenario: GPIO pin 37 action family

- **WHEN** GPIO pin **37** is triggered
- **THEN** actions SHALL route through the track select / mute / delete family handlers

#### Scenario: GPIO pin 38 action family

- **WHEN** GPIO pin **38** is triggered
- **THEN** actions SHALL route through the loop / edit mode switch family handlers

#### Scenario: GPIO pin 39 action family

- **WHEN** GPIO pin **39** is triggered
- **THEN** actions SHALL route through the play / stop family handlers

### Requirement: cycleNoteEditType order select_move_pitch_length

**EditManager::cycleNoteEditType** SHALL call **`applyCycleEditKind`** using deterministic encoder cycle order in **`select_move_pitch_length`**, independent from incidental fader-origin kind updates.

#### Scenario: Full encoder cycle from select

- **WHEN** **edit kind** is **select**
- **AND** the user presses the encoder button four times
- **THEN** **edit kind** SHALL visit **move**, **pitch**, **length**, **select**

#### Scenario: Cycle anchor after fader move

- **WHEN** **edit kind** is **move** because the user moved the note with faders
- **AND** the user presses the encoder button once
- **THEN** **edit kind** SHALL resolve to **move** as cycle anchor

### Requirement: Encoder turn uses per-kind acceleration

GPIO encoder rotation SHALL use **`NoteEditManager::processEncoderMovement`** with per-state acceleration tables and **`EditManager::onEncoderTurn`** with stepped deltas.

#### Scenario: Fast spin in move kind

- **WHEN** the user spins the encoder quickly in **move** kind
- **THEN** applied tick delta SHALL exceed single-step rotation (acceleration active)

### Requirement: Enter note edit at select state

GPIO encoder entry into note edit from outside edit SHALL open **`EditSelectNoteState`**, not **`EditNoteHomeState`**.

#### Scenario: First encoder press enters select

- **WHEN** the user short-presses encoder outside note edit to enter
- **THEN** **`NoteEditSessionState.kind`** SHALL be **select**
- **AND** encoder turn SHALL navigate bracket like fader 1 select

### Requirement: Encoder owns note length edit on GPIO surface

On the GPIO surface, note length edit SHALL be reached via encoder **`cycleNoteEditType`** into **length** kind and encoder turn. There SHALL be no dedicated GPIO length-mode button.

#### Scenario: Length edit via encoder cycle

- **WHEN** the user cycles edit kind to **length**
- **THEN** encoder turn SHALL apply length geometry mutation handlers

### Requirement: MIDI NOTE_EDIT_MODE rename

**MidiConfig::Transport::NOTE_UNDO** SHALL be renamed **`NOTE_EDIT_MODE`** for MIDI note **38** (DROID B2.31 **[NOTEEDIT]** session switch). The name SHALL NOT imply global undo.

#### Scenario: Config references updated

- **WHEN** **`MidiButtonConfig`** maps note **38**
- **THEN** it SHALL use **`NOTE_EDIT_MODE`** constant and describe session mode switch
