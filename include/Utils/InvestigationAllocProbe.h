//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#ifndef SESSION_CAPTURE_INVESTIGATION_DIAG
#define SESSION_CAPTURE_INVESTIGATION_DIAG 0
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG

namespace InvestigationAllocProbe {

// File-scope counters, zero-initialized before IntervalTimer starts. Do not
// replace these with function-local statics: ClockManager::updateInternalClock
// records from the IntervalTimer ISR, and __cxa_guard_acquire on first init
// from that ISR rebooted the Teensy at the first MIDI clock after Record
// (session_20260826_170149).
// Monotonic since boot, unlike `RuntimeTimingTelemetry::clockPulses`, which is reset every
// reporting window. The length-path probe samples this as a delta across begin/end
// (`beginLengthPathLengthenProbe` -> `endLengthPathLengthenProbe`), so the two counters are
// deliberately separate and must not be collapsed into one.
extern volatile uint32_t clockPulseEntriesCount;
extern volatile uint32_t internalClockDispatchCount;
extern volatile uint32_t intervalTimerEntryCount;
extern volatile uint32_t intervalTimerUsTotal;
extern volatile uint32_t spanBuildUsTotal;
extern volatile uint32_t projectUsTotal;

inline uint32_t clockPulseEntries() {
  return clockPulseEntriesCount;
}

inline uint32_t internalClockDispatches() {
  return internalClockDispatchCount;
}

inline uint32_t intervalTimerEntries() {
  return intervalTimerEntryCount;
}

inline uint32_t intervalTimerUs() {
  return intervalTimerUsTotal;
}

inline uint32_t spanBuildUs() {
  return spanBuildUsTotal;
}

inline uint32_t projectUs() {
  return projectUsTotal;
}

inline void recordClockPulseEntry() {
  ++clockPulseEntriesCount;
}

inline void recordInternalClockDispatch() {
  ++internalClockDispatchCount;
}

inline void recordIntervalTimerSpan(uint32_t durationUs) {
  ++intervalTimerEntryCount;
  intervalTimerUsTotal += durationUs;
}

inline void addSpanBuildUs(uint32_t durationUs) {
  spanBuildUsTotal += durationUs;
}

inline void addProjectUs(uint32_t durationUs) {
  projectUsTotal += durationUs;
}

}  // namespace InvestigationAllocProbe

#endif
