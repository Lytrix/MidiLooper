//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// Device boundary of the runtime profiler (DEC-239): the raw cycle counter, the systick-extended
// wall clock, the exception-context read, the boot CPU-rate contract and the row sink. Native
// fixtures define these themselves. No string literal: CRLF is written as two characters so this TU
// adds nothing to DTCM `.rodata`.

#include "Utils/RuntimeProfiler.h"

#if RUNTIME_PROFILE_SCOPES && defined(ARDUINO) && !defined(PIO_UNIT_TEST_NATIVE)

#include <Arduino.h>

#include <cstring>

#include "Utils/ClockPhysicalReferencePlatform.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MidiServiceDrain.h"

static_assert(RuntimeProfiler::kOwnerTurnCeilingUs == MidiServiceDrain::kOwnerWorkBudgetUs,
              "the idle-owner ceiling must equal the enforced owner budget");

namespace RuntimeProfiler {
namespace Platform {

// Raw read, no `dsb/isb`: unlike `readCycleCounterForObservation()`, this brackets main-lane work and
// needs neither ordering against a peripheral nor a physical-clock sample.
FLASHMEM uint32_t cycles() { return ARM_DWT_CYCCNT; }

FLASHMEM uint32_t micros() { return ::micros(); }

FLASHMEM uint32_t ipsr() {
  uint32_t value;
  __asm__ volatile("mrs %0, ipsr" : "=r"(value));
  return value;
}

FLASHMEM uint32_t cpuHz() { return ClockPhysicalReferencePlatform::cpuClockHzForObservation(); }

FLASHMEM void emitRow(const char* line) {
  if (line == nullptr) return;
  const size_t length = std::strlen(line);
  const int room = Serial.availableForWrite();
  if (room < 0 || static_cast<size_t>(room) >= length + 2u) {
    Serial.write(reinterpret_cast<const uint8_t*>(line), length);
    Serial.write(static_cast<uint8_t>('\r'));
    Serial.write(static_cast<uint8_t>('\n'));
    return;
  }
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::appendCaptureTextLine(line);
#endif
}

}  // namespace Platform
}  // namespace RuntimeProfiler

#endif
