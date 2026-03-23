---
name: Stable BPM display
overview: Lower EMA alpha to 0.02 and add a 0.1 display dead-band to guarantee zero display flicker while preserving accuracy for real fractional BPM values.
todos:
  - id: ema-alpha
    content: Lower EMA alpha to 0.02 in ClockManager.cpp
    status: completed
  - id: display-deadband
    content: Add 0.1 display dead-band in DisplayManager.cpp drawInfoArea
    status: completed
isProject: false
---

# Stable BPM Display

## Problem

With EMA alpha=0.05, the smoothed BPM still jitters ±0.1, causing the display to flicker between e.g. 119.9 and 120.1. Any dead-band large enough to mask this would hide real fractional BPM changes like 119.1.

## Solution: two changes

### 1. Lower EMA alpha to 0.02

In [src/ClockManager.cpp](src/ClockManager.cpp), change the EMA line (currently set by user to 0.05):

```cpp
computedBpm = 0.02f * computedBpm + 0.98f * bpmSmoothed;
```

This reduces smoothed jitter to approximately ±0.03 BPM. With 24 sliding-window updates per quarter note, step response is still fast -- a 0.9 BPM change (120 to 119.1) reaches the target within ~~6 quarter notes (~~3 seconds).

### 2. Display dead-band of 0.1

In [src/DisplayManager.cpp](src/DisplayManager.cpp), in `drawInfoArea()`, replace:

```cpp
char bpmStr[8];
snprintf(bpmStr, sizeof(bpmStr), "%.1f", (double)bpm);
```

With:

```cpp
static float displayedBpm = 0.0f;
if (displayedBpm == 0.0f || fabsf(bpm - displayedBpm) >= 0.1f) {
  displayedBpm = bpm;
}
char bpmStr[8];
snprintf(bpmStr, sizeof(bpmStr), "%.1f", (double)displayedBpm);
```

## Why this combination works

- **Steady state**: smoothed BPM jitters ±0.03 around true value. Dead-band of 0.1 never triggers. Display is rock-solid.
- **Real 0.1 BPM change (e.g. 120.0 to 119.9)**: smoothed value settles near 119.9 ± 0.03 (i.e. 119.87-119.93). Distance from displayed 120.0 is 0.07-0.13. Once it exceeds 0.1, display updates accurately.
- **Fractional BPM (e.g. 119.1)**: displayed correctly once the smoothed value converges (within ~3 seconds).

Two files, one line each.