//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/ExternalPoolAccounting.h"

#if defined(__IMXRT1062__)
#include "Utils/MemoryMonitor.h"
#include <Arduino.h>
#endif

#if defined(__IMXRT1062__) || defined(EXTERNAL_POOL_ACCOUNTING_NATIVE_TEST)

#if defined(EXTERNAL_POOL_ACCOUNTING_NATIVE_TEST)
#define FLASHMEM
#define DMAMEM
#endif

#include <cstdio>
#include <cstring>

namespace {

/// Power-of-two shadow table capacity. The first run at 512 saturated immediately
/// (`blocks` pinned at the ceiling with `untracked`/`unknown` in the thousands), so the
/// pool holds far more blocks than "a few large buffers" — mostly small event rows and
/// node entries. 8192 entries occupy 96 KiB of RAM2, reducing internal heap capacity; overflow is still
/// counted rather than ignored, so a second saturation is visible, not silent.
constexpr size_t kShadowCapacity = 8192;
constexpr size_t kShadowMask = kShadowCapacity - 1;

/// Tombstone for a retired entry. Deletion must not break probe chains, otherwise a
/// later lookup would stop early and report a live block as an unknown free.
void* const kTombstone = reinterpret_cast<void*>(1);

struct ShadowEntry {
  void* pointer = nullptr;
  uint32_t bytes = 0;
  /// Bracket tag active when this block was allocated, so a free decrements the
  /// right tag regardless of the scope at free time. Costs 96 KB of RAM2 for the
  /// table (12 B/entry) and buys exact per-step attribution.
  uint8_t tag = 0;
};

/// Bracket stack. Depth 4 is well past the nesting a prewarm step uses.
constexpr size_t kTagStackDepth = 4;
uint8_t gTagStack[kTagStackDepth] = {};
uint8_t gCurrentTag = 0;
size_t gTagDepth = 0;

/// RAM2. Shadow metadata must not consume ITCM/DTCM headroom. `.bss.dma` is NOLOAD and not zeroed at startup, so
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

FLASHMEM const Counters& counters() { return gCounters; }

FLASHMEM void formatTagVector(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  size_t used = 0;
  out[0] = '\0';
  for (size_t tag = 1; tag < Counters::kTagCount; ++tag) {
    const int written = std::snprintf(out + used, outLen - used, "%s%lu",
                                      used == 0 ? "" : "/",
                                      static_cast<unsigned long>(gCounters.tagBytes[tag] / 1024));
    if (written <= 0 || static_cast<size_t>(written) >= outLen - used) {
      break;
    }
    used += static_cast<size_t>(written);
  }
}

FLASHMEM void beginTag(uint8_t tag) {
  if (gTagDepth < kTagStackDepth) {
    gTagStack[gTagDepth++] = gCurrentTag;
  }
  gCurrentTag = tag;
}

FLASHMEM void endTag() {
  gCurrentTag = gTagDepth > 0 ? gTagStack[--gTagDepth] : 0;
}

FLASHMEM void recordPoolAllocation(void* pointer, size_t bytes) {
  if (!isExternalPoolAddress(pointer)) {
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
      // Re-allocation at the same address: retire the stale size and its class first.
      const size_t staleClass = sizeClassFor(entry.bytes);
      gCounters.liveBytesByClass[staleClass] -=
          entry.bytes < gCounters.liveBytesByClass[staleClass] ? entry.bytes
                                                              : gCounters.liveBytesByClass[staleClass];
      if (gCounters.liveBlocksByClass[staleClass] > 0) {
        gCounters.liveBlocksByClass[staleClass] -= 1;
      }
      gCounters.liveBytes -= entry.bytes < gCounters.liveBytes ? entry.bytes : gCounters.liveBytes;
      gCounters.tagBytes[entry.tag] -= entry.bytes < gCounters.tagBytes[entry.tag]
          ? entry.bytes : gCounters.tagBytes[entry.tag];
      if (gCounters.tagBlocks[entry.tag] > 0) --gCounters.tagBlocks[entry.tag];
      entry.tag = gCurrentTag;
      entry.bytes = clamped;
      gCounters.liveBytes += clamped;
      const size_t freshClass = sizeClassFor(clamped);
      gCounters.liveBytesByClass[freshClass] += clamped;
      gCounters.liveBlocksByClass[freshClass] += 1;
      gCounters.tagBytes[gCurrentTag] += clamped;
      gCounters.tagBlocks[gCurrentTag] += 1;
      if (gCounters.liveBytes > gCounters.highWaterLiveBytes) {
        gCounters.highWaterLiveBytes = gCounters.liveBytes;
      }
      return;
    }
    if (entry.pointer == kTombstone && reusable == kShadowCapacity) {
      reusable = (start + probe) & kShadowMask;
    }
    if (entry.pointer == nullptr) {
      if (reusable == kShadowCapacity) reusable = (start + probe) & kShadowMask;
      break;
    }
  }
  if (reusable != kShadowCapacity) {
    const size_t slot = reusable;
    gShadow[slot].pointer = pointer;
    gShadow[slot].bytes = clamped;
    gShadow[slot].tag = gCurrentTag;
    gCounters.liveBytes += clamped;
    gCounters.liveBlocks += 1;
    gCounters.trackedBlocks += 1;
    const size_t freshClass = sizeClassFor(clamped);
    gCounters.liveBytesByClass[freshClass] += clamped;
    gCounters.liveBlocksByClass[freshClass] += 1;
    gCounters.tagBytes[gCurrentTag] += clamped;
    gCounters.tagBlocks[gCurrentTag] += 1;
    if (gCounters.liveBytes > gCounters.highWaterLiveBytes) {
      gCounters.highWaterLiveBytes = gCounters.liveBytes;
    }
    if (gCounters.liveBlocks > gCounters.highWaterLiveBlocks) {
      gCounters.highWaterLiveBlocks = gCounters.liveBlocks;
    }
    return;
  }
  // Table full: the allocation is real but unshadowed, so it is deliberately kept
  // out of `liveBytes` and reported instead.
  gCounters.untrackedBlocks += 1;
}

