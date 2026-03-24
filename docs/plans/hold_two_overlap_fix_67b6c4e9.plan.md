---
name: HOLD_TWO Overlap Fix
overview: The HOLD_TWO gesture for selecting bar 2+3 (or any two-bar range) fails because it requires an 800ms gap between presses. When the user holds bar 2 and presses bar 3 within ~400ms, the gap is too short and HOLD_TWO never fires—instead LONG_PRESS on bar 1 and SHORT_PRESS on bar 2 are dispatched, leaving only the last bar selected.
todos: []
isProject: false
---

# HOLD_TWO Overlap-Based Fix

## Root Cause

From the terminal log (lines 62-77):

- Bar 1 (note 18) pressed at 259.270
- Bar 2 (note 19) pressed at 259.665 → **gap = 395ms**
- Bar 1 released at 260.092
- Log: `hadTwo duration=822ms gap=395ms overlap=427ms firstHeldBeforeSecond=0`
- HOLD_TWO requires `gap >= holdTwoMinGap` (800ms) per [src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp) line 273
- Since 395 < 800, `firstHeldBeforeSecond` is false → HOLD_TWO never fires
- Result: LONG_PRESS on bar 1 → single bar, then SHORT_PRESS on bar 2 → overwrites to single bar 2

Current condition (line 273):

```cpp
firstHeldBeforeSecond = !isSwap && (gap >= holdTwoMinGap);
```

The design requires waiting 800ms after the first press before adding the second—intended to avoid accidental two-finger taps. This creates poor UX: the user must hold one bar for 800ms before pressing the second.

## Proposed Fix

Add an **alternative path** so HOLD_TWO also fires when the user clearly held both intentionally:

1. **First button held long enough**: `(now - minStart) >= longPressTime` (600ms) — same threshold as long press
2. **Overlap sufficient**: `overlapDuration >= 200` — both buttons held together for 200ms, excluding quick tap-tap

New condition:

```cpp
bool firstHeldLongEnough = (now - minStart) >= longPressTime;
bool overlapSufficient = (overlapDuration >= 200);
firstHeldBeforeSecond = !isSwap && (
    (gap >= holdTwoMinGap) ||
    (firstHeldLongEnough && overlapSufficient)
);
```

## Behavior After Fix


| Scenario                                                            | gap    | overlap | firstHeldLongEnough | Result                |
| ------------------------------------------------------------------- | ------ | ------- | ------------------- | --------------------- |
| Hold bar 1 for 600ms, press bar 2 at 400ms, release bar 1 at 1000ms | 400ms  | 600ms   | yes                 | HOLD_TWO              |
| Hold bar 1 for 600ms, press bar 2 at 400ms, release bar 2 at 650ms  | 400ms  | 250ms   | yes                 | HOLD_TWO              |
| Quick tap bar 1, tap bar 2 within 200ms                             | 150ms  | 50ms    | no                  | no HOLD_TWO (correct) |
| Original: wait 800ms after bar 1, then press bar 2                  | 800ms+ | any     | yes                 | HOLD_TWO (unchanged)  |


## Files to Modify

- **[src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp)**: In `handleNoteOff`, update the `firstHeldBeforeSecond` computation (around lines 267-275) to add the overlap-based alternative.

## Constants

- 200ms overlap threshold: intentionally short to allow natural "hold one, add second" without changing existing constants in [include/Utils/PressTiming.h](include/Utils/PressTiming.h). Could be made configurable later if needed.
- `longPressTime` (600ms) and `holdTwoMinGap` (800ms) remain unchanged.

