---
name: HOLD_TWO jam state fix
overview: Convert HOLD_TWO from permanent loop mutation to temporary jam state (same pattern as bar select), fixing both the "notes disappear" display bug and the "hard to exit" UX issue.
todos:
  - id: header-flag
    content: Add `bool isHoldTwoJam` private member to BarStepButtonHandler.h
    status: completed
  - id: constructor-init
    content: Initialize isHoldTwoJam(false) in constructor initializer list
    status: completed
  - id: enter-bar-select
    content: Set isHoldTwoJam = false in enterBarSelect()
    status: completed
  - id: exit-bar-select
    content: Set isHoldTwoJam = false in exitBarSelect()
    status: completed
  - id: handle-note-on
    content: "Restructure handleNoteOn jam exit: any press exits HOLD_TWO jam, same-bar exits bar select"
    status: completed
  - id: hold-two-case
    content: Replace HOLD_TWO loop mutation with track.setJam() + isHoldTwoJam = true, remove undo push
    status: completed
  - id: build-verify
    content: Build with pio run and verify no compilation errors
    status: completed
isProject: false
---

# HOLD_TWO Jam State Fix

## Problem

Two issues with HOLD_TWO:

1. **Notes disappear** because `track.setLoopLength()` shrinks `loopLengthTicks`, and `getCachedNotes()` passes this to `reconstructNotes()` which discards notes beyond the new boundary at [NoteUtils.cpp line 55](src/Utils/NoteUtils.cpp).
2. **Hard to exit** because the loop params are permanently changed; only triple-press undo can restore them.

## Solution

Convert HOLD_TWO to use the jam state (same pattern as bar select). Add a `isHoldTwoJam` flag to distinguish HOLD_TWO jams from bar-select jams, since their exit behavior differs: any short press exits HOLD_TWO, but bar select requires same-bar press to exit.

## File Changes

### 1. [include/BarStepButtonHandler.h](include/BarStepButtonHandler.h) line 68

Add private member:

```cpp
bool isHoldTwoJam;
```

### 2. [src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp) — four changes:

**a) Constructor (line 23-37):** Initialize `isHoldTwoJam(false)` in the initializer list.

**b) `enterBarSelect` (line 390):** Set `isHoldTwoJam = false` so entering bar select clears the flag.

**c) `handleNoteOn` jam exit logic (lines 199-204):** Replace the current single-case check with:

```cpp
if (trackRef.isJamming()) {
    if (isHoldTwoJam) {
        // Any button press exits HOLD_TWO jam
        exitBarSelect();
    } else if (info.type == BarStepButtonType::BAR && info.stepIndex == selectedBarIndex) {
        // Same bar press exits bar select
        exitBarSelect();
    }
}
```

This keeps bar-select behavior unchanged (same bar exits / different bar handled by HOLD_ONE), but lets any press exit a HOLD_TWO jam.

**d) `HOLD_TWO_BUTTONS` case (lines 461-475):** Replace permanent loop mutation with jam state:

- Remove `TrackUndo::pushLoopStartSnapshot(track)` (no loop params change, nothing to undo)
- Remove `track.setLoopStartTick(rStartStorage)` and `track.setLoopLength(rLen)`
- Add `track.setJam(rStartStorage, rLen)` and `isHoldTwoJam = true`

**e) `exitBarSelect` (line 401):** Reset `isHoldTwoJam = false` on exit.

## Why This Fixes Both Issues

- **Notes visible**: `loopLengthTicks` stays at the full loop length, so `getCachedNotes()` reconstructs all notes. The display uses `getJamLength()`/`getJamStartTick()` to clip the view to the selected range.
- **Easy exit**: Any short press (bar or 16th) when `isHoldTwoJam` is true clears the jam and returns to full loop view.

