//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/** Advisory system pressure — subsystems consult; owners decide safe reclaim. */
enum class MemoryPressureLevel : uint8_t {
  Normal = 0,
  Low = 1,
  Critical = 2,
};

const char* memoryPressureLevelName(MemoryPressureLevel level);
