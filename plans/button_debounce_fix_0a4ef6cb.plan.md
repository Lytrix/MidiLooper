---
name: Button Debounce Fix
overview: "Fix two DROID button issues: (1) add per-button hardware debounce so rapid double-fire NoteOns are discarded, and (2) make handleButtonRelease() use per-button longPressTime so gate-mode buttons with hold durations above 600ms are not misclassified as long presses."
todos:
  - id: debounce-state
    content: Add lastReleaseTime to ButtonState in MidiButtonProcessor.h
    status: pending
  - id: debounce-config
    content: Add debounceMs field and withDebounce() builder to ButtonConfig in MidiButtonConfig.h
    status: pending
  - id: per-button-timing
    content: In handleButtonRelease() in MidiButtonProcessor.cpp, look up the button config and use its longPressTime instead of the global value
    status: pending
  - id: debounce-logic
    content: Set lastReleaseTime on NoteOff and check debounce on NoteOn in MidiButtonProcessor.cpp
    status: pending
  - id: transport-timing
    content: Add .withDebounce(150).withTiming(300, 400, 3000) to the transport button in MidiButtonConfig.cpp
    status: pending
isProject: false
---

# Button Debounce Root Cause Fix

## Problem

The DROID hardware fires two NoteOn/NoteOff pairs ~85-91ms apart for a single physical button press. The processor has no concept of debounce, so it enters the tap-counting logic twice, detects a double-press, and fires `NONE` (unimplemented action).

## Files to change

- `[include/MidiButtonProcessor.h](include/MidiButtonProcessor.h)` — add `lastReleaseTime` to `ButtonState`
- `[src/MidiButtonProcessor.cpp](src/MidiButtonProcessor.cpp)` — set `lastReleaseTime` on NoteOff; check debounce on NoteOn
- `[include/Utils/MidiButtonConfig.h](include/Utils/MidiButtonConfig.h)` — add `debounceMs` field and `.withDebounce()` builder to `ButtonConfig`
- `[src/Utils/MidiButtonConfig.cpp](src/Utils/MidiButtonConfig.cpp)` — add `.withDebounce(150)` to the transport button

## Changes

`**ButtonState` in `MidiButtonProcessor.h`:**

```cpp
uint32_t lastReleaseTime;  // add, init to 0
```

`**handleButtonRelease()` in `MidiButtonProcessor.cpp`:**

```cpp
// at the top of the function, before any other logic:
state.lastReleaseTime = now;
```

`**handleMidiNote()` in `MidiButtonProcessor.cpp**` — after the `!state.isPressed` check on NoteOn:

```cpp
const auto* cfg = MidiButtonConfig::Config::findButtonConfig(note, channel);
if (cfg && cfg->debounceMs > 0 && state.lastReleaseTime > 0 &&
    (now - state.lastReleaseTime) < cfg->debounceMs) {
    logger.log(CAT_BUTTON, LOG_DEBUG, "Debounce: ignoring Ch%d Note%d (%lums since last release)",
               channel, note, now - state.lastReleaseTime);
    return;  // still inside debounce window
}
```

`**ButtonConfig` in `MidiButtonConfig.h`:**

```cpp
uint32_t debounceMs;   // add field, default 0 (no debounce)
// in constructor:
debounceMs(0),
// builder:
ButtonConfig& withDebounce(uint32_t ms) { debounceMs = ms; return *this; }
```

**Transport button in `MidiButtonConfig.cpp`:**

```cpp
addButton(ButtonConfig(Notes::D2_SHARP, 16, "Global Transport")
          .onShortPress(ActionType::TOGGLE_TRANSPORT)
          .withDebounce(150));
```

