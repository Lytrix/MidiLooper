---
name: Exclude LED channels from All Notes Off
overview: Exclude MIDI channels 2, 3, and 4 from All Notes Off (CC 123) on USB output only, so transport and button LED feedback on the DROID controller are not turned off when stopping playback.
todos: []
isProject: false
---

# Exclude LED Channels from All Notes Off on USB

## Problem

When `sendAllNotesOff()` is called (on transport stop, track mute, etc.), it sends CC 123 (All Notes Off) on all 16 MIDI channels. On USB output, channels 2–4 are used for DROID LED control:

- **Channel 2**: Track/button state LEDs (ClipView `_DUMMY1`–`_DUMMY16`)
- **Channel 3**: 16th-note position LEDs (`_CURRENT_TICK_16TH_1`–`16`)
- **Channel 4**: Transport LED (`_START_STOP_STATE` → L3.29)

CC 123 on these channels clears the LED states and turns off highlighted notes on the DROID buttons.

## Solution

When sending CC 123 to the **USB output** (`usbMIDI`), skip channels 2, 3, and 4. Continue sending CC 123 on all channels to **Serial** (5-pin DIN) so external instruments still receive All Notes Off.

## Implementation

### 1. Add constants and helper (optional but recommended)

In [include/MidiHandler.h](include/MidiHandler.h) or a shared config:

- `CC_ALL_NOTES_OFF = 123`
- LED channels 2–4 as a documented range (or inline checks)

### 2. Modify `sendMidiEvent` in [src/MidiHandler.cpp](src/MidiHandler.cpp)

In the `midi::ControlChange` branch (around lines 313–316), add a guard before sending to USB:

```cpp
case midi::ControlChange: {
    // Skip CC 123 on USB channels 2-4 (DROID LED feedback)
    bool skipUsb = (event.data.ccData.cc == 123 &&
                    event.channel >= 2 && event.channel <= 4);
    if (outputUSB && !skipUsb)
        usbMIDI.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
    if (outputSerial)
        MIDIserial.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
    break;
}
```

Serial output stays unchanged: CC 123 is sent on all channels to DIN MIDI.

## Call sites

`Track::sendAllNotesOff()` in [src/Track.cpp](src/Track.cpp) calls `midiHandler.sendControlChange(ch, 123, 0)` for each channel 1–16. All of this goes through `sendMidiEvent`, so no changes are needed there.

## Notes

- No change to `usbHostMIDI` routing, since `sendMidiEvent` does not send to `usbHostMIDI` (only `sendMidiThru` does for thru).
- If LED channels ever change, the `2` and `4` bounds can be replaced with named constants in `MidiHandler` or `Globals.h`.

