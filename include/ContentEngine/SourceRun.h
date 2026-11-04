//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"

namespace ContentEngine {

/// One run of a composition source (a manifest's runs in pass order).
struct SourceRun {
  RunId run;
  uint32_t rowLimit;  // rows readable now (a writer's prefix, or the sealed count)
};

}  // namespace ContentEngine
