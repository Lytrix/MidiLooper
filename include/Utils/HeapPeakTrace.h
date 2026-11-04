//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Task 4.20 per-`_sbrk` accounting (firmware half; the ring is `Utils/HeapPeakRing.h`). Capture builds only: the
// build adds `-D HEAP_PEAK_TRACE=1 -Wl,--wrap=malloc -Wl,--wrap=_sbrk`, so no call site changes and the production
// image carries none of it. Zero RAM1 by construction: the ring lives in RAM2 (`DMAMEM`), the code and every literal
// are FLASHMEM/PSTR, and the wrappers never allocate.
#pragma once

#include <cstddef>
#include <cstdint>

namespace HeapPeakTrace {
#if defined(HEAP_PEAK_TRACE) && defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
/// Emit up to `maxRows` pending events as Tier-A `DIAG,heap_probe,sbrk,...` rows (the prefix is already classified
/// Tier-A, so no new literal reaches the classifier's DTCM unit). Edge-driven by the caller's existing 2 s census
/// cadence; never per turn, never an allocation.
void emitPending(size_t maxRows);
/// Tag the allocations made until the next call (0 clears), so a row names the operation that caused it. Written
/// to RAM2 only; no allocation, no I/O.
void setContext(uint8_t context);
#else
inline void emitPending(size_t) {}
inline void setContext(uint8_t) {}
#endif
// Memory-pressure investigation (owner ruling 2026-11-04), diagnostic image only (`HEAP_OWNER_ATTRIBUTION`): every live
// internal-heap block attributed to the site that allocated it, and the top owners by live bytes emitted with the
// census. A failed internal allocation is named immediately (site, size, tail, largest free chunk).
#if defined(HEAP_OWNER_ATTRIBUTION) && defined(HEAP_PEAK_TRACE) && defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
void emitOwners(size_t maxRows);
/// Failed internal allocations since this boot armed the tables (each one fell to the caller's fallback, if any).
uint32_t allocFailCount();
#else
inline void emitOwners(size_t) {}
inline uint32_t allocFailCount() { return 0; }
#endif
}  // namespace HeapPeakTrace
