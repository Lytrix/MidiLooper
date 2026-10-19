// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "ClockManager.h"
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
#include "Utils/ClockPhysicalReferencePlatform.h"
#if defined(__IMXRT1062__)
#define CLOCK_OBSERVATION_MEM FLASHMEM
#else
#define CLOCK_OBSERVATION_MEM
#endif

CLOCK_OBSERVATION_MEM ClockPhysicalObservation ClockManager::observePhysicalClockForHitl() const {
  using namespace ClockPhysicalReferencePlatform;
  ClockPhysicalObservation out{};
  // Measure the real production read with the caller's incoming mask unchanged.
  // This includes call/barrier overhead and possible interrupts; not a WCET bound.
  out.cpuHz = cpuClockHzForObservation();
  const uint32_t cycleBefore = readCycleCounterForObservation();
  const auto timed = getPhysicalClockStamp();
  out.readCycles = readCycleCounterForObservation() - cycleBefore;
  if (out.cpuHz == 0) out.readCycles = 0;

  const uint32_t saved = maskInterrupts();
  // Failed startup still reports the mismatching configuration, not an identity.
  // Never access peripheral registers when its bus gate is disabled.
  const bool gate = (read(Register::ClockGate) & kPitGateMask) == kPitGateMask;
  const uint32_t debug = read(Register::DebugControl);
  out.configBits =
      ((debug & kUnsupportedDebugMask) == 0 ? 512u : 0u) |
      (gate && (read(Register::ClockSelect) & kClockSelectMask) == kOscillator24MHz ? 64u : 0u) |
      ((debug & 1u) != 0 ? 1024u : 0u) |
      ((debug & (1u << 17)) != 0 ? 2048u : 0u) |
      ((debug & (1u << 18)) != 0 ? 4096u : 0u);
  if (gate) out.configBits |=
      (read(Register::Module) == kFreeze ? 1u : 0u) |
      (read(Register::Load0) == UINT32_MAX && read(Register::Load1) == UINT32_MAX ? 2u : 0u) |
      (read(Register::Control0) == kEnable ? 4u : 0u) |
      (read(Register::Control1) == (kEnable | kChain) ? 8u : 0u) |
      (read(Register::Control2) == 3u ? 16u : 0u) |
      (read(Register::Control3) == 0 ? 32u : 0u) |
      ((read(Register::Flag0) & kTimeout) != 0 ? 128u : 0u) |
      ((read(Register::Flag1) & kTimeout) != 0 ? 256u : 0u);
  const auto before = getPhysicalClockStamp();
  if (before.era != 0) {
    // Shared lifetime latch remains exclusively held through raw and musical reads.
    const uint32_t high = read(Register::LifetimeHigh);
    const uint32_t low = read(Register::LifetimeLow);
    out.rawTicks = ~((uint64_t(high) << 32) | low);
    const auto musical = getPlaybackDeadlineClock();
    out.musicalTick = musical.tick;
    out.epoch = musical.epoch;
    out.transport = isTransportRunning();
    out.source = static_cast<uint8_t>(getClockSource());
    out.periodUs = getMicrosPerTick();
    const auto after = getPhysicalClockStamp();
    const uint64_t rawUs = out.rawTicks / 24u;
    if (timed.era == before.era && before.era == after.era &&
        before.microseconds <= rawUs && rawUs <= after.microseconds &&
        after.microseconds - before.microseconds <= UINT32_MAX) {
      out.era = before.era;
      out.beforeUs = before.microseconds;
      out.widthUs = static_cast<uint32_t>(after.microseconds - before.microseconds);
    }
  }
  out.status = static_cast<uint8_t>(physicalClockReferenceStatus());
  restoreInterrupts(saved);
  // A Ready status with era0 is still explicitly unqualified (e.g. a bracket mismatch).
  return out;
}
#endif
