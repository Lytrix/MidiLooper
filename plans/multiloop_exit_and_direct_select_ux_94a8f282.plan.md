---
name: Multiloop Exit and Direct Select UX
overview: Change multiloop bar-button behavior so that (1) short-pressing the first bar of the current jam exits to full loop, matching the user's habit, and (2) short-pressing any other bar directly switches to that single bar, eliminating the need to exit first. HOLD_TWO already replaces the multiloop.
todos: []
isProject: false
---

# Multiloop Exit and Direct Select UX

## Current Behavior

**Single-bar jam** (`!isHoldTwoJam`):

- Short press same bar → exit (on NoteOn)
- Short press other bar → switch to that bar

**Multiloop** (`isHoldTwoJam`):

- Short press any bar → **jam navigate**: shifts the jam window to start at that bar, keeps same length (e.g. bars 2+3, press bar 5 → bars 5+6)
- No way to exit via short press; no way to directly switch to a single bar
- TRIPLE_PRESS exits (but also undoes loop, which is a different action)

## Target Behavior


| Gesture                                  | In multiloop   | Result                                       |
| ---------------------------------------- | -------------- | -------------------------------------------- |
| Short press **first bar** of current jam | Yes            | **Exit** to full loop                        |
| Short press **any other bar**            | Yes            | **Switch to single bar** (replace multiloop) |
| HOLD_TWO (bars X+Y)                      | Yes            | Replace with new multiloop (already works)   |
| Short press same bar                     | Single-bar jam | Exit (unchanged)                             |


This gives:

1. **Exit**: Short press first bar of the loop = exit (user's automatism)
2. **Direct select**: No exit-then-select; any bar press outside the first bar goes directly there
3. **HOLD_TWO**: Continues to replace with new range

## Implementation

### 1. Compute "first bar of current jam" in [src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp)

When `isHoldTwoJam` and `track.isJamPlaybackActive()`, the jam start bar index is:

```cpp
uint32_t loopLength = track.getLoopLength();
uint32_t loopStartTick = track.getLoopStartTick();
uint32_t jamStartTick = track.getJamStartTick();
uint32_t offsetInLoop = (jamStartTick - loopStartTick + loopLength) % loopLength;
uint8_t jamStartBarIndex = offsetInLoop / Config::TICKS_PER_BAR;
```

(Clip to bar count if loop has < 8 bars; bar buttons are 0-7.)

### 2. Change SHORT_PRESS handling in `executeLoopEditAction` (lines 443-462)

**Current** (when `isHoldTwoJam`):

```cpp
if (isHoldTwoJam) {
  uint32_t newStart = ...;
  track.setJam(newStart, track.getJamLength());  // jam navigate
  ...
}
```

**New**:

- If `info.stepIndex == jamStartBarIndex` → `exitBarSelect()`
- Else → `switchBarSelect(info.stepIndex)` (single bar, replace multiloop)

This replaces the "jam navigate" behavior with exit + direct single-bar switch.

### 3. Extend handleNoteOn exit-on-press for multiloop (lines 203-208)

**Current**: Exit only when `!isHoldTwoJam && info.stepIndex == selectedBarIndex`.

**New**: Also exit when `isHoldTwoJam` and the pressed bar is the first bar of the current jam. Compute `jamStartBarIndex` as above and compare to `info.stepIndex`. This gives immediate exit on NoteOn (no waiting for SHORT_PRESS timer), matching the single-bar behavior.

### 4. Handle bar-count edge case

If `loopLength < Config::TICKS_PER_BAR` or the loop has fewer than 2 bars, multiloop is unlikely. Ensure `jamStartBarIndex` stays in valid range: `min(jamStartBarIndex, 7)` or use `(loopLength / Config::TICKS_PER_BAR)` as max.

## Files to Modify

- **[src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp)**:
  1. In `handleNoteOn` (around line 203): Add multiloop branch — when `isHoldTwoJam`, compute `jamStartBarIndex` and if `info.stepIndex == jamStartBarIndex`, call `exitBarSelect()`.
  2. In `executeLoopEditAction` SHORT_PRESS (around line 446): Replace jam-navigate logic with: if first bar → exit, else → switchBarSelect.

## Optional: Helper for jam start bar

Add a small helper to avoid duplication:

```cpp
uint8_t getJamStartBarIndex() const;
```

It would use `trackManager.getSelectedTrack()` and compute the bar index. Could live in `BarStepButtonHandler` as a private helper.