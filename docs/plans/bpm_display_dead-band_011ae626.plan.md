---
name: BPM display dead-band
overview: Add display-side dead-band hysteresis in DisplayManager so the BPM readout only updates when the smoothed value differs from the displayed value by more than 0.3 BPM, making the display rock-solid during steady-state.
todos:
  - id: dead-band
    content: Add display-side dead-band for BPM in DisplayManager::drawInfoArea
    status: pending
isProject: false
---

# BPM Display Dead-Band

## Problem

EMA smoothing reduces jitter but cannot eliminate it. With alpha=0.05, the displayed BPM still moves ±0.1 due to correlated USB timing bursts.

## Solution

Add a `static float displayedBpm` in `drawInfoArea()` in [src/DisplayManager.cpp](src/DisplayManager.cpp). Only update it when the global `bpm` differs by more than 0.3. Display `displayedBpm` instead of `bpm`.

In `drawInfoArea()`, replace:

```cpp
char bpmStr[8];
snprintf(bpmStr, sizeof(bpmStr), "%.1f", (double)bpm);
```

With:

```cpp
static float displayedBpm = 0.0f;
if (displayedBpm == 0.0f || fabsf(bpm - displayedBpm) > 0.3f) {
  displayedBpm = bpm;
}
char bpmStr[8];
snprintf(bpmStr, sizeof(bpmStr), "%.1f", (double)displayedBpm);
```

Single file, single function, four lines.
