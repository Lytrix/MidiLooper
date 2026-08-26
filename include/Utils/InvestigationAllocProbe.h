//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#ifndef SESSION_CAPTURE_INVESTIGATION_DIAG
#define SESSION_CAPTURE_INVESTIGATION_DIAG 0
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG

namespace InvestigationAllocProbe {

inline uint32_t& clockPulseEntries() {
  static uint32_t value = 0;
  return value;
}

inline uint32_t& internalClockDispatches() {
  static uint32_t value = 0;
  return value;
}

inline void recordClockPulseEntry() {
  ++clockPulseEntries();
}

inline void recordInternalClockDispatch() {
  ++internalClockDispatches();
}

}  // namespace InvestigationAllocProbe

#endif
