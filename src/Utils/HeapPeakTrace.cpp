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

extern "C" {
FLASHMEM void* __wrap_malloc(size_t size) {
  gPendingRequest = static_cast<uint32_t>(size);
  gPendingCaller = reinterpret_cast<uint32_t>(__builtin_return_address(0));
  void* pointer = __real_malloc(size);
  if (size >= kLargeRequestBytes) {
    gRing.record(micros(), 'm', 0, static_cast<uint32_t>(size), gPendingCaller, tailBytes(), currentContext());
  }
  gPendingRequest = 0;
  return pointer;
}

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

}  // namespace HeapPeakTrace

#endif  // HEAP_PEAK_TRACE && SESSION_CAPTURE && __IMXRT1062__
