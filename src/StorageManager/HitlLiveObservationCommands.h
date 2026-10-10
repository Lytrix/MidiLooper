// Capture-only read-only command dispatch shared with the native framing fixture.
#pragma once
#include <Arduino.h>
#include <cstring>
#include "Utils/DebugSessionCapture.h"

namespace StorageManagerInternal {
namespace {
const char kTrackStatusCommand[] PROGMEM = "!TRACK_STATUS";
const char kTraceFenceCommandPrefix[] PROGMEM = "!TRACE_FENCE ";
}

#if defined(__IMXRT1062__)
FLASHMEM
#endif
inline bool processLiveObservationCommand(const char* command, void (*printSelectedStatus)()) {
  // No transport gate: these scalar replies do not drain the retained ring.
  if (std::strncmp(command, kTraceFenceCommandPrefix, 13) == 0) {
    (void)DebugSessionCapture::emitTraceFence(command + 13);
    return true;
  }
  if (std::strcmp(command, kTrackStatusCommand) == 0) {
    printSelectedStatus();
    return true;
  }
  return false;
}
}  // namespace StorageManagerInternal
