//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Task 4.20 per-`_sbrk` accounting. See `include/Utils/HeapPeakTrace.h`.
//
// Two linker wrappers, the same zero-call-site-cost pattern as `ExternalPoolAccounting.cpp`:
//   * `__wrap_malloc` remembers the request size and its return site while newlib serves it, and records requests of
//     `kLargeRequestBytes` or more -- those are what shrink the largest free chunk without moving the tail;
//   * `__wrap__sbrk` records every real movement of the arena frontier and attributes it to the malloc in flight.
// Neither allocates, formats or does I/O: they write one fixed slot of a RAM2 ring. Formatting happens in
// `emitPending`, called from the existing 2 s census turn.
#include "Utils/HeapPeakTrace.h"

#if defined(HEAP_PEAK_TRACE) && defined(SESSION_CAPTURE) && defined(__IMXRT1062__)

#include <Arduino.h>

#include <cstdint>
#include <cstdio>

#include "Utils/DebugSessionCapture.h"
#include "Utils/HeapPeakRing.h"
#include "Utils/MemoryMonitor.h"

extern "C" {
void* __real_malloc(size_t size);
void* __real__sbrk(int incr);
extern char* __brkval;
extern char _heap_end;
}

namespace {
constexpr size_t kRingEvents = 32;
// Requests this large are worth a row even when the tail did not move. The step under investigation is 45,056 B.
constexpr size_t kLargeRequestBytes = 32 * 1024;

DMAMEM HeapPeakTrace::Ring<kRingEvents> gRing;
// In-flight malloc context, read by the `_sbrk` wrapper. Plain statics are enough: newlib's malloc is not reentrant
// and holds no lock on this single-core target, so there is at most one request in flight.
// Both live in RAM2 with the ring: two plain statics here were measured at +32 B of DTCM (`variables` 96,192 ->
// 96,224, the 32 B align edge), which the RAM1 budget gate refuses. No initialiser is needed -- the wrapper writes
// both before any read.
DMAMEM uint32_t gPendingRequest;
DMAMEM uint32_t gPendingCaller;
// RAM2 is uninitialised at power-on, so the tag carries a marker: anything without it reads as "no context".
constexpr uint32_t kContextMarker = 0xC7A50000u;
DMAMEM uint32_t gContext;
inline uint8_t currentContext() {
  return (gContext & 0xFFFF0000u) == kContextMarker ? static_cast<uint8_t>(gContext & 0xFFu) : 0;
}

inline uint32_t tailBytes() { return static_cast<uint32_t>(&_heap_end - __brkval); }
}  // namespace

