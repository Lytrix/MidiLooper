// Boundary stubs for the real blocking-drain suite.
//
// The drain itself is production code under test; these are the cold boundaries it reaches that cannot
// be linked natively, each recorded rather than silently satisfied:
//
//   * `RtcTime` reads the Teensy RTC through `core_pins.h`/imxrt registers, so its TU does not compile
//     for `native` at all. The drain only needs a monotonic non-zero wall clock for the persisted
//     `lastActiveUnix` field.
//   * `LooperStateManager::exitLoadSaveMode` lives in `src/LooperState.cpp`, which also defines the
//     `looperState` global the shared harness owns; including the TU is a duplicate definition. The
//     method only closes the set-browser overlay, which is unreachable from a drain fixture.
//
// Neither is domain logic under test.
#pragma once

#include <cstdint>

#include "LooperState.h"

namespace DrainHarness {

/// Test-controlled wall clock. A fixture may pin it.
inline uint32_t& wallClockSeconds() {
  static uint32_t seconds = 1700000000u;
  return seconds;
}

}  // namespace DrainHarness

namespace RtcTime {
uint32_t getUnixTime() { return DrainHarness::wallClockSeconds(); }
void raiseWallClockToAtLeast(uint32_t value) {
  if (value > DrainHarness::wallClockSeconds()) {
    DrainHarness::wallClockSeconds() = value;
  }
}
}  // namespace RtcTime

void LooperStateManager::exitLoadSaveMode() {
  // Qualified: `LooperStateManager` has a member named `looperState` (the enum), which shadows the
  // global `LooperStateManager looperState` object the harness owns.
  ::looperState.setLoadSaveModeForTest(false);
}
