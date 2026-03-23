---
name: BPM Display Clock Fix
overview: "Re-implement BPM display and clock management, fixing the two bugs: measure BPM over 24 pulses (one quarter note) instead of per-pulse for accuracy, and use a ClockSource state machine for clean external/internal switching with BPM persistence."
todos:
  - id: clock-state-machine
    content: Create ClockSourceStateMachine.h and .cpp
    status: completed
  - id: clock-manager-refactor
    content: Fix PPQN, add state machine, 24-pulse BPM, setBpmFloat to ClockManager
    status: completed
  - id: midi-handler-update
    content: Update MidiHandler to use requestTransitionTo
    status: completed
  - id: display-bpm
    content: Add BPM field to DisplayManager drawInfoArea
    status: completed
  - id: storage-bpm
    content: Add BPM to StorageManager v2 format
    status: completed
  - id: main-loop
    content: Add checkClockSource to main loop
    status: completed
  - id: build-verify
    content: Build and verify compilation
    status: completed
isProject: false
---

# BPM Display and Clock Fix

## Root Cause of Previous Bugs

**BPM ~5 too high**: Measured delta between single consecutive MIDI clock pulses. USB buffering and jitter cause systematic bias on individual pulse intervals. At 120 BPM the expected interval is 20,833 us; even 1 ms of jitter causes ~5 BPM error.

**Too rapid updates**: BPM was recalculated on every pulse (24x per quarter note), causing display flicker and feeding noisy values into the IntervalTimer.

## Fix

**Measure over a full quarter note (24 pulses)**: Count 24 pulses, record the timestamp at pulse 0 and pulse 24, compute BPM from the total interval. This gives one stable BPM update per quarter note, eliminates per-pulse jitter, and is mathematically exact.

Formula: `bpm = 60000000.0f / (float)(timestamp24 - timestamp0)`

No EMA needed; no calibration factor needed. One measurement per quarter note is both accurate and stable on the display.

---

## 1. Fix slow internal tempo (PPQN bug, same as before)

**File**: [src/ClockManager.cpp](src/ClockManager.cpp)

The internal clock uses `MidiConfig::PPQN` (24) but the app runs at `Config::INTERNAL_PPQN` (192). Change all three instances:

- Line 42: `microsPerTick = 60000000UL / (bpm * Config::INTERNAL_PPQN);`
- Line 48: same
- Line 54: same

---

## 2. BPM from MIDI clock (24-pulse measurement)

**File**: [include/ClockManager.h](include/ClockManager.h)

Add private members:

```cpp
uint8_t midiClockPulseCount;       // counts 0..23 then resets
uint32_t midiClockQuarterStart;    // micros() at pulse 0
```

**File**: [src/ClockManager.cpp](src/ClockManager.cpp)

In `onMidiClockPulse()`, add:

```cpp
if (midiClockPulseCount == 0) {
  midiClockQuarterStart = micros();
}
midiClockPulseCount++;
if (midiClockPulseCount >= 24) {
  uint32_t elapsed = micros() - midiClockQuarterStart;
  if (elapsed > 0) {
    float computedBpm = 60000000.0f / (float)elapsed;
    if (computedBpm >= 20.0f && computedBpm <= 300.0f) {
      setBpmFloat(computedBpm);
    }
  }
  midiClockPulseCount = 0;
}
```

Initialize `midiClockPulseCount = 0` and `midiClockQuarterStart = 0` in constructor.

---

## 3. ClockSource state machine

Same pattern as [TrackStateMachine](src/TrackStateMachine.cpp).

**New file**: [include/ClockSourceStateMachine.h](include/ClockSourceStateMachine.h)

```cpp
namespace ClockSourceStateMachine {
    bool isValidTransition(ClockSource current, ClockSource next);
    const char* toString(ClockSource source);
}
```

**New file**: [src/ClockSourceStateMachine.cpp](src/ClockSourceStateMachine.cpp)

INTERNAL to EXTERNAL and EXTERNAL to INTERNAL are the only valid transitions.

**File**: [include/ClockManager.h](include/ClockManager.h)

- Replace `bool externalClockPresent` with `ClockSource clockSource`.
- Add `void setBpmFloat(float newBpm)`.
- Add `ClockSource getClockSource() const`.
- Add `void requestTransitionTo(ClockSource target)`.
- Add private: `ClockSource pendingClockSource`, `bool transitionPending`, `void actuallyTransition(ClockSource from, ClockSource to)`.

**File**: [src/ClockManager.cpp](src/ClockManager.cpp)

- `isExternalClockPresent()`: return `clockSource == CLOCK_EXTERNAL`.
- `checkClockSource()`: detect timeout, request INTERNAL, process pending transitions.
- `actuallyTransition(EXTERNAL, INTERNAL)`: reset pulse counter, persist BPM, log.
- `actuallyTransition(INTERNAL, EXTERNAL)`: log.
- `onMidiClockPulse()`: `requestTransitionTo(CLOCK_EXTERNAL)`.
- `onMidiStart()`: `requestTransitionTo(CLOCK_EXTERNAL)`.
- Remove `setExternalClockPresent()` usage, update callers.

**File**: [src/MidiHandler.cpp](src/MidiHandler.cpp)

- `handleMidiContinue()`: change `clockManager.setExternalClockPresent(true)` to `clockManager.requestTransitionTo(CLOCK_EXTERNAL)`.

**File**: [src/main.cpp](src/main.cpp)

- Add `clockManager.checkClockSource()` in the loop after `midiHandler.handleMidiInput()`.

---

## 4. Display BPM field

**File**: [src/DisplayManager.cpp](src/DisplayManager.cpp)

In `drawInfoArea`, after the CHN field:

```cpp
char bpmStr[8];
snprintf(bpmStr, sizeof(bpmStr), "%.1f", bpm);
int bpmX = chnX + (3 + 1 + 2) * 6 + 6; // CHN:XX + space
drawInfoField("BPM", bpmStr, bpmX, y, false, 5);
```

---

## 5. Persist and restore BPM

**File**: [src/StorageManager.cpp](src/StorageManager.cpp)

- Bump `STORAGE_VERSION` to 2.
- In `saveState`: write `float bpm` after version.
- In `loadState`: accept v1 (skip BPM) or v2 (read BPM, set `::bpm`).
- `clockManager.setup()` runs after `looper.setup()` (which loads state), so it picks up the restored BPM.

**Save trigger**: `actuallyTransition(EXTERNAL, INTERNAL)` calls `StorageManager::saveState(...)`.

---

## Files to modify

- [include/ClockSourceStateMachine.h](include/ClockSourceStateMachine.h) -- New
- [src/ClockSourceStateMachine.cpp](src/ClockSourceStateMachine.cpp) -- New
- [include/ClockManager.h](include/ClockManager.h) -- State machine, setBpmFloat, pulse counter
- [src/ClockManager.cpp](src/ClockManager.cpp) -- PPQN fix, 24-pulse BPM, state machine, checkClockSource
- [src/DisplayManager.cpp](src/DisplayManager.cpp) -- Add BPM field
- [src/StorageManager.cpp](src/StorageManager.cpp) -- Version 2, save/load BPM
- [src/MidiHandler.cpp](src/MidiHandler.cpp) -- requestTransitionTo instead of setExternalClockPresent
- [src/main.cpp](src/main.cpp) -- checkClockSource in loop
