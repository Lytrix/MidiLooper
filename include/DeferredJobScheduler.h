//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <stdint.h>

/// Phase B owner of non-realtime deferred job frame execution (DEC-027).
/// B.1: delegates load work to StorageManager::runDeferredFrame (behavior-preserving).
class DeferredJobScheduler {
public:
    /// Spend up to budgetUs microseconds on deferred jobs (LoadLoopJob in B.1).
    static void runFrame(uint32_t budgetUs);
};
