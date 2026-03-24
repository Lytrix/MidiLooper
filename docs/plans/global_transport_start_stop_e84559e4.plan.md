---
name: Global Transport Start Stop
overview: Add a global transport start/stop controlled by MIDI note 39 on channel 16. Clock and playback stay stopped at boot until the user presses the start button.
todos:
  - id: clock-init-stop
    content: Set sequencerRunning = false, add toggleTransport() and isTransportRunning() to ClockManager
    status: completed
  - id: is-clock-running
    content: Change isClockRunning() to return only sequencerRunning
    status: completed
  - id: stop-tracks-on-stop
    content: "In toggleTransport when stopping: stop all playing tracks, send all-notes-off, save state"
    status: completed
  - id: action-type-handler
    content: Add TOGGLE_TRANSPORT ActionType, handleToggleTransport(), and wire in executeAction
    status: completed
  - id: button-config
    content: Add note 39 ch 16 button with TOGGLE_TRANSPORT to loadConfiguration
    status: completed
isProject: false
---

# Global Transport Start/Stop

## Summary

- **Button:** MIDI note 39 on channel 16
- **Initial state:** Stopped (`sequencerRunning = false`)
- **Behavior:** Short press toggles transport run/stop

## Changes

### 1. ClockManager — initial state and toggle API

**File:** [src/ClockManager.cpp](src/ClockManager.cpp)

- Set `sequencerRunning = false` at line 16 (replace `true` with `false`)
- Add `void ClockManager::toggleTransport()` that flips `sequencerRunning` and (if needed) stops all tracks when transitioning to stopped
- Add `bool ClockManager::isTransportRunning() const` returning `sequencerRunning`

**File:** [include/ClockManager.h](include/ClockManager.h)

- Declare `void toggleTransport()`
- Declare `bool isTransportRunning() const`

### 2. isClockRunning behavior

**File:** [src/ClockManager.cpp](src/ClockManager.cpp)

`isClockRunning()` currently returns `sequencerRunning || (clockSource == CLOCK_EXTERNAL)`, so external clock can make it "running" even when stopped. Change to:

```cpp
return sequencerRunning;
```

so the transport button fully controls whether the clock is considered active.

### 3. Stop all tracks when transport stops

**File:** [src/ClockManager.cpp](src/ClockManager.cpp)

In `toggleTransport()`, when transitioning to stopped:

- Set `sequencerRunning = false`
- Stop all tracks (call `trackManager.stopPlayingTrack(i)` for each PLAYING/OVERDUBBING track)
- Send all-notes-off on all tracks
- Call `StorageManager::saveState()` to persist

### 4. ActionType and handler

**File:** [include/Utils/MidiButtonConfig.h](include/Utils/MidiButtonConfig.h)

- Add `TOGGLE_TRANSPORT` to the `ActionType` enum (place near other transport actions)

**File:** [include/MidiButtonActions.h](include/MidiButtonActions.h)

- Add `void handleToggleTransport()` declaration

**File:** [src/MidiButtonActions.cpp](src/MidiButtonActions.cpp)

- Implement `handleToggleTransport()` calling `clockManager.toggleTransport()`
- In `executeAction()`, handle `ActionType::TOGGLE_TRANSPORT` and call `handleToggleTransport()`

### 5. Button configuration

**File:** [src/Utils/MidiButtonConfig.cpp](src/Utils/MidiButtonConfig.cpp)

In `loadConfiguration()`, add after the NOTELEN button (around line 212):

```cpp
addButton(ButtonConfig(Notes::D2_SHARP, 16, "Global Transport")
          .onShortPress(ActionType::TOGGLE_TRANSPORT));
```

`Notes::D2_SHARP` is 39. Use channel 16 as specified.

### 6. DROID mapping (optional documentation)

**File:** [droid/midilooper_v1.ini](droid/midilooper_v1.ini)

Add a mapping for note 39 on channel 16 to a physical button (e.g. B2.32 or another grid button) if not already present. The ini already maps transport buttons; add a DROID output line for the transport button or document which B32 button to use.

## Dependencies

- `ClockManager` must be able to call `trackManager` and `StorageManager` (via includes)
- `MidiButtonActions` must include `ClockManager.h`

