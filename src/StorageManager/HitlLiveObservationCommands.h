// Capture-only read-only command dispatch shared with the native framing fixture.
#pragma once
#include <Arduino.h>
#include <cstring>
#include "Utils/DebugSessionCapture.h"

namespace StorageManagerInternal {
namespace {
const char kTrackStatusCommand[] PROGMEM = "!TRACK_STATUS";
const char kOverlayStatusCommand[] PROGMEM = "!OVERLAY_STATUS";
const char kTraceFenceCommandPrefix[] PROGMEM = "!TRACE_FENCE ";
const char kPitObserveCommandPrefix[] PROGMEM = "!PIT_OBSERVE ";
const char kPitMotionCommandPrefix[] PROGMEM = "!PIT_MOTION ";
#if MIDI_INGRESS_COST_QUALIFICATION
const char kIngressCostCommandPrefix[] PROGMEM = "!INGRESS_COST ";
#endif
}

#if defined(__IMXRT1062__)
FLASHMEM
#endif
inline bool processLiveObservationCommand(const char* command, void (*printSelectedStatus)(),
                                          void (*printOverlayStatus)() = nullptr,
                                          void (*printPitObservation)(uint32_t) = nullptr,
                                          void (*printPitMotion)(uint32_t) = nullptr,
                                          void (*printIngressCost)(uint32_t) = nullptr) {
  // No transport gate: these scalar replies do not drain the retained ring.
  const bool motion = std::strncmp(command, kPitMotionCommandPrefix, 12) == 0;
#if MIDI_INGRESS_COST_QUALIFICATION
  const bool cost = std::strncmp(command, kIngressCostCommandPrefix, 14) == 0;
#else
  const bool cost = false;
#endif
  if (cost || motion || std::strncmp(command, kPitObserveCommandPrefix, 13) == 0) {
    const char* cursor = command + (cost ? 14 : motion ? 12 : 13);
    uint32_t id = 0;
    bool valid = *cursor != '\0';
    for (; valid && *cursor != '\0'; ++cursor) {
      if (*cursor < '0' || *cursor > '9') { valid = false; break; }
      const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
      if (id > (UINT32_MAX - digit) / 10u) { valid = false; break; }
      id = id * 10u + digit;
    }
    const auto callback = cost ? printIngressCost : motion ? printPitMotion : printPitObservation;
    if (valid && callback != nullptr) callback(id);
    return true; // Malformed requests have no observation/side effects.
  }
  if (std::strncmp(command, kTraceFenceCommandPrefix, 13) == 0) {
    (void)DebugSessionCapture::emitTraceFence(command + 13);
    return true;
  }
  if (std::strcmp(command, kTrackStatusCommand) == 0) {
    printSelectedStatus();
    return true;
  }
  if (std::strcmp(command, kOverlayStatusCommand) == 0) {
    if (printOverlayStatus != nullptr) printOverlayStatus();
    return true;
  }
  return false;
}
}  // namespace StorageManagerInternal
