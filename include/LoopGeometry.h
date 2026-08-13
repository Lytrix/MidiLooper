//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "Utils/InternalHeapFirstAllocator.h"

/// Content-record state for a persisted loop-geometry revision.
enum class LoopGeometryState : uint8_t { Active, Disabled };

/// Immutable Loop content record for a committed length/start change.
/// Distinct from `UndoLoopGeometry` (in-session GUS payload until Stage 3b).
struct LoopGeometry {
  PassId id = kInvalidPassId;
  uint32_t loopStartTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t startLoopTick = 0;
  LoopGeometryState state = LoopGeometryState::Active;
};

using LoopGeometryVec = std::vector<LoopGeometry, InternalHeapFirstAllocator<LoopGeometry>>;
