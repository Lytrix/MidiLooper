---
name: 8 Bar LED feedback
overview: "Add LED feedback for 8 bar buttons (MIDI notes 40-47 on channel 2): velocity 64 for bars that are used (in loop), velocity 127 for bars that contain notes. Integrate into MidiLedManager and call from the existing LED update flow."
todos: []
isProject: false
---

# 8 Bar LED Feedback (Notes 40-47, Channel 2)

## Overview

Add visualization for 8 bar buttons on the DROID: each LED (notes 40-47 on channel 2) shows whether that bar is used in the loop and whether it contains notes. Velocity 64 = bar used (in loop, empty); Velocity 127 = bar contains notes; NoteOff/velocity 0 = bar not in loop.

## Data Model

- **Bar 0** → note 40, **Bar 1** → note 41, … **Bar 7** → note 47
- **Used**: loop extends into that bar (`loopLength > barIndex * TICKS_PER_BAR`)
- **Has notes**: at least one NoteOn event in that bar's tick range
- **Not used**: bar beyond loop length → send NoteOff

## Implementation

### 1. Add constants and helper in [include/MidiLedManager.h](include/MidiLedManager.h)

- `BAR_LED_BASE_NOTE = 40`
- `NUM_BAR_LEDS = 8`
- `VEL_BAR_USED = 64`
- `VEL_BAR_HAS_NOTES = 127`
- Declare `void updateBarLeds(Track& track, uint32_t loopLength)`
- Declare `bool hasNoteInBar(Track& track, uint32_t barStartTick, uint32_t barEndTick, uint32_t loopLength)` (private)

### 2. Implement helpers and `updateBarLeds` in [src/MidiLedManager.cpp](src/MidiLedManager.cpp)

`**hasNoteInBar`**: Similar to `hasNoteInSixteenthStep` but for a bar's tick range. Iterate `track.getMidiEvents()`, check NoteOn events where `event.tick` falls in `[barStartTick, barEndTick)` with wrap handling.

`**updateBarLeds**`:

- For each bar `i` in 0..7:
  - `barStartTick = i * Config::TICKS_PER_BAR`
  - `barEndTick = (i + 1) * Config::TICKS_PER_BAR`
  - If `loopLength <= barStartTick`: bar not used → `sendNoteOff(ch 2, 40+i, 0)`
  - Else if `hasNoteInBar(...)`: → `sendNoteOn(ch 2, 40+i, 127)`
  - Else: bar used, no notes → `sendNoteOn(ch 2, 40+i, 64)`
- Add small delay between sends (reuse `updateDelayMicros`) to avoid MIDI flooding

### 3. Call `updateBarLeds` from the LED update flow

In `updateLeds()`: after `analyzeAndUpdateBar()`, call `updateBarLeds(track, loopLength)` so bar LEDs refresh on the same bar-boundary updates.

In `forceUpdate()`: already triggers `updateLeds()`, so bar LEDs will refresh on track/loop changes.

In `clearAllLeds()`: send NoteOff for notes 40-47 on channel 2 so bar LEDs are cleared when appropriate.

### 4. Track bar LED state (optional optimization)

To avoid redundant sends, track `lastBarLedState[8]` (or a packed representation) and only send when a bar's velocity changes. For 8 LEDs this may be unnecessary; implement without caching first and add later if needed.

## Tick Ranges and Wrapping

- Bar tick ranges use absolute ticks within the loop (0..loopLength).
- For `hasNoteInBar`: handle wrap when `barEndTick > loopLength` (bar spans loop end) by splitting the check or using the same wrap logic as `hasNoteInSixteenthStep`.

## Files Changed

- [include/MidiLedManager.h](include/MidiLedManager.h): constants, declarations
- [src/MidiLedManager.cpp](src/MidiLedManager.cpp): `hasNoteInBar`, `updateBarLeds`, integration in `updateLeds` and `clearAllLeds`

