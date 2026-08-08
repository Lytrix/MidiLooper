---
name: Fix Channel 3 LED Blocking
overview: The revert re-introduced `usb = 1` and `trs = 0` in the tick [midiin] block. In DROID 1.7, `usb = 1` on midiin blocks prevents MIDI from reaching circuits. Removing these parameters restores channel 3 LED functionality.
todos: []
isProject: false
---

# Fix Channel 3 LED Logic After Revert

## Root Cause

In [droid/midilooper_v1.ini](droid/midilooper_v1.ini), the **current tick [midiin] block** (lines 200-236) has:

```ini
[midiin]
    usb = 1
    trs = 0
    note1 = 16
    ...
    channel = 3
```

You previously reported: *"I needed to remove usb = 1 of all midi in circuits, else none of the midi would reach any circuit"*. In DROID 1.7, `usb = 1` on [midiin] blocks blocks or redirects incoming MIDI so it never reaches the circuits. The revert brought these parameters back.

The tick block is the **only** [midiin] block with `usb = 1`. The bar content block (line 239) and 8-bar block (line 390) do not have it. Depending on DROID behavior, `usb = 1` on one block can either:

- Only break that block’s MIDI, or  
- Affect routing for all midiin blocks.

Either way, removing `usb = 1` and `trs = 0` from the tick block is required.

## Current State

- **Firmware** ([include/MidiLedManager.h](include/MidiLedManager.h), [src/MidiLedManager.cpp](src/MidiLedManager.cpp)): Uses channel 3, notes 0–15 (bar content), 16–31 (tick), 40–47 (8-bar). Logic looks correct.
- **Call flow** ([src/TrackManager.cpp](src/TrackManager.cpp)): `updateLeds()` and `updateCurrentTick()` are called from the main loop.
- **DROID config** ([droid/midilooper_v1.ini](droid/midilooper_v1.ini)): Tick block has `usb = 1` and `trs = 0`.

## Fix

**File:** [droid/midilooper_v1.ini](droid/midilooper_v1.ini)

Remove lines 201–202 from the tick [midiin] block:

```diff
 [midiin]
-    usb = 1
-    trs = 0
     note1 = 16
```

Resulting block start:

```ini
[midiin]
    note1 = 16
    note2 = 17
    ...
    channel = 3
```

This aligns the tick block with the bar content and 8-bar blocks, which work without `usb`/`trs`.

## Verification

1. Reload the DROID patch in DROID Forge.
2. Confirm:
  - 16th-step bar content LEDs
  - Current tick indicator (notes 16–31)
  - 8-bar LEDs (notes 40–47)

No firmware changes are needed.