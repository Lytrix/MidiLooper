#pragma once

#include <cstdint>

struct UndoLoopGeometry {
  uint32_t loopLengthTicks = 0;
  uint32_t startLoopTick = 0;
  uint32_t loopStartTick = 0;
};
