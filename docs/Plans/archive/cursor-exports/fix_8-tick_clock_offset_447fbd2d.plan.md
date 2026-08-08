---
name: Fix 8-tick clock offset
overview: Fix the consistent 8-tick recording offset caused by the MIDI clock advancing the tick before note events are processed, and by the first clock pulse after MIDI Start advancing past tick 0.
todos:
  - id: post-increment
    content: Move currentTick += TICKS_PER_CLOCK to after updateAllTracks in onMidiClockPulse()
    status: completed
  - id: tick-capture
    content: Move tickNow capture in handleMidiMessage to per-case (after Clock dispatch) so notes always use the latest tick
    status: completed
isProject: false
---

# Fix 8-Tick MIDI Clock Recording Offset

## Root Cause

The 8-tick offset is caused by the order of operations in `onMidiClockPulse()`:

```
currentTick += Config::TICKS_PER_CLOCK;   // tick becomes 8
trackManager.updateAllTracks(currentTick); // tracks see tick 8
```

Per the MIDI specification, the **first Clock pulse after MIDI Start IS the downbeat** (tick 0). But the code increments the tick **before** processing, so:

1. `onMidiStart()` sets `currentTick = 0`
2. First `onMidiClockPulse()` arrives (the downbeat) -- increments to 8 before any note at that pulse is processed
3. A NoteOn arriving with that same pulse reads `getCurrentTick()` which returns 8 instead of 0

This is a classic "pre-increment vs post-increment" timing error. The tick should represent the current position **at the time the pulse arrives**, not the next position.

Additionally, the `handleMidiMessage` function reads `tickNow = clockManager.getCurrentTick()` at the **top** of the function, but for Clock messages, it calls `onMidiClockPulse()` which advances the tick. Other messages (NoteOn etc.) that arrived in the same `usbMIDI.read()` batch will have already captured `tickNow` before the clock advanced -- but messages in the **next** batch will see the post-increment value. The timing depends on message ordering which is not deterministic.

## Fix (2 changes in [src/ClockManager.cpp](src/ClockManager.cpp))

### Change 1: Post-increment in `onMidiClockPulse()`

Move the tick advance to **after** `updateAllTracks`, so any note events processed at the same pulse time use the current tick, not the next one:

```cpp
// In onMidiClockPulse():
// BEFORE (current - pre-increment):
currentTick += Config::TICKS_PER_CLOCK;
trackManager.updateAllTracks(currentTick);

// AFTER (post-increment):
trackManager.updateAllTracks(currentTick);
currentTick += Config::TICKS_PER_CLOCK;
```

This means the first pulse after Start processes at tick 0 (correct), the second at tick 8, etc.

### Change 2: Read tick AFTER clock dispatch in `handleMidiMessage`

In [src/MidiHandler.cpp](src/MidiHandler.cpp), the `tickNow` is captured at line 101 **before** the Clock case advances the tick. For note events that arrive in the same polling cycle as a clock pulse, they use the stale tick. Move `tickNow` capture to after the Clock dispatch, or capture it per-case for channel voice messages:

Move `tickNow` capture from the top of `handleMidiMessage` to just before it's used (inside each channel-voice case), so it always reflects the latest tick after any clock pulses in the same batch have been processed.

### Why these 2 changes fix it

- **Change 1** ensures `updateAllTracks` (which processes pending record starts) sees tick 0 on the first pulse, not tick 8.
- **Change 2** ensures NoteOn/NoteOff events that arrive alongside or after a clock pulse in the same polling cycle use the correct post-pulse tick.
- No artificial offset compensation needed -- the tick counter simply reflects the correct musical position at each moment.

