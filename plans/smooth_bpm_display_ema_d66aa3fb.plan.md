---
name: Smooth BPM display EMA
overview: Add a light EMA (alpha=0.3) to the per-quarter-note BPM measurement to filter out USB timing jitter while still responding to real BPM changes within a few quarter notes.
todos:
  - id: ema-smooth
    content: Add bpmSmoothed member, EMA in onMidiClockPulse, reset in actuallyTransition
    status: completed
isProject: false
---

# Smooth BPM Display with EMA

## Why the jitter occurs

USB MIDI has a 1ms polling interval. The timestamps of the first and last pulse in a 24-interval window are each subject to up to ~1ms of jitter. Over a 500ms window (120 BPM), that is roughly +/-0.4-1% error, explaining the +/-1 BPM bounce.

## Solution: per-quarter-note EMA

Apply EMA **after** the 24-interval measurement (once per quarter note), not per-pulse. With alpha=0.3:

- **Jitter suppression**: alternating 119/121 readings converge to display ~119.8-120.2 (effectively stable 120.0 on the display).
- **Response to real change**: a step from 120 to 121 BPM shows as 120.3 -> 120.5 -> 120.7 -> 120.8 ... reaching 121.0 within ~~8 quarter notes (~~4 seconds at 120 BPM). The user can see the change happening progressively.
- The first measurement after clock connect uses the raw value directly (no stale history).

## Changes

### 1. [include/ClockManager.h](include/ClockManager.h) -- add one member

Add `float bpmSmoothed;` under the existing BPM measurement members (line 66):

```cpp
// --- BPM from MIDI clock (24-pulse measurement) ---
uint8_t midiClockPulseCount;
uint32_t midiClockQuarterStart;
float bpmSmoothed;
```

### 2. [src/ClockManager.cpp](src/ClockManager.cpp) -- three small edits

**Constructor** (line 28): initialize `bpmSmoothed(0.0f)`.

`**onMidiClockPulse()**` (lines 116-118): apply EMA before calling `setBpmFloat`:

```cpp
float computedBpm = 60000000.0f / (float)elapsed;
if (computedBpm >= 20.0f && computedBpm <= 300.0f) {
  if (bpmSmoothed > 0.0f) {
    computedBpm = 0.3f * computedBpm + 0.7f * bpmSmoothed;
  }
  bpmSmoothed = computedBpm;
  setBpmFloat(computedBpm);
}
```

`**actuallyTransition(EXTERNAL, INTERNAL)**` (line 85): reset `bpmSmoothed = 0.0f` alongside the existing `midiClockPulseCount = 0` so the next external clock connection starts with a fresh measurement.