#if defined(HEAP_OWNER_ATTRIBUTION)
// ---- Owner attribution (diagnostic image only) -----------------------------------------------------------------
// A live-block table (pointer -> owner, size) and an owner table (site -> live bytes/blocks, peak, allocations). Both
// in EXTMEM (PSRAM), never RAM2: on this target the internal heap arena is the RAM2 left after `DMAMEM`, so a RAM2
// table would shrink the very arena under investigation (the first image did, and exhausted it at boot). No
// allocation and no I/O inside the wrappers. The tables survive a warm reset, so arming is per boot (`gArmedAtUs`).
namespace {
constexpr uint32_t kLiveSlots = 8192;  // power of two
constexpr uint32_t kOwners = 192;
constexpr uint32_t kArmed = 0x0A77A1B5u;
struct LiveSlot {
  uint32_t pointer;
  uint32_t size;
  uint16_t owner;
  uint16_t state;  // 0 empty, 1 live, 2 freed (a tombstone)
};
struct Owner {
  uint32_t site;
  uint32_t live;
  uint32_t blocks;
  uint32_t peak;
  uint32_t allocs;
};
EXTMEM LiveSlot gLive[kLiveSlots];
EXTMEM Owner gOwners[kOwners];
EXTMEM uint32_t gOwnerCount;
EXTMEM uint32_t gOwnerArmed;
EXTMEM uint32_t gUntracked;  // blocks the live table could not hold
EXTMEM uint32_t gNewCaller;  // operator new's caller, consumed by the nested malloc
EXTMEM uint32_t gFailSeen;

EXTMEM uint32_t gArmedAtUs;
// Armed this boot: the marker, and an arming time not later than now (micros() restarts at every boot).
FLASHMEM bool armedThisBoot() { return gOwnerArmed == kArmed && gArmedAtUs <= micros(); }
FLASHMEM void armOwners() {
  if (armedThisBoot()) return;
  for (uint32_t i = 0; i < kLiveSlots; ++i) gLive[i] = LiveSlot{};
  for (uint32_t i = 0; i < kOwners; ++i) gOwners[i] = Owner{};
  gOwnerCount = 0;
  gUntracked = 0;
  gNewCaller = 0;
  gFailSeen = 0;
  gArmedAtUs = micros();
  gOwnerArmed = kArmed;
}
FLASHMEM uint32_t slotOf(uint32_t pointer) { return ((pointer >> 3) * 2654435761u) & (kLiveSlots - 1); }
FLASHMEM uint16_t ownerFor(uint32_t site) {
  for (uint32_t i = 0; i < gOwnerCount; ++i)
    if (gOwners[i].site == site) return static_cast<uint16_t>(i);
  if (gOwnerCount < kOwners) {
    gOwners[gOwnerCount] = Owner{site, 0, 0, 0, 0};
    return static_cast<uint16_t>(gOwnerCount++);
  }
  return static_cast<uint16_t>(kOwners - 1);  // the last slot aggregates any overflow of distinct sites
}
FLASHMEM void trackAlloc(void* pointer, uint32_t size, uint32_t site) {
  if (pointer == nullptr) return;
  armOwners();
  const uint16_t owner = ownerFor(site);
  Owner& o = gOwners[owner];
  o.live += size;
  ++o.blocks;
  ++o.allocs;
  if (o.live > o.peak) o.peak = o.live;
  const uint32_t key = reinterpret_cast<uint32_t>(pointer);
  for (uint32_t probe = 0, i = slotOf(key); probe < 64; ++probe, i = (i + 1) & (kLiveSlots - 1)) {
    if (gLive[i].state != 1) {
      gLive[i] = LiveSlot{key, size, owner, 1};
      return;
    }
  }
  ++gUntracked;
}
FLASHMEM void trackFree(void* pointer) {
  if (pointer == nullptr || !armedThisBoot()) return;
  const uint32_t key = reinterpret_cast<uint32_t>(pointer);
  for (uint32_t probe = 0, i = slotOf(key); probe < 64; ++probe, i = (i + 1) & (kLiveSlots - 1)) {
    LiveSlot& slot = gLive[i];
    if (slot.state == 0) return;  // not tracked (allocated before arming, or by newlib's internal _malloc_r)
    if (slot.state == 1 && slot.pointer == key) {
      Owner& o = gOwners[slot.owner];
      o.live -= slot.size < o.live ? slot.size : o.live;
      if (o.blocks) --o.blocks;
      slot.state = 2;
      return;
    }
  }
}
// A failed internal allocation: named at once and directly (the device may not survive to a later census turn).
FLASHMEM void reportFailure(uint32_t size, uint32_t site) {
  ++gFailSeen;
  if (gFailSeen > 4) return;
  char line[176];
  const int n = snprintf_P(line, sizeof(line),
      PSTR("#CAP,%lu,DIAG,alloc_census,fail,site=0x%08lX,size=%lu,tail=%lu,largest=%lu,untracked=%lu\r\n"),
      static_cast<unsigned long>(micros()), static_cast<unsigned long>(site), static_cast<unsigned long>(size),
      static_cast<unsigned long>(tailBytes()),
      static_cast<unsigned long>(MemoryMonitor::largestInternalFreeChunkBytes()),
      static_cast<unsigned long>(gUntracked));
  if (n > 0) {
    Serial.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
    Serial.flush();
  }
}
}  // namespace
#endif  // HEAP_OWNER_ATTRIBUTION

extern "C" {
FLASHMEM void* __wrap_malloc(size_t size) {
  gPendingRequest = static_cast<uint32_t>(size);
  gPendingCaller = reinterpret_cast<uint32_t>(__builtin_return_address(0));
#if defined(HEAP_OWNER_ATTRIBUTION)
  if (gNewCaller != 0 && armedThisBoot()) gPendingCaller = gNewCaller;  // a C++ new: its own caller owns it
#endif
  void* pointer = __real_malloc(size);
  if (size >= kLargeRequestBytes) {
    gRing.record(micros(), 'm', 0, static_cast<uint32_t>(size), gPendingCaller, tailBytes(), currentContext());
  }
#if defined(HEAP_OWNER_ATTRIBUTION)
  if (pointer == nullptr && size != 0) reportFailure(static_cast<uint32_t>(size), gPendingCaller);
  trackAlloc(pointer, static_cast<uint32_t>(size), gPendingCaller);
#endif
  gPendingRequest = 0;
  return pointer;
}

#if defined(HEAP_OWNER_ATTRIBUTION)
void __real_free(void* pointer);
void* __real_realloc(void* pointer, size_t size);
void* __real__Znwj(size_t size);

FLASHMEM void __wrap_free(void* pointer) {
  trackFree(pointer);
  __real_free(pointer);
}

FLASHMEM void* __wrap_realloc(void* pointer, size_t size) {
  const uint32_t site = reinterpret_cast<uint32_t>(__builtin_return_address(0));
  void* moved = __real_realloc(pointer, size);
  if (moved != nullptr || size == 0) {
    trackFree(pointer);
    trackAlloc(moved, static_cast<uint32_t>(size), site);
  } else {
    reportFailure(static_cast<uint32_t>(size), site);
  }
  return moved;
}

FLASHMEM void* __wrap__Znwj(size_t size) {
  armOwners();
  gNewCaller = reinterpret_cast<uint32_t>(__builtin_return_address(0));
  void* pointer = __real__Znwj(size);  // the core's operator new calls malloc, which takes gNewCaller as the site
  gNewCaller = 0;
  return pointer;
}
#endif

FLASHMEM void* __wrap__sbrk(int incr) {
  void* previous = __real__sbrk(incr);
  if (incr > 0 && previous != reinterpret_cast<void*>(-1)) {
    gRing.record(micros(), 's', static_cast<uint32_t>(incr), gPendingRequest, gPendingCaller, tailBytes(), currentContext());
  }
  return previous;
}
}  // extern "C"

