---
name: Fix BPM off-by-one
overview: Fix the off-by-one error in the 24-pulse BPM measurement in `onMidiClockPulse()` so that the elapsed time spans exactly 24 inter-pulse intervals (one full quarter note).
todos:
  - id: fix-off-by-one
    content: Update onMidiClockPulse() in ClockManager.cpp to measure 24 intervals instead of 23
    status: completed
isProject: false
---

# Fix BPM Off-By-One Error

## Root Cause

The current code records the timestamp at pulse 0 and computes BPM when 24 pulses have been received (count reaches 24). But from pulse 0 to pulse 23 there are only **23 intervals** between pulses. Since one MIDI quarter note is 24 clock ticks, we need **24 intervals** (25 pulse arrivals) to span a full quarter note. This produces a systematic error of `24/23 = 1.0435x`, turning 120 BPM into ~125 BPM.

## Fix

Change `onMidiClockPulse()` in [src/ClockManager.cpp](src/ClockManager.cpp) to wait for 24 intervals (the 25th pulse). Reuse the endpoint timestamp as the next measurement's start so measurements are contiguous with no gap:

```cpp
if (midiClockPulseCount == 0) {
  midiClockQuarterStart = micros();
  midiClockPulseCount = 1;
} else {
  midiClockPulseCount++;
  if (midiClockPulseCount > 24) {
    uint32_t now = micros();
    uint32_t elapsed = now - midiClockQuarterStart;
    if (elapsed > 0) {
      float computedBpm = 60000000.0f / (float)elapsed;
      if (computedBpm >= 20.0f && computedBpm <= 300.0f) {
        setBpmFloat(computedBpm);
      }
    }
    midiClockQuarterStart = now;
    midiClockPulseCount = 1;
  }
}
```

Trace at 120 BPM:

- Pulse 1: count=0, record start, set count=1. (No BPM computed yet)
- Pulse 2: count=2. Pulse 3: count=3. ... Pulse 25: count=25.
- count > 24: elapsed = time(pulse 25) - time(pulse 1) = **24 intervals** = 500,000 us at 120 BPM.
- `60000000 / 500000 = 120.0` BPM. Correct.
- Reuse pulse 25's timestamp as the start of the next measurement (count=1), so no data is lost.

Single file change, single function.