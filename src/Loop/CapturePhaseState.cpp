// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "LoopPasses.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define CAPTURE_PHASE_MEM FLASHMEM
#else
#define CAPTURE_PHASE_MEM
#endif

CAPTURE_PHASE_MEM CapturePhaseState& CapturePhaseState::operator=(CapturePhase value) {
  if (generation_) generation_ = generation_ == UINT64_MAX ? 0 : generation_ + 1;
  value_ = value;
  return *this;
}
