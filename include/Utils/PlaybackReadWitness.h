#pragma once
#include <cstdint>

// Observation of one completed immutable-view reader turn, not a build/readiness promise.
struct PlaybackReadWitness {
  uint32_t atUs = 0;
  uint32_t tick = 0;
  uint32_t generation = 0;
  uint64_t storageGeneration = 0;
  uint32_t start = 0;
  uint32_t length = 0;
  uint32_t period = 0;
  uint32_t phase = 0;
  uint8_t track = 0;
  uint8_t slot = 0;
  uint8_t origin = 0;
  bool availableOnly = false; // retained current-root snapshot, not a completed cursor turn
};
