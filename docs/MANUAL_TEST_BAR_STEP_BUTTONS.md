# Manual Test Guide: Bar Step Buttons

This document describes how to manually test the Bar Step Button Handler. Notes 0-15 (16th steps) and 17-24 (bars) on channel 16 trigger mode-specific actions in Loop Edit and Note Edit.

## Enabling Test Logging

To see `[BARSTEP]` log output over Serial (115200 baud):

1. In `src/main.cpp` setup(), uncomment:
   ```cpp
   barStepButtonHandler.setTestLoggingEnabled(true);
   logger.setCategoryEnabled(CAT_BAR_STEP_BUTTON, true);
   ```
2. Rebuild and upload.

## Test Point Markers in Logs

Log lines include `[TEST POINT: ...]` tags for key events:

| Tag | When | What to verify |
|-----|------|----------------|
| `note received` | Any bar/step note On/Off | Routing from controller to handler works |
| `press start` | Note On for bar/step | Button registered, held count increments |
| `release` | Note Off | Duration logged, hadTwoBeforeRelease correct |
| `short` | Short press committed | After double-tap window with no second tap |
| `long press` | Hold > 600ms | Single-button long press |
| `two-button hold` | 2+ held, then release | Range logged (min-max) |
| `action dispatch` | Before executing | Mode (LOOP_EDIT/NOTE_EDIT) and press type |
| `seek done` | Loop Edit short | currentTick seeked to 16th/bar position |
| `loop set` | Loop Edit hold one | Loop start/length set to single region |
| `loop range` | Loop Edit hold two | Loop spans button range |
| `undo` | Triple press | Loop start undo executed |

## Manual Test Procedures

### Prerequisites

- Controller sending notes 0-15 (16th) and 17-24 (bar) on channel 16 over USB
- Or use a MIDI monitor (e.g. from DAW) to send test notes
- Switch to **LOOP_EDIT** mode (Edit Mode button) for loop actions
- Switch to **NOTE_EDIT** mode for note actions

### 1. Routing Test

**Goal**: Confirm bar/step notes reach the handler and do not go to MidiButtonManager.

1. Enable test logging.
2. Send Note On ch16 note 0 (16th step 0).
3. **Expected**: `[BARSTEP]` log with `[TEST POINT: note received]`. No other button action.
4. Send Note On ch16 note 17 (bar 0).
5. **Expected**: Same routing.

### 2. Short Press (Loop Edit)

**Goal**: Playhead seeks to 16th/bar position.

1. Be in LOOP_EDIT mode. Track with loop data.
2. Short-press 16th button (e.g. note 3).
3. **Expected**: `[TEST POINT: short]`, then `[TEST POINT: seek done]`. Display shows playhead at that 16th.

### 3. Hold One Button (Loop Edit)

**Goal**: Loop shrinks to single 16th or bar.

1. Be in LOOP_EDIT mode.
2. Hold a 16th or bar button for > 600ms, then release.
3. **Expected**: `[TEST POINT: long press]`, `[TEST POINT: loop set]`. Loop length = 1×16th or 1×bar.

### 4. Hold Two Buttons (Loop Edit)

**Goal**: Loop spans from first to second button.

1. Be in LOOP_EDIT mode.
2. Press and hold 16th button 1, then press and hold 16th button 3. Hold both > 600ms, then release one.
3. **Expected**: `[TEST POINT: two-button hold]` with `range 1-3`. Loop start/length updated.

### 5. Triple Press (Undo)

**Goal**: Loop start undo after a loop change.

1. Change loop (hold one or two buttons).
2. Triple-press the same or another bar/step button.
3. **Expected**: `[TEST POINT: undo]`. Loop start restored.

### 6. Double Tap

**Goal**: Double press detected (continuous loop toggle – TODO).

1. Double-tap a 16th button within ~300ms.
2. **Expected**: `[TEST POINT: double]`. Action logged (TODO: continuous loop region).

### 7. Note Edit Mode

**Goal**: Note Edit actions (selector, bulk, add/remove – TODO).

1. Switch to NOTE_EDIT mode.
2. Short-press a 16th button.
3. **Expected**: `[TEST POINT: action dispatch]` with `mode=NOTE_EDIT`. Action stubbed as TODO.

## Simulating Without Hardware

Use a MIDI utility to send:

- **Note On** ch16, note 0–15 or 17–24, vel 127
- **Note Off** ch16, same note, vel 0

Delay between On and Off controls short vs long: < 600ms = short, ≥ 600ms = long.
For two-button hold: send Note On for two different notes, wait > 600ms, then Note Off for one.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No `[BARSTEP]` logs | Test logging enabled? CAT_BAR_STEP_BUTTON enabled? |
| Notes go to transport/other | Channel 16? Note in 0–15 or 17–24? |
| Short always fires as long | Hold time < 600ms? |
| Two-button not detected | Both buttons held > 600ms before release? Same type (both 16th or both bar)? |
| Wrong loop position | Loop Edit mode? Track has data? |
