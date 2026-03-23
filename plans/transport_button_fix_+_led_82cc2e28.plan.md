---
name: Transport Button Fix + LED
overview: Fix the global transport button's erratic behavior via per-button timing and debounce, and sync the DROID's L3.29 LED with the actual transport running state.
todos:
  - id: debounce-state
    content: Add lastReleaseTime to ButtonState in MidiButtonProcessor.h
    status: completed
  - id: debounce-config
    content: Add debounceMs field and withDebounce() builder to ButtonConfig in MidiButtonConfig.h
    status: completed
  - id: per-button-timing
    content: In handleButtonRelease(), set lastReleaseTime and use per-button effective timing values
    status: completed
  - id: debounce-logic
    content: Add debounce gate check at the top of the NoteOn path in handleMidiNote()
    status: completed
  - id: transport-timing
    content: Update transport button in MidiButtonConfig.cpp with .withDebounce(150).withTiming(300, 400, 3000)
    status: completed
  - id: transport-led-firmware
    content: In MidiButtonActions::handleToggleTransport(), send NoteOn/NoteOff on ch4 note39 after state change
    status: completed
  - id: transport-led-droid
    content: "Update droid ini: remove led=L3.29 from button, add midiin on ch4 + copy to L3.29"
    status: completed
isProject: false
---

# Transport Button Fix + LED Sync

## Problem recap

Two problems after the DROID config was changed to gate mode:

1. **Double-fire**: DROID sends two rapid NoteOn/NoteOff pairs (85-91ms apart) → detected as double press → no action
2. **Long gate**: DROID holds NoteOn for 720-4212ms → global `longPressTime=600ms` misclassifies it as a long press → no action

The existing plan uses per-button timing + debounce to fix both. The LED (L3.29) currently mirrors only the button gate (is it held?), not the transport state.

## Fix 1: Debounce — `[include/MidiButtonProcessor.h](include/MidiButtonProcessor.h)`

Add `lastReleaseTime` to `ButtonState`:

```cpp
struct ButtonState {
    // ... existing fields ...
    uint32_t lastReleaseTime = 0;   // add
};
```

## Fix 2: Per-button debounceMs — `[include/Utils/MidiButtonConfig.h](include/Utils/MidiButtonConfig.h)`

Add field and builder to `ButtonConfig`:

```cpp
uint32_t debounceMs = 0;  // add field (default 0 = disabled)

ButtonConfig& withDebounce(uint32_t ms) { debounceMs = ms; return *this; }
```

## Fix 3: Apply fixes in processor — `[src/MidiButtonProcessor.cpp](src/MidiButtonProcessor.cpp)`

**In `handleMidiNote()` on NoteOn**, before recording the press, check debounce:

```cpp
const auto* cfg = MidiButtonConfig::Config::findButtonConfig(note, channel);
if (cfg && cfg->debounceMs > 0 && state.lastReleaseTime > 0 &&
    (now - state.lastReleaseTime) < cfg->debounceMs) {
    logger.log(CAT_BUTTON, LOG_DEBUG, "Debounce: ignoring Ch%d Note%d (%lums since release)",
               channel, note, now - state.lastReleaseTime);
    return;
}
```

**In `handleButtonRelease()`**, set `lastReleaseTime` and use per-button timing:

```cpp
state.lastReleaseTime = now;   // set at top of function

const auto* cfg = MidiButtonConfig::Config::findButtonConfig(note, channel);
uint32_t effectiveLongPress  = (cfg && cfg->longPressTime > 0)    ? cfg->longPressTime  : longPressTime;
uint32_t effectiveDoubleTap  = (cfg && cfg->doubleTapWindow > 0)  ? cfg->doubleTapWindow : doubleTapWindow;
uint32_t effectiveTripleTap  = (cfg && cfg->tripleTapWindow > 0)  ? cfg->tripleTapWindow : tripleTapWindow;
// replace all uses of longPressTime/doubleTapWindow/tripleTapWindow with effective* variants
```

## Fix 4: Transport button config — `[src/Utils/MidiButtonConfig.cpp](src/Utils/MidiButtonConfig.cpp)`

```cpp
addButton(ButtonConfig(Notes::D2_SHARP, 16, "Global Transport")
          .onShortPress(ActionType::TOGGLE_TRANSPORT)
          .withDebounce(150)
          .withTiming(300, 400, 3000));  // longPressTime 3000ms — far above any real gate
```

## Fix 5: Transport LED feedback — `[src/MidiButtonActions.cpp](src/MidiButtonActions.cpp)`

After calling `clockManager.toggleTransport()`, send MIDI back on channel 4 (already defined as `MidiButtonConfig::Channels::TRANSPORT`):

```cpp
void MidiButtonActions::handleToggleTransport() {
    clockManager.toggleTransport();
    if (clockManager.isTransportRunning()) {
        midiHandler.sendNoteOn(MidiButtonConfig::Channels::TRANSPORT, Notes::D2_SHARP, 127);
    } else {
        midiHandler.sendNoteOff(MidiButtonConfig::Channels::TRANSPORT, Notes::D2_SHARP, 0);
    }
}
```

- Channel 4 is the `TRANSPORT` channel constant already in the codebase
- At boot, transport is stopped and no NoteOn is sent, so L3.29 starts dark (correct)

## Fix 6: DROID config — `[droid/midilooper_v1.ini](droid/midilooper_v1.ini)`

Remove direct LED wiring from the button and add MIDI-driven LED:

```ini
# Before:
[button]
    button = B3.29
    led = L3.29
    output = _TRIGGER_START_STOP

# After:
[button]
    button = B3.29
    output = _TRIGGER_START_STOP

[midiin]
    notegate1 = _TRANSPORT_LED
    note1 = 39
    channel = 4
    usb = 1

[copy]
    input = _TRANSPORT_LED
    output = L3.29
```

The LED now stays on for as long as transport is running, not just while the button is held.