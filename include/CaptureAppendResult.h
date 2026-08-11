//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

enum class CaptureAppendDenyReason : uint8_t {
  Accepted = 0,
  PhaseNone,
  PendingPass,
  Duplicate,
  PoolAlloc,
  StoreOther,
};

struct CaptureAppendResult {
  bool accepted = false;
  CaptureAppendDenyReason reason = CaptureAppendDenyReason::Accepted;
};

const char* captureAppendDenyReasonLabel(CaptureAppendDenyReason reason);
