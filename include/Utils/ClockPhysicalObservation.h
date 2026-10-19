// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>

// Request-only qualification data, not a deadline/correlation authority or resident state.
struct ClockPhysicalObservation {
  uint64_t rawTicks = 0;
  uint64_t beforeUs = 0;
  uint64_t musicalTick = 0;
  uint32_t era = 0;
  uint32_t widthUs = 0;
  uint32_t epoch = 0;
  uint32_t readCycles = 0;
  uint32_t cpuHz = 0;
  uint32_t configBits = 0;
  uint32_t periodUs = 0;
  uint8_t status = 0;
  bool transport = false;
  uint8_t source = 0;
};
