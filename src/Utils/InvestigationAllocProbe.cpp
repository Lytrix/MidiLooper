//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/InvestigationAllocProbe.h"

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG

namespace InvestigationAllocProbe {

volatile uint32_t clockPulseEntriesCount = 0;
volatile uint32_t internalClockDispatchCount = 0;
volatile uint32_t intervalTimerEntryCount = 0;
volatile uint32_t intervalTimerUsTotal = 0;
volatile uint32_t spanBuildUsTotal = 0;
volatile uint32_t projectUsTotal = 0;

}  // namespace InvestigationAllocProbe

#endif
