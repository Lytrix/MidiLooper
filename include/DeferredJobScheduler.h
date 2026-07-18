//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <stdint.h>

/// Phase B owner of non-realtime deferred job frame execution (DEC-027).
/// B.2: runFrame is the sole main-loop entry; domain step is StorageManager::stepSubmittedLoadJobs.
class DeferredJobScheduler {
public:
    /// Spend up to budgetUs microseconds on deferred jobs (LoadLoopJob in Phase B).
    static void runFrame(uint32_t budgetUs);
};
