//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

/// Checked capacity at the allocation boundary (owner ruling 2026-11-04, PSRAM exhaustion).
///
/// `ExternalMemoryFirstAllocator` aborts when neither the external pool nor the internal heap can serve a request
/// (device: a 92,160 B `rawEvents` growth in tile preparation, the 2,304-event buffer still alive). A free-byte check
/// cannot answer that: the request needs one contiguous block while the old buffer coexists. `tryReserve` asks the
/// allocator's own two pools for the exact block the growth will request -- with the old buffer still live -- and
/// returns false (nothing allocated, nothing changed) when neither can serve it. On success the probe block is
/// returned and the vector reserves that same size at once, on the same thread, so the reservation is served by the
/// block the probe just proved. Never allocates on failure and never aborts.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "Utils/ExternalMemoryFirstAllocator.h"

#if !defined(PIO_UNIT_TEST_NATIVE)
#include "Utils/MemoryMonitor.h"
#endif

namespace CheckedCapacity {

/// The size of the last refused probe, read by `RefusalBackoff` (only after a refusal set it) to tell a temporary
/// refusal from a request no pool could ever serve. Device: one RAM2 word defined in LoopContentResolution.cpp, so
/// no RAM1 static is added (a function-local static cost 32 B of RAM1 variables).
#if defined(PIO_UNIT_TEST_NATIVE)
inline std::size_t& lastRefusedBytes() {
  static std::size_t bytes = 0;
  return bytes;
}
#else
extern std::size_t gLastRefusedBytes;
inline std::size_t& lastRefusedBytes() { return gLastRefusedBytes; }
#endif

/// Whether `bytes` exceeds what the two pools could hold even if empty: such a request is permanently insufficient.
inline bool exceedsTotalCapacity(std::size_t bytes) {
#if defined(PIO_UNIT_TEST_NATIVE)
  return bytes > nativeExternalPoolPressure().totalCapacityBytes;
#else
  const std::size_t total = static_cast<std::size_t>(MemoryMonitor::getExternalMemoryPoolTotalBytes()) +
                            static_cast<std::size_t>(MemoryMonitor::getInternalHeapTotalBytes());
  return total != 0 && bytes > total;
#endif
}

/// Whether one contiguous block of `bytes` can be served by the external pool or the internal heap right now.
inline bool probe(std::size_t bytes) {
  if (bytes == 0) return true;
#if defined(PIO_UNIT_TEST_NATIVE)
  if (bytes >= nativeExternalPoolPressure().refuseAtOrAboveBytes) {
    ++nativeExternalPoolPressure().probeRefusals;
    lastRefusedBytes() = bytes;
    return false;
  }
  return true;
#else
  void* block = extmem_malloc(bytes);
  if (block != nullptr) {
    extmem_free(block);
    return true;
  }
  block = std::malloc(bytes);
  if (block != nullptr) {
    std::free(block);
    return true;
  }
  lastRefusedBytes() = bytes;
  return false;
#endif
}

/// Ensure `v` can hold `needed` elements without another allocation. The growth is the vector's own doubling rule
/// (at least `needed`), probed as one contiguous block while the old buffer is still live. False: unchanged.
template <typename T, typename A>
bool tryReserve(std::vector<T, A>& v, std::size_t needed) {
  if (v.capacity() >= needed) return true;
  std::size_t target = v.capacity() * 2;
  if (target < needed) target = needed;
  if (!probe(target * sizeof(T))) {
    if (target == needed || !probe(needed * sizeof(T))) return false;
    target = needed;  // the exact need fits where the doubled one does not
  }
  v.reserve(target);
  return true;
}

/// Ensure `v` can hold exactly `needed` elements (no doubling): for a known upper bound that is then used once.
template <typename T, typename A>
bool tryReserveExact(std::vector<T, A>& v, std::size_t needed) {
  if (v.capacity() >= needed) return true;
  if (!probe(needed * sizeof(T))) return false;
  v.reserve(needed);
  return true;
}

/// A lane's answer to a refusal: the work stays owed and is retried after a bounded, doubling wait (64 .. 1,024 turns),
/// never busy; a request no pool could ever serve is parked (named) until the lane's source generation changes.
struct RefusalBackoff {
  static constexpr uint16_t kFirstWait = 64;
  static constexpr uint16_t kMaxWait = 1024;
  uint16_t wait = 0;
  uint16_t step = 0;
  uint32_t refusals = 0;
  bool permanent = false;

  /// True while the lane should not attempt the refused work this turn.
  bool parked() {
    if (permanent) return true;
    if (wait == 0) return false;
    --wait;
    return true;
  }
  void onRefused() {
    ++refusals;
    if (exceedsTotalCapacity(lastRefusedBytes())) {
      permanent = true;
      return;
    }
    step = step == 0 ? kFirstWait : static_cast<uint16_t>(step >= kMaxWait / 2 ? kMaxWait : step * 2);
    wait = step;
  }
  void onProgress() { step = 0; }
  /// The source changed (a new generation or a new cut): a parked request is attempted afresh.
  void reset() {
    wait = 0;
    step = 0;
    permanent = false;
  }
};

}  // namespace CheckedCapacity
