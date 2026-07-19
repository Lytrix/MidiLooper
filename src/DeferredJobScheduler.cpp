//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DeferredJobScheduler.h"

#include "StorageManager.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define DEFERRED_JOB_SCHEDULER_MEM FLASHMEM
#else
#define DEFERRED_JOB_SCHEDULER_MEM
#endif

void DEFERRED_JOB_SCHEDULER_MEM DeferredJobScheduler::runFrame(uint32_t budgetUs) {
    // B.3: scheduler owns selection then step; StorageManager owns job storage/begin.
    const bool activatedThisFrame = StorageManager::selectSubmittedLoadJobs();
    StorageManager::stepSubmittedLoadJobs(budgetUs, activatedThisFrame);
}
