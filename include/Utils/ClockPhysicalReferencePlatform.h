// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>

// Thin Teensy register/interrupt boundary. ClockManager owns policy and lifetime.
// Native fixtures implement only these hardware operations, not the producer FSM.
namespace ClockPhysicalReferencePlatform {
enum class Register : uint8_t {
  Module, ClockSelect, ClockGate, DebugControl,
  Load0, Control0, Flag0, Load1, Control1, Flag1,
  Control2, Control3, LifetimeHigh, LifetimeLow, Count
};
constexpr uint32_t kClockSelectMask = 0x7f, kOscillator24MHz = 0x40;
constexpr uint32_t kPitGateMask = 3u << 12;
constexpr uint32_t kFreeze = 1, kModuleDisable = 2;
constexpr uint32_t kEnable = 1, kChain = 4, kTimeout = 1;
// C_DEBUGEN may remain set on a normally running board; it does not mean halted.
// Run-mode-only still excludes debugger/sleep interruptions between observations.
constexpr uint32_t kUnsupportedDebugMask = (1u << 17) | (1u << 18);
uint32_t read(Register reg);
void write(Register reg, uint32_t value);
uint32_t maskInterrupts();
void restoreInterrupts(uint32_t saved);
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
uint32_t readCycleCounterForObservation();
uint32_t cpuClockHzForObservation(); // Zero when the short-interval DWT source is disabled.
#endif
}
