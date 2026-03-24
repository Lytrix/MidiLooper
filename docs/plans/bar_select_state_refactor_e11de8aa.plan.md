---
name: Bar Select State Refactor
overview: Introduce a "bar select" state in BarStepButtonHandler that temporarily zooms into a single bar while preserving the original loop for LEDs and restoration. Add immediate seek on NoteOn, fader blocking during bar select, and correct enter/exit semantics.
todos:
  - id: immediate-seek
    content: Add immediate seek in handleNoteOn for bar/16th buttons, quantized to 16th step boundaries
    status: completed
  - id: bar-select-state
    content: Add barSelectActive, selectedBarIndex, savedLoopStartTick, savedLoopLength to BarStepButtonHandler.h with public getters
    status: completed
  - id: enter-exit-logic
    content: Implement bar select enter (HOLD_ONE), exit (NoteOn same bar + HOLD_ONE same bar), and switch (HOLD_ONE different bar) in BarStepButtonHandler.cpp
    status: completed
  - id: block-faders
    content: Block fader input in LoopEditManager when bar select is active
    status: completed
  - id: led-feedback
    content: Use saved loop values for LED rendering in MidiLedManager during bar select
    status: deferred
isProject: false
---

# Bar Select State Refactor

## Current Behavior

- SHORT_PRESS: seeks to bar (delayed by 300ms double-tap window via `processPendingPresses`)
- HOLD_ONE: permanently sets loop start + length to 1 bar (no way to restore)
- Faders always active
- LEDs always reflect current (modified) loop

## New Behavior

### 1. Immediate seek on NoteOn (quantized to 16th)

In `handleNoteOn` ([BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp) line 177), immediately call `clockManager.setCurrentTick()` for both bar and 16th-type buttons. This gives zero-latency feedback instead of waiting for the 300ms pending-press timer.

The seek target is quantized to the nearest 16th step boundary using `Config::TICKS_PER_16TH_STEP` (48 ticks):

```cpp
uint32_t seekTick = startLoopTick + tickInLoopStorage;
seekTick = (seekTick / Config::TICKS_PER_16TH_STEP) * Config::TICKS_PER_16TH_STEP;
clockManager.setCurrentTick(seekTick);
```

Bar buttons naturally align (768 is a multiple of 48), but this ensures correctness regardless of `startLoopTick` / `loopStartTick` alignment.

### 2. Bar Select State (enter/exit)

Add state to [BarStepButtonHandler.h](include/BarStepButtonHandler.h):

```cpp
bool barSelectActive;
uint8_t selectedBarIndex;
uint32_t savedLoopStartTick;
uint32_t savedLoopLength;
```

**Enter bar select** -- on HOLD_ONE (NoteOff, long press) when `!barSelectActive`:

- Save `track.getLoopStartTick()` and `track.getLoopLength()` into `savedLoopStartTick` / `savedLoopLength`
- Calculate the selected bar's tick range from `info.stepIndex` relative to the *saved* loop
- Set `track.setLoopStartTick()` and `track.setLoopLength()` to zoom into that bar
- Set `barSelectActive = true`, `selectedBarIndex = info.stepIndex`

**Switch bar** -- on HOLD_ONE when `barSelectActive && info.stepIndex != selectedBarIndex`:

- Keep `savedLoopStartTick` / `savedLoopLength` unchanged (still references the original loop)
- Recalculate zoom to the new bar using `savedLoopStartTick` / `savedLoopLength`
- Update `selectedBarIndex`

**Exit bar select** -- two triggers:

- **NoteOn of same bar** (in `handleNoteOn`): if `barSelectActive && info.stepIndex == selectedBarIndex`, immediately restore `savedLoopStartTick` / `savedLoopLength`, set `barSelectActive = false`
- **HOLD_ONE of same bar** (NoteOff, long press): if `barSelectActive && info.stepIndex == selectedBarIndex`, restore and set `barSelectActive = false`

On exit, call `trackManager.forceLedUpdate()` to refresh.

### 3. Bar index calculation fix

The HOLD_ONE code at line 384 calculates `regionStartStorage = (loopStartTick + regionStartDisplay) % loopLength`. During bar select, this must use `savedLoopStartTick` / `savedLoopLength` (the original loop) so bar indices remain stable.

### 4. LED feedback uses original loop (DEFERRED)

Skipped for now -- test first without this to see if the default LED behavior is acceptable. Can be added later if needed.

### 5. Block fader changes during bar select

In [LoopEditManager.cpp](src/LoopEditManager.cpp):

- `handleLoopLengthInput` (CC 101): early return if `barStepButtonHandler.isBarSelectActive()`
- `handleLoopStartFaderInput` (pitchbend ch16): early return if `barStepButtonHandler.isBarSelectActive()`

Add `#include "BarStepButtonHandler.h"` and reference the extern `barStepButtonHandler`.

## Files to Modify

- [include/BarStepButtonHandler.h](include/BarStepButtonHandler.h) -- add bar select state, public getters
- [src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp) -- immediate seek in `handleNoteOn`, bar select enter/exit/switch in `executeLoopEditAction`, exit check in `handleNoteOn`
- [src/LoopEditManager.cpp](src/LoopEditManager.cpp) -- block fader input when bar select active
- ~~[src/MidiLedManager.cpp](src/MidiLedManager.cpp) -- use saved loop values for LED rendering during bar select~~ (deferred)

