---
name: HOLD_TWO fix first-held
overview: Restrict HOLD_TWO_BUTTONS so it only fires when the user holds one button for 600ms before pressing a second, not when two buttons are pressed in quick succession (both held long enough that the released one's duration exceeds 600ms).
todos: []
isProject: false
---

# HOLD_TWO_BUTTONS: Require First Button Held Before Adding Second

## Problem

The range logic (HOLD_TWO_BUTTONS) currently fires when:

- 2+ buttons of the same type (bar or 16th) are pressed at release time, and  
- The **released** button was held for 600ms+.

That allows a false positive: press A, press B ~200ms later, hold both for 500ms, release A. A’s duration = 700ms, so HOLD_TWO fires even though both were pressed in quick succession. The user intended two short presses, not “hold one and add second”.

**Desired**: HOLD_TWO only when the user **holds one button for 600ms, then adds a second** while still holding the first. So the time between the first press and the second press must be ≥ 600ms.

## Fix

Add a check that the **earliest** press among the two held buttons was at least 600ms before the **latest** press. That means the first button was held 600ms before the second was pressed.

- `minPressStart` = earliest `pressStartTime` among currently pressed buttons of that type  
- `maxPressStart` = latest `pressStartTime` among those same buttons  
- Require: `(maxPressStart - minPressStart) >= longPressTime` (600ms)

This is computed **before** clearing the released button in `handleNoteOff`, while both buttons are still marked as pressed.

## Implementation

**File**: [src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp)

1. Add two private helpers to get min/max press start times for the given type (using existing `pressed16thBits` / `pressedBarBits` and `buttonStates`).
2. In `handleNoteOff`, when computing `hadTwoBeforeRelease && duration >= longPressTime` for the HOLD_TWO path, also require `(maxPressStart - minPressStart) >= longPressTime` before firing HOLD_TWO. If this fails, fall through to the normal tap / HOLD_ONE logic (do not treat as two-button hold).

**File**: [include/BarStepButtonHandler.h](include/BarStepButtonHandler.h)

1. Declare the two helpers: `getMinPressStartTimeForType(BarStepButtonType)` and `getMaxPressStartTimeForType(BarStepButtonType)` (or a single `getPressStartTimeRange` returning a pair). Alternatively, a single helper `firstButtonHeldBeforeSecond(uint32_t now, BarStepButtonType)` returning bool, to keep the API minimal.

## Example Logic

```cpp
// In handleNoteOff, when hadTwoBeforeRelease && duration >= longPressTime:
uint32_t minStart = getMinPressStartTimeForType(info.type);
uint32_t maxStart = getMaxPressStartTimeForType(info.type);
uint32_t gap = (maxStart >= minStart) ? (maxStart - minStart) : 0;
if (gap >= longPressTime) {
  // Fire HOLD_TWO: first was held 600ms before second was added
  ...
} else {
  // Fall through to tap/HOLD_ONE logic - don't treat as two-button hold
}
```

## Testing

- **Hold A 600ms, add B, release A**: gap ≥ 600ms → HOLD_TWO ✓  
- **Press A, press B 200ms later, hold both 500ms, release A**: gap = 200ms → no HOLD_TWO, falls through to tap logic ✓  
- **Press A, press B 200ms later, release B quickly**: B duration short → no HOLD_TWO ✓ (existing behavior)

