//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoadLoopBudget.h"

namespace LoadLoopBudget {

uint32_t resolveLoadLoopSliceBudgetUs(bool bootTitle, bool focusWork, bool captureActive) {
  if (bootTitle) {
    return BootTitleRestoreUs;
  }
  if (focusWork) {
    return FocusRestoreUs;
  }
  if (captureActive) {
    return 0;
  }
  return BackgroundRestoreUs;
}

bool loadLoopSliceBudgetExhausted(uint32_t sliceBudgetUs, uint32_t elapsedUs) {
  return sliceBudgetUs != UINT32_MAX && elapsedUs >= sliceBudgetUs;
}

bool shouldFinishActiveBeforePreempt(size_t bytesRead, size_t payloadSize, size_t chunkBytes) {
  if (payloadSize == 0) {
    return true;
  }
  if (bytesRead >= payloadSize) {
    return true;
  }
  const size_t remaining = payloadSize - bytesRead;
  return remaining <= chunkBytes;
}

}  // namespace LoadLoopBudget
