//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "PersistenceBudget.h"

namespace PersistenceBudget {

uint32_t resolveMaxPersistenceMicros(bool captureActive, bool transportBudgetActive) {
  if (captureActive || transportBudgetActive) {
    return Config::maxPersistenceMicrosActive;
  }
  return Config::maxPersistenceMicrosIdle;
}

bool persistenceSliceBudgetExhausted(uint32_t sliceBudgetUs, uint32_t elapsedUs) {
  return sliceBudgetUs != UINT32_MAX && elapsedUs >= sliceBudgetUs;
}

}  // namespace PersistenceBudget
