---
name: Double-Press Exit and Jam Seek
overview: Use double press (not short press) to exit single-bar and multiloop. Remove same-bar exit so short press always seeks/resets jamTick. Enable jamTick seek when multiloop is active, matching the immediate seek behavior available before selecting any bar.
todos: []
isProject: false
---

# Double-Press Exit and Jam Seek

## Current Behavior

- **Same-bar short press** (handleNoteOn): exits single-bar jam — causes unwanted exits
- **HOLD_ONE same bar**: exits
- **TRIPLE_PRESS**: exits + undo loop
- **DOUBLE_PRESS**: TODO (no action)
- **Multiloop bar short press**: jam navigate (shifts window), no seek
- **16th short press in jam**: seeks via `setJamTick` when `seekPos < jamLength` (single-bar works; multiloop first bar works)
- **Before selecting** (not jamming): bar/16th press does immediate seek (handleNoteOn, lines 211-224)

## Target Behavior


| Gesture                       | Result                                                                    |
| ----------------------------- | ------------------------------------------------------------------------- |
| **Double press** any bar/16th | **Exit** (single-bar and multiloop)                                       |
| **Short press** bar/16th      | **Seek** (reset jamTick to that position, quantized to 16th) — never exit |
| **Triple press**              | Undo loop only (no exit)                                                  |
| **HOLD_ONE** same bar         | Seek, not exit                                                            |


Seek when jamming must work for both single-bar and multiloop, matching the immediate seek used before selecting.

## Implementation

### 1. Remove same-bar exit from [src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp)

**handleNoteOn** (lines 203-207): Delete the block that exits when `!isHoldTwoJam && info.stepIndex == selectedBarIndex`. Do not exit on same-bar press.

### 2. Add immediate seek when jamming (handleNoteOn)

When `trackRef.isJamPlaybackActive() && !potentialHoldTwo` and bar/16th is pressed, perform the same seek as the non-jamming case but via `setJamTick` instead of `clockManager.setCurrentTick`:

- Compute `tickInLoop` for the pressed bar/16th (as in lines 216-219)
- Quantize to 16th: `seekTick = (tickInLoop / TICKS_PER_16TH_STEP) * TICKS_PER_16TH_STEP`
- Compute offset within jam: `(tickInLoop - jamStartOffset + loopLength) % loopLength` where `jamStartOffset = (jamStartTick - loopStartTick + loopLength) % loopLength`
- If offset < `jamLength`: `track.setJamTick(offset)`, `trackManager.forceLedUpdate`

This mirrors the “before selecting” behavior for jam mode.

### 3. SHORT_PRESS when jamming: always seek, never exit

**executeLoopEditAction** SHORT_PRESS (lines 443-462):

- **BAR**:
  - If bar is within current jam: seek to that bar’s start (`setJamTick` to bar offset in jam)
  - Else: `switchBarSelect(info.stepIndex)` (switch to single bar)
- **16th**: Keep existing logic: `setJamTick(seekPos)` when `seekPos < jamLength`

Remove any exit logic from SHORT_PRESS.

### 4. HOLD_ONE same bar: seek instead of exit

**executeLoopEditAction** HOLD_ONE (lines 465-472):

- When `track.isJamming() && info.stepIndex == selectedBarIndex`: do **seek** (`setJamTick(0)` for that bar) instead of `exitBarSelect`
- When jamming and different bar: keep `switchBarSelect`
- When not jamming: keep `enterBarSelect`

### 5. DOUBLE_PRESS → exit

**executeLoopEditAction** DOUBLE_PRESS (lines 502-504):

- Replace TODO with: `exitBarSelect()` when `track.isJamming()`

### 6. TRIPLE_PRESS → undo only

**executeLoopEditAction** TRIPLE_PRESS (lines 506-508):

- Remove `exitBarSelect()`; keep only `TrackUndo::undoLoopStart(track)`

## Edge cases

- **16th in multiloop**: `seekPos = stepIndex * TICKS_PER_16TH_STEP` addresses 16ths 0–15 (first bar). For the second bar, use bar buttons. If the jam is longer than 1 bar, clamp or wrap `seekPos` into `[0, jamLength)`.
- **Bar outside jam**: `switchBarSelect` (unchanged).

## Files to Modify

- **[src/BarStepButtonHandler.cpp](src/BarStepButtonHandler.cpp)**:
  1. handleNoteOn: Remove same-bar exit block; add immediate seek when jamming
  2. executeLoopEditAction SHORT_PRESS: Make BAR seek when bar is in jam; ensure no exit
  3. executeLoopEditAction HOLD_ONE: Same bar → seek, not exit
  4. executeLoopEditAction DOUBLE_PRESS: Call `exitBarSelect()` when jamming
  5. executeLoopEditAction TRIPLE_PRESS: Remove `exitBarSelect()`

