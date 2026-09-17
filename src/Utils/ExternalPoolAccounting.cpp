//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/ExternalPoolAccounting.h"

#if defined(__IMXRT1062__)

#include <Arduino.h>

#include <cstring>

namespace {

/// Power-of-two shadow table capacity. The first run at 512 saturated immediately
/// (`blocks` pinned at the ceiling with `untracked`/`unknown` in the thousands), so the
/// pool holds far more blocks than "a few large buffers" — mostly small event rows and
/// node entries. 8192 entries is 64 KB of RAM2, which is free; overflow is still
/// counted rather than ignored, so a second saturation is visible, not silent.
constexpr size_t kShadowCapacity = 8192;
constexpr size_t kShadowMask = kShadowCapacity - 1;

/// Tombstone for a retired entry. Deletion must not break probe chains, otherwise a
/// later lookup would stop early and report a live block as an unknown free.
void* const kTombstone = reinterpret_cast<void*>(1);

struct ShadowEntry {
  void* pointer = nullptr;
  uint32_t bytes = 0;
};

/// RAM2. Four kilobytes of shadow metadata must not touch the ~700 bytes of ITCM
/// headroom or the DTCM budget. `.bss.dma` is NOLOAD and not zeroed at startup, so
/// the table is cleared lazily on first use, keyed off a normal `.bss` flag.
DMAMEM ShadowEntry gShadow[kShadowCapacity];
bool gShadowReady = false;

ExternalPoolAccounting::Counters gCounters;

size_t shadowSlotFor(const void* pointer) {
  const uintptr_t mixed = reinterpret_cast<uintptr_t>(pointer) >> 4;
  return static_cast<size_t>((mixed * 2654435761u) & kShadowMask);
}

void ensureShadowReady() {
  if (gShadowReady) {
    return;
  }
  std::memset(gShadow, 0, sizeof(gShadow));
  gShadowReady = true;
}

}  // namespace

namespace ExternalPoolAccounting {

const Counters& counters() { return gCounters; }

void recordPoolAllocation(void* pointer, size_t bytes) {
  if (pointer == nullptr) {
    return;
  }
  ensureShadowReady();
  const uint32_t clamped = static_cast<uint32_t>(bytes);
  gCounters.cumulativeAllocatedBytes += clamped;

  const size_t start = shadowSlotFor(pointer);
  size_t reusable = kShadowCapacity;
  for (size_t probe = 0; probe < kShadowCapacity; ++probe) {
    ShadowEntry& entry = gShadow[(start + probe) & kShadowMask];
    if (entry.pointer == pointer) {
      // Re-allocation at the same address: retire the stale size first.
      gCounters.liveBytes -= entry.bytes < gCounters.liveBytes ? entry.bytes : gCounters.liveBytes;
      entry.bytes = clamped;
      gCounters.liveBytes += clamped;
      if (gCounters.liveBytes > gCounters.highWaterLiveBytes) {
        gCounters.highWaterLiveBytes = gCounters.liveBytes;
      }
      return;
    }
    if (entry.pointer == kTombstone && reusable == kShadowCapacity) {
      reusable = (start + probe) & kShadowMask;
    }
    if (entry.pointer == nullptr) {
      const size_t slot = reusable != kShadowCapacity ? reusable : ((start + probe) & kShadowMask);
      gShadow[slot].pointer = pointer;
      gShadow[slot].bytes = clamped;
      gCounters.liveBytes += clamped;
      gCounters.liveBlocks += 1;
      gCounters.trackedBlocks += 1;
      if (gCounters.liveBytes > gCounters.highWaterLiveBytes) {
        gCounters.highWaterLiveBytes = gCounters.liveBytes;
      }
      if (gCounters.liveBlocks > gCounters.highWaterLiveBlocks) {
        gCounters.highWaterLiveBlocks = gCounters.liveBlocks;
      }
      return;
    }
  }
  // Table full: the allocation is real but unshadowed, so it is deliberately kept
  // out of `liveBytes` and reported instead.
  gCounters.untrackedBlocks += 1;
}

void recordPoolFree(void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  ensureShadowReady();
  const size_t start = shadowSlotFor(pointer);
  for (size_t probe = 0; probe < kShadowCapacity; ++probe) {
    ShadowEntry& entry = gShadow[(start + probe) & kShadowMask];
    if (entry.pointer == pointer) {
      const uint32_t bytes = entry.bytes;
      entry.pointer = kTombstone;
      entry.bytes = 0;
      gCounters.liveBytes -= bytes < gCounters.liveBytes ? bytes : gCounters.liveBytes;
      gCounters.cumulativeFreedBytes += bytes;
      if (gCounters.liveBlocks > 0) {
        gCounters.liveBlocks -= 1;
      }
      return;
    }
    if (entry.pointer == nullptr) {
      break;
    }
  }
  gCounters.unknownFrees += 1;
}

void reset() {
  gCounters = Counters{};
  std::memset(gShadow, 0, sizeof(gShadow));
  gShadowReady = true;
}

}  // namespace ExternalPoolAccounting

#if defined(EXTERNAL_POOL_WRAP)
extern "C" {
void* __real_extmem_malloc(size_t size);
void __real_extmem_free(void* pointer);

// Linker wrappers (`-Wl,--wrap=extmem_malloc,--wrap=extmem_free`). This is the whole
// reason accounting costs no call-site code: every existing reference to the pool API
// is redirected here at link time instead of each call site gaining a record call.
FLASHMEM void* __wrap_extmem_malloc(size_t size) {
  void* pointer = __real_extmem_malloc(size);
  if (pointer != nullptr) {
    ExternalPoolAccounting::recordPoolAllocation(pointer, size);
  }
  return pointer;
}

FLASHMEM void __wrap_extmem_free(void* pointer) {
  if (pointer != nullptr) {
    ExternalPoolAccounting::recordPoolFree(pointer);
  }
  __real_extmem_free(pointer);
}
}  // extern "C"
#endif  // EXTERNAL_POOL_WRAP

#endif  // __IMXRT1062__
