//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "CaptureAppendResult.h"

const char* captureAppendDenyReasonLabel(CaptureAppendDenyReason reason) {
  switch (reason) {
    case CaptureAppendDenyReason::Accepted:
      return "accepted";
    case CaptureAppendDenyReason::PhaseNone:
      return "phase_none";
    case CaptureAppendDenyReason::PendingPass:
      return "pending_pass";
    case CaptureAppendDenyReason::Duplicate:
      return "duplicate";
    case CaptureAppendDenyReason::PoolAlloc:
      return "pool_alloc";
    case CaptureAppendDenyReason::StoreOther:
      return "store_other";
  }
  return "store_other";
}
