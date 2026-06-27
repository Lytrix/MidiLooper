//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"

namespace PersistenceBudget {

/// Slice budget for deferred save and revision commit while capture or transport is active.
uint32_t resolveMaxPersistenceMicros(bool captureActive, bool transportBudgetActive);

/// Same exhaustion rule as `processDeferredSaveState` slice loop.
bool persistenceSliceBudgetExhausted(uint32_t sliceBudgetUs, uint32_t elapsedUs);

}  // namespace PersistenceBudget
