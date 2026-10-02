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
  /// DEC-108 §14 (task 3.12c): the window-admission limit `L` refused the interval before the event was accepted.
  /// Named so that capacity pressure is distinguishable from a store fault or an allocation failure -- the
  /// requirement's "refused by name at admission" -- and so that no partial row is ever accepted. Appended last, so
  /// no existing reason's value moves.
  WindowAdmission,
};

struct CaptureAppendResult {
  bool accepted = false;
  CaptureAppendDenyReason reason = CaptureAppendDenyReason::Accepted;
};

const char* captureAppendDenyReasonLabel(CaptureAppendDenyReason reason);
