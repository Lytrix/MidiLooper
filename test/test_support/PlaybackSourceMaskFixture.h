// Native PRIMASK boundary only; no source/build/runtime algorithms.
#pragma once
#include <cstdint>
#include <vector>
namespace PlaybackSourceMaskFixture {
struct Event { bool enter; uint32_t before, after; unsigned depth; };
inline uint32_t primask = 0;
inline unsigned depth = 0;
inline std::vector<Event> events;
inline void (*pendingInterrupt)() = nullptr;
inline void (*observeUnmasked)() = nullptr;
inline void reset(uint32_t incoming = 0) {
  primask = incoming; depth = 0; events.clear();
  pendingInterrupt = observeUnmasked = nullptr;
}
inline void request(void (*interrupt)()) {
  if (primask) pendingInterrupt = interrupt;
  else interrupt();
}
}
namespace PlaybackSourceNativeMask {
inline uint32_t enter() {
  using namespace PlaybackSourceMaskFixture;
  const auto saved = primask; primask = 1; ++depth;
  events.push_back({true,saved,primask,depth}); return saved;
}
inline void restore(uint32_t saved) {
  using namespace PlaybackSourceMaskFixture;
  events.push_back({false,primask,saved,depth}); --depth; primask = saved;
  if (primask == 0) {
    if (observeUnmasked) observeUnmasked();
    if (pendingInterrupt) {
      auto interrupt = pendingInterrupt; pendingInterrupt = nullptr; interrupt();
    }
  }
}
}