FLASHMEM void recordPoolFree(void* pointer) {
  if (!isExternalPoolAddress(pointer)) {
    return;
  }
  ensureShadowReady();
  const size_t start = shadowSlotFor(pointer);
  for (size_t probe = 0; probe < kShadowCapacity; ++probe) {
    ShadowEntry& entry = gShadow[(start + probe) & kShadowMask];
    if (entry.pointer == pointer) {
      const uint32_t bytes = entry.bytes;
      const size_t blockClass = sizeClassFor(bytes);
      gCounters.liveBytesByClass[blockClass] -=
          bytes < gCounters.liveBytesByClass[blockClass] ? bytes
                                                         : gCounters.liveBytesByClass[blockClass];
      if (gCounters.liveBlocksByClass[blockClass] > 0) {
        gCounters.liveBlocksByClass[blockClass] -= 1;
      }
      const uint8_t tag = entry.tag;
      gCounters.tagBytes[tag] -= bytes < gCounters.tagBytes[tag] ? bytes : gCounters.tagBytes[tag];
      if (gCounters.tagBlocks[tag] > 0) {
        gCounters.tagBlocks[tag] -= 1;
      }
      entry.pointer = kTombstone;
      entry.bytes = 0;
      entry.tag = 0;
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

FLASHMEM void recordPoolRefusal(size_t bytes, uintptr_t caller) {
  const uint32_t clamped = static_cast<uint32_t>(bytes);
  gCounters.poolRefusedCount += 1;
#if defined(__IMXRT1062__)
  gCounters.poolLastRefusalMs = millis();
#endif
  if (clamped > gCounters.poolRefusedLargestBytes) {
    gCounters.poolRefusedLargestBytes = clamped;
  }
#if defined(__IMXRT1062__)
  // Bounded so a refusal storm cannot flood the transport, but a blanket cap hid the
  // *fatal* request behind a tolerated retry storm (the first run logged twelve 32 KB
  // refusals from mid-record and never showed the one that aborted at overdub start).
  // So: always report the first few, then only when the refused size changes, which
  // keeps the distinct shapes visible for bounded output. Direct serial, not the
  // `#CAP` ring, which does not flush while PLAYING/OVERDUBBING. No allocation, no
  // pool walk.
  constexpr uint32_t kRefusalReportLimit = 8;
  static uint32_t lastLoggedRefusedBytes = 0;
  const bool sizeChanged = clamped != lastLoggedRefusedBytes;
  if (gCounters.poolRefusedCount > kRefusalReportLimit && !sizeChanged) {
    return;
  }
  lastLoggedRefusedBytes = clamped;
  char tagVector[160];
  formatTagVector(tagVector, sizeof(tagVector));
  Serial.printf_P(
      PSTR("#MEM_EXT_REFUSED,%lu,bytes=%lu,count=%lu,live=%luKB,blocks=%lu,heap_free=%luKB,"
           "tag=%u,tag_live=%luKB,tags=%s,accounting=2,caller=0x%lx\n"),
      static_cast<unsigned long>(micros()), static_cast<unsigned long>(clamped),
      static_cast<unsigned long>(gCounters.poolRefusedCount),
      static_cast<unsigned long>(gCounters.liveBytes / 1024),
      static_cast<unsigned long>(gCounters.liveBlocks),
      static_cast<unsigned long>(MemoryMonitor::getInternalHeapFreeBytes() / 1024),
      static_cast<unsigned>(gCurrentTag),
      static_cast<unsigned long>(gCounters.tagBytes[gCurrentTag] / 1024), tagVector,
      static_cast<unsigned long>(caller));
#endif
}

FLASHMEM void reset() {
  gCounters = Counters{};
  gCurrentTag = 0;
  gTagDepth = 0;
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
    return pointer;
  }
  // The core has already tried its internal fallback. Capture the wrapper's
  // return site, not a stack walk; resolve with the exact flashed ELF.
#if defined(SESSION_CAPTURE)
  const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#else
  const uintptr_t caller = 0;
#endif
  ExternalPoolAccounting::recordPoolRefusal(size, caller);
  return nullptr;
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