namespace HeapPeakTrace {

FLASHMEM void setContext(uint8_t context) { gContext = kContextMarker | context; }

FLASHMEM void emitPending(size_t maxRows) {
#if defined(HEAP_OWNER_ATTRIBUTION)
  emitOwners(8);  // the same 2 s census turn: the owner rows ride the existing call site
#endif
  Event event;
  uint32_t lost = 0;
  char line[176];
  for (size_t row = 0; row < maxRows && gRing.drainOne(event, lost); ++row) {
    const int length = snprintf_P(
        line, sizeof(line),
        PSTR("#CAP,%lu,DIAG,heap_probe,sbrk,n=%lu,t=%lu,k=%c,incr=%lu,req=%lu,caller=0x%08lX,tail=%lu,ctx=%u,lost=%lu\r\n"),
        static_cast<unsigned long>(micros()), static_cast<unsigned long>(event.seq),
        static_cast<unsigned long>(event.atUs), event.kind, static_cast<unsigned long>(event.incr),
        static_cast<unsigned long>(event.request), static_cast<unsigned long>(event.caller),
        static_cast<unsigned long>(event.tailAfter), static_cast<unsigned>(event.context),
        static_cast<unsigned long>(lost));
    if (length <= 0) return;
    DebugSessionCapture::emitEnvelopeRowLine(line);
  }
}

#if defined(HEAP_OWNER_ATTRIBUTION)
FLASHMEM uint32_t allocFailCount() { return armedThisBoot() ? gFailSeen : 0; }

// The top owners by live internal bytes, plus the arena state, delivered directly when USB has room.
FLASHMEM void emitOwners(size_t maxRows) {
  if (!armedThisBoot()) return;
  bool taken[kOwners] = {};
  uint32_t total = 0;
  for (uint32_t i = 0; i < gOwnerCount; ++i) total += gOwners[i].live;
  char line[176];
  int n = snprintf_P(line, sizeof(line),
      PSTR("#CAP,%lu,DIAG,alloc_census,owners,tracked=%lu,owners=%lu,untracked=%lu,tail=%lu,largest=%lu\r\n"),
      static_cast<unsigned long>(micros()), static_cast<unsigned long>(total),
      static_cast<unsigned long>(gOwnerCount), static_cast<unsigned long>(gUntracked),
      static_cast<unsigned long>(tailBytes()),
      static_cast<unsigned long>(MemoryMonitor::largestInternalFreeChunkBytes()));
  if (n > 0) DebugSessionCapture::emitLineDirectOrRing(line);
  for (size_t rank = 0; rank < maxRows; ++rank) {
    uint32_t best = kOwners;
    for (uint32_t i = 0; i < gOwnerCount; ++i)
      if (!taken[i] && (best == kOwners || gOwners[i].live > gOwners[best].live)) best = i;
    if (best == kOwners || gOwners[best].live == 0) break;
    taken[best] = true;
    const Owner& o = gOwners[best];
    n = snprintf_P(line, sizeof(line),
        PSTR("#CAP,%lu,DIAG,alloc_census,owner,rank=%u,site=0x%08lX,live=%lu,blocks=%lu,peak=%lu,allocs=%lu\r\n"),
        static_cast<unsigned long>(micros()), static_cast<unsigned>(rank), static_cast<unsigned long>(o.site),
        static_cast<unsigned long>(o.live), static_cast<unsigned long>(o.blocks), static_cast<unsigned long>(o.peak),
        static_cast<unsigned long>(o.allocs));
    if (n > 0) DebugSessionCapture::emitLineDirectOrRing(line);
  }
}
#endif

}  // namespace HeapPeakTrace

#endif  // HEAP_PEAK_TRACE && SESSION_CAPTURE && __IMXRT1062__
