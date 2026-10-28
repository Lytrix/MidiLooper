//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MemoryMonitor.h"
#include "Utils/HeapPeakTrace.h"
#if defined(__IMXRT1062__)
#include "Globals.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/MemoryPool.h"
#include "Logger.h"
#include <Arduino.h>

#include "LoopEventStore.h"
#include "LoopSnapshot.h"
#include "PersistenceQueue.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/MemoryPressurePolicy.h"
#include "Utils/StoppedHeapDiagnostics.h"

#include <smalloc.h>

// Linker symbols from imxrt1062_t41.ld
extern "C" {
  extern char *__brkval;
  extern unsigned long _heap_start;
  extern unsigned long _heap_end;
  extern uint8_t external_psram_size;  // MB detected at startup (Teensy core)
  // newlib dlmalloc free-list head (`mallocr.c`): the arena's top chunk and the small-bin lists. Read-only
  // here; `StoppedHeapDiagnostics::scanInternal` pins the ABI it walks.
  extern uint32_t __malloc_av_[258];
}

namespace MemoryMonitor {

namespace {

uint32_t sMinEverFreeBytes = UINT32_MAX;

void updateInternalHeapWatermark(uint32_t freeBytes) {
  if (freeBytes < sMinEverFreeBytes) {
    sMinEverFreeBytes = freeBytes;
  }
}

void getPsramStats(size_t* totalUsed, size_t* totalFree) {
  if (totalUsed) *totalUsed = 0;
  if (totalFree) *totalFree = 0;
  if (extmem_smalloc_pool.pool_size == 0 || extmem_smalloc_pool.pool == nullptr) {
    return;
  }
  // sm_malloc_stats_pool always writes *free (and uses *total) even when those
  // out-params are null — pass stack locals and copy out selectively.
  size_t total = 0;
  size_t userUsed = 0;
  size_t freeBytes = 0;
  int blocks = 0;
  // sm_malloc_stats_pool returns 1 when blocks exist, 0 when pool is empty but valid,
  // -1 on verify failure. Either non-negative rc fills freeBytes (= pool_size - total).
  const int rc = sm_malloc_stats_pool(&extmem_smalloc_pool, &total, &userUsed, &freeBytes, &blocks);
  if (rc >= 0) {
    if (totalUsed) *totalUsed = total;
    if (totalFree) *totalFree = freeBytes;
  }
  (void)userUsed;
  (void)blocks;
}

}  // namespace

// Conservative unused arena tail, not malloc free-list capacity. Do not interpret
// zero as proof that all previously allocated blocks remain live.
uint32_t getInternalHeapFreeBytes() {
  int32_t freeBytes = reinterpret_cast<char*>(&_heap_end) - __brkval;
  const uint32_t free = freeBytes > 0 ? static_cast<uint32_t>(freeBytes) : 0;
  updateInternalHeapWatermark(free);
  return free;
}

// Task 4.20: the allocator's own largest-block answer, read-only. Bounded by `scanInternal` (≤ 4096 free chunks) and
// allocation-free, so it can run on a user-gesture admission without perturbing the arena it is judging. `arena` is
// computed from the break directly (== `mallinfo().arena`, which is only refreshed on demand) so this never depends on
// a prior `mallinfo()` call.
FLASHMEM uint32_t largestInternalFreeChunkBytes() {
  const uintptr_t start = reinterpret_cast<uintptr_t>(&_heap_start);
  const uintptr_t end = reinterpret_cast<uintptr_t>(__brkval);
  const uint32_t arena = end >= start ? static_cast<uint32_t>(end - start) : 0u;
  if (arena == 0) {
    return getInternalHeapFreeBytes();  // nothing allocated yet: the tail is the whole arena
  }
  const StoppedHeapDiagnostics::Internal internal = StoppedHeapDiagnostics::scanInternal(
      reinterpret_cast<uintptr_t>(__malloc_av_), start, end, arena,
      [](uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); });
  if (!internal.complete) {
    return 0u;  // an incomplete walk must never override the tail verdict
  }
  return internal.largest;
}

uint32_t getInternalHeapMinEverFreeBytes() {
  if (sMinEverFreeBytes == UINT32_MAX) {
    return getInternalHeapFreeBytes();
  }
  return sMinEverFreeBytes;
}

void resetInternalHeapWatermark() {
  sMinEverFreeBytes = UINT32_MAX;
  (void)getInternalHeapFreeBytes();
}

uint32_t getInternalHeapTotalBytes() {
  return static_cast<uint32_t>(reinterpret_cast<char*>(&_heap_end) -
                               reinterpret_cast<char*>(&_heap_start));
}

/// Task 4.20: take the exact block and give it straight back. Deliberately *not* folded into
/// `probeBlockAvailable`: that one answers "can anyone serve this" and is used as an admission authority for the
/// point-of-use reserves, while this one must keep **internal heap** and **external pool** separable — the whole
/// question is whether the internal arena can serve a request the tail metric says it cannot. Raw entry points only,
/// so a refusal is a nullptr rather than the aborting C++ allocators' process death.
HeapAllocatabilityProbe probeInternalHeapAllocatability(uint32_t bytes) {
  HeapAllocatabilityProbe out;
  out.tailFreeBytes = getInternalHeapFreeBytes();
  out.requestedBytes = bytes;
  if (bytes == 0) {
    return out;
  }
  void* block = std::malloc(bytes);
  if (block != nullptr) {
    std::free(block);
    out.internalMallocOk = true;
    return out;
  }
  block = extmem_malloc(bytes);
  if (block != nullptr) {
    extmem_free(block);
    out.externalPoolOk = true;
  }
  return out;
}

/// Task 4.20: the admission authority. One `malloc`/`free` pair, internal heap only — deliberately **no** `extmem`
/// fallback, because the question the floor+margin test is really asking is "is there room *here*", and a request the
/// pool serves says nothing about that. Returns true for a zero request (nothing to prove).
bool allocatorCanServeInternalBlock(uint32_t bytes) {
  if (bytes == 0) {
    return true;
  }
  void* block = std::malloc(bytes);
  if (block == nullptr) {
    return false;
  }
  std::free(block);
  return true;
}

/// Task 4.20: the one-shot denial-edge capacity ladder. Descending, abandoned at the first success, so at most one rung
/// is ever resident and a failed rung allocates nothing. Internal heap only, for the reason above.
HeapCapacityLadder probeHeapCapacityLadder(uint32_t needBytes) {
  HeapCapacityLadder out;
  out.tailFreeBytes = getInternalHeapFreeBytes();
  out.needBytes = needBytes;
  // Rungs computed, not tabulated: a `constexpr` array of five would land in this script's DTCM `.rodata`
  // (`variables` +32 B here, which the RAM1 budget gate rightly refuses). 16/24/32/48/64 KB = 16 KB + delta.
  const auto rungBytes = [](int index) -> uint32_t {
    const uint32_t deltaKb = index == 0 ? 0u : (index == 1 ? 8u : (index == 2 ? 16u : (index == 3 ? 32u : 48u)));
    return (16u + deltaKb) * 1024u;
  };
  for (int rung = 4; rung >= 0; --rung) {
    void* block = std::malloc(rungBytes(rung));
    if (block == nullptr) {
      continue;
    }
    std::free(block);
    out.servedMask = static_cast<uint8_t>(
        out.servedMask | (1u << static_cast<unsigned>(rung)));
    out.largestAllocatableBytes = rungBytes(rung);
    // Every smaller rung is implied by the largest one; stop so only one block is ever taken.
    for (int smaller = 0; smaller < rung; ++smaller) {
      out.servedMask = static_cast<uint8_t>(
          out.servedMask | (1u << static_cast<unsigned>(smaller)));
    }
    break;
  }
  return out;
}

uint32_t getInternalHeapUsedBytes() {
  uint32_t total = getInternalHeapTotalBytes();
  uint32_t free = getInternalHeapFreeBytes();
  return total > free ? total - free : 0;
}

bool isExternalMemoryPoolAvailable() {
  return external_psram_size > 0 && extmem_smalloc_pool.pool_size > 0 &&
         extmem_smalloc_pool.pool != nullptr;
}

uint32_t getExternalMemoryPoolTotalBytes() {
  return static_cast<uint32_t>(extmem_smalloc_pool.pool_size);
}

uint32_t getExternalMemoryPoolFreeBytes() {
  size_t freeBytes = 0;
  getPsramStats(nullptr, &freeBytes);
  return static_cast<uint32_t>(freeBytes);
}

uint32_t getExternalMemoryPoolUsedBytes() {
  size_t usedBytes = 0;
  getPsramStats(&usedBytes, nullptr);
  return static_cast<uint32_t>(usedBytes);
}

bool isLowMemory(uint32_t thresholdBytes) {
  return getInternalHeapFreeBytes() < thresholdBytes;
}

void logStatus(bool includeExternalPoolUsage) {
  const uint32_t freeK = getInternalHeapFreeBytes() / 1024;
  const uint32_t totalK = getInternalHeapTotalBytes() / 1024;
  const uint32_t usedK = getInternalHeapUsedBytes() / 1024;
  const uint32_t minEverK = getInternalHeapMinEverFreeBytes() / 1024;
  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] heap free=%lu used=%lu total=%lu KB min_ever=%lu KB",
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK,
             (unsigned long)minEverK);
  if (!isExternalMemoryPoolAvailable()) {
    logger.log(CAT_GENERAL, LOG_INFO, "[Memory] psram unavailable");
  } else if (includeExternalPoolUsage) {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] psram chip=%u MB free=%lu used=%lu pool=%lu KB",
               (unsigned)external_psram_size,
               (unsigned long)(getExternalMemoryPoolFreeBytes() / 1024),
               (unsigned long)(getExternalMemoryPoolUsedBytes() / 1024),
               (unsigned long)(getExternalMemoryPoolTotalBytes() / 1024));
  } else {
    // pool_size only — free/used walk the header chain and must not run once the
    // transport or an external clock can be live (141815: 593 ms, MIDI clock lost).
    logger.log(CAT_GENERAL, LOG_INFO, "[Memory] psram chip=%u MB pool=%lu KB",
               (unsigned)external_psram_size,
               (unsigned long)(getExternalMemoryPoolTotalBytes() / 1024));
  }
  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}


// --- Operation admission gate -------------------------------------------------
// Constants are deliberately conservative and documented, because the whole point
// is to refuse *before* the allocator is asked: 2026-09-17 showed the device already
// at 99.5% pool with 4 KB heap free, denying a 32 KB request 841 times, before a
// single 5120-byte request on an unguarded path reached
// `ExternalMemoryFirstAllocator::abort()`.
namespace {
/// Internal-heap floor an admitted operation must leave untouched, before margin.
constexpr uint32_t kOperationHeapFloorBytes = 32u * 1024u;
/// How recently the pool must have refused a request to count as "full right now".
constexpr uint32_t kPoolRefusalWindowMs = 2000u;
/// Repeat-denial report window. The gate has one per-turn caller -- the snapshot publication
/// boundary asks on every construction turn and can stay parked for as long as the pool is full --
/// so the same denial shape is reported at most once per window. Only the diagnostic is bounded;
/// the decision is exact on every call, which is what a parked caller acts on. `recordPoolRefusal`
/// bounds its own storm the same way.
constexpr uint32_t kDenialReportWindowMs = 1000u;

/// Per-operation headroom the operation must leave untouched beyond the floor.
/// Overdub start gets the largest because 2026-09-17 showed it still requesting
/// 5120 bytes about two seconds after admission, on top of 32 KB chunk retries;
/// record start is already refusal-tolerant and only needs a floor. Zero at the
/// call site means "use the default for this kind".
FLASHMEM uint32_t defaultMarginFor(OperationKind operation) {
  switch (operation) {
    case OperationKind::RecordStart:
      return 8u * 1024u;
    case OperationKind::OverdubStart:
      return 16u * 1024u;
    case OperationKind::SnapshotReserve:
      return 8u * 1024u;
    case OperationKind::BootProbe:
      // Deliberately mirrors OverdubStart so the boot probe and a post-record
      // overdub denial are an apples-to-apples pair: same requirement, different
      // availability.
      return 16u * 1024u;
  }
  return 8u * 1024u;
}

namespace {
extern "C" {
void* extmem_malloc(size_t size);
void extmem_free(void* pointer);
}

/// Take the exact block and give it straight back. Called immediately before the real
/// reserve, so its verdict is predictive; uses the raw entry points, never the aborting
/// C++ allocators, so a failed probe is a nullptr rather than process death.
///
/// The tier asked first is the tier the real reserve's allocator tries first (`reserveProbeAsksExternalPoolFirst`):
/// a PSRAM-first reserve is proven against the pool and never touches the internal arena unless the pool refuses
/// (which is exactly when the real allocation would fall back to it). `context` tags any arena movement the probe
/// causes in the capture's per-`_sbrk` rows.
FLASHMEM bool probeBlockAvailable(size_t bytes, bool externalPoolFirst, uint8_t context) {
  if (bytes == 0) {
    return true;
  }
  HeapPeakTrace::setContext(context);
  bool available = false;
  if (externalPoolFirst) {
    void* block = extmem_malloc(bytes);
    if (block != nullptr) {
      extmem_free(block);
      available = true;
    }
  }
  if (!available) {
    void* block = std::malloc(bytes);
    if (block != nullptr) {
      std::free(block);
      available = true;
    }
  }
  if (!available && !externalPoolFirst) {
    void* block = extmem_malloc(bytes);
    if (block != nullptr) {
      extmem_free(block);
      available = true;
    }
  }
  HeapPeakTrace::setContext(0);
  return available;
}
}  // namespace

FLASHMEM const char* operationName(OperationKind operation) {
  // DEC-115: kept out of the switch below on purpose -- extending that switch would grow its jump
  // table in `.rodata` (DTCM in this script), which the RAM1 guard books as `variables`. A literal
  // wrapped in `PSTR` lands in `.progmem` (FLASH), and this function is already FLASH-resident.
  if (operation == OperationKind::DisplayProjectionReserve) {
    return PSTR("display_projection");
  }
  if (operation == OperationKind::SourceViewReserve) {
    // Same reason as the branch above: a literal in `PSTR` lands in `.progmem` instead of growing
    // this function's `.rodata` jump table.
    return PSTR("source_view_reserve");
  }
  switch (operation) {
    case OperationKind::RecordStart:
      return PSTR("record_start");
    case OperationKind::OverdubStart:
      return PSTR("overdub_start");
    case OperationKind::SnapshotReserve:
      return PSTR("snapshot_reserve");
    case OperationKind::BootProbe:
      return PSTR("boot_probe");
    case OperationKind::CompanionHistoryReserve:
      return PSTR("companion_history_reserve");
    case OperationKind::SlotSwitch:
      return PSTR("slot_switch");
  }
  return PSTR("unknown");
}
}  // namespace

FLASHMEM FLASHMEM void reportDeclaredCapDenied(OperationKind operation, uint8_t roleClassIndex,
                                              uint8_t holderSlot) {
  // `holder` names the slot the census found holding the class, or 255 (`kNoWorkingSetClassHolder`) when
  // it found none. The holder is what makes the row diagnosable: a slot whose capture has already stopped
  // still counts, so a reader can tell "the envelope was really exceeded" from "the outgoing finalizer was
  // counted as a second live capture" without guessing.
  Serial.printf_P(PSTR("#MEM_DENIED,%lu,op=%s,reason=cap_reached,class=%u,holder=%u,need=0B,"
                       "heap_free=%luKB,pool_refused=%lu/%luB\n"),
                  static_cast<unsigned long>(micros()), operationName(operation),
                  static_cast<unsigned>(roleClassIndex), static_cast<unsigned>(holderSlot),
                  static_cast<unsigned long>(getInternalHeapFreeBytes() / 1024),
                  static_cast<unsigned long>(ExternalPoolAccounting::counters().poolRefusedCount),
                  static_cast<unsigned long>(
                      ExternalPoolAccounting::counters().poolRefusedLargestBytes));
}

FLASHMEM void reportPrewarmAdmissionDenied(OperationKind operation) {
  const ExternalPoolAccounting::Counters& pool = ExternalPoolAccounting::counters();
  Serial.printf_P(
      PSTR("#MEM_DENIED,%lu,op=%s,reason=prewarm_failed,need=0B,heap_free=%luKB,"
           "pool_refused=%lu/%luB\n"),
      static_cast<unsigned long>(micros()), operationName(operation),
      static_cast<unsigned long>(getInternalHeapFreeBytes() / 1024),
      static_cast<unsigned long>(pool.poolRefusedCount),
      static_cast<unsigned long>(pool.poolRefusedLargestBytes));
}

FLASHMEM bool admitOperationMemory(OperationKind operation, uint32_t marginBytes) {
  const uint32_t heapFree = getInternalHeapFreeBytes();
  const ExternalPoolAccounting::Counters& pool = ExternalPoolAccounting::counters();
  const uint32_t nowMs = millis();
  const uint32_t sinceRefusalMs =
      pool.poolLastRefusalMs == 0 ? UINT32_MAX : (nowMs - pool.poolLastRefusalMs);
  const uint32_t requiredBytes =
      marginBytes != 0 ? marginBytes : defaultMarginFor(operation);
  // Point-of-use reserves are the one case where a probe is predictive, so they are the
  // only kind that uses one. Everything else keeps the heap-floor / pool-refusal checks
  // that were verified on the overdub boundary.
  const bool pointOfUseReserve =
      (operation == OperationKind::CompanionHistoryReserve ||
       operation == OperationKind::DisplayProjectionReserve ||
       operation == OperationKind::SourceViewReserve) && requiredBytes != 0;
  // Task 4.20: the floor and the margin are unchanged — what changed is the *question* asked about them, and only for
  // **user-gesture admissions**. The arena tail (`&_heap_end - __brkval`) is a cheap pre-filter, never a verdict: it is
  // a **lower bound** on the largest allocatable block, so "tail >= need" proves the operation fits, while "tail < need"
  // proves nothing at all — freed blocks below `__brkval` are reusable and the number never recovers because `_sbrk` is
  // a pure bump. Device evidence (2026-10-27, `7b4ebc25…`): at a 24,576 B tail a 16 KB internal `malloc` succeeded
  // (`HEAP_PROBE,tail=24576,want=16384,malloc=1,pool=0`) while this predicate refused the same start with
  // `need=16384B,heap_free=24KB`.
  //
  // **Scope is deliberate and load-bearing:** only `OverdubStart`/`RecordStart`/`SlotSwitch` may consult the allocator.
  // They are raised by a human press, so at most a few probes per second, and only when the tail already said no.
  // `SnapshotReserve` must **never** probe: it gates every publication turn on the idle lane, so a probe there is a
  // malloc/free storm that raises `__brkval` on each turn — measured on device 2026-10-27 as a hang/reset on image
  // `2776cae3…`. That is why this is scoped rather than applied to every operation. The point-of-use reserves keep
  // their own predictive probe below.
  const uint32_t admissionNeedBytes = kOperationHeapFloorBytes + requiredBytes;
  const bool tailLooksLow = !pointOfUseReserve && heapFree < admissionNeedBytes;
  // Task 4.20 (DEC-169): the tail is a cheap pre-filter, never a verdict. A user-gesture admission whose tail says
  // "low" is resolved by the **read-only** free-list walk (`largestInternalFreeChunkBytes`), not by the tail: the
  // withdrawn malloc-probe (`2776cae3…`) was withdrawn because a successful probe raises `__brkval` permanently and
  // starves the tail-based gates, but the walk allocates nothing, so it answers the same `floor + margin` question
  // without consuming the resource it is judging. Device `!HEAP_STATS` (2026-10-27): tail 36 KB while the largest free
  // chunk was 79.4 KB — the tail alone is a false denial. The walk is bounded (≤ 4096 free chunks) and gated to the
  // rare gesture press, so it never becomes the per-turn `SnapshotReserve` storm the withdrawn probe was measured to
  // cause. `SnapshotReserve` and the point-of-use reserves keep their existing paths untouched.
  const bool userGestureAdmission =
      operation == OperationKind::OverdubStart || operation == OperationKind::RecordStart ||
      operation == OperationKind::SlotSwitch;
  // Report-only: the walk's answer, kept so the denial row can name the largest free chunk the verdict was made on
  // (0 = the walk did not run, or was incomplete). Same call, same condition, same order as before.
  uint32_t largestFreeBytes = 0;
  if (tailLooksLow && userGestureAdmission) largestFreeBytes = largestInternalFreeChunkBytes();
  const bool heapLow = tailLooksLow && !(userGestureAdmission && largestFreeBytes >= admissionNeedBytes);
  const bool poolRefusing = sinceRefusalMs <= kPoolRefusalWindowMs;
  const bool reserveFailed =
      pointOfUseReserve && !probeBlockAvailable(requiredBytes, reserveProbeAsksExternalPoolFirst(operation),
                                                static_cast<uint8_t>(operation));
  const bool reportAdmitted = operation == OperationKind::BootProbe;
  if (!heapLow && !poolRefusing && !reserveFailed) {
    if (reportAdmitted) {
      Serial.printf_P(
          PSTR("#MEM_ADMIT,%lu,op=%s,need=%luB,heap_free=%luKB,pool_refused=%lu/%luB,"
               "since_refusal=%lums\n"),
          static_cast<unsigned long>(micros()), operationName(operation),
          static_cast<unsigned long>(requiredBytes), static_cast<unsigned long>(heapFree / 1024),
          static_cast<unsigned long>(pool.poolRefusedCount),
          static_cast<unsigned long>(pool.poolRefusedLargestBytes),
          static_cast<unsigned long>(sinceRefusalMs));
    }
    return true;
  }
  // Bounded report, exact decision: see `kDenialReportWindowMs`.
  const uint8_t reasonCode = reserveFailed ? 2u : (heapLow ? 1u : 0u);
  static uint32_t lastReportMs = 0;
  static uint32_t lastReportBytes = 0;
  static uint8_t lastReportReason = 0;
  if (requiredBytes == lastReportBytes && reasonCode == lastReportReason &&
      (nowMs - lastReportMs) < kDenialReportWindowMs) {
    return false;
  }
  lastReportMs = nowMs;
  lastReportBytes = requiredBytes;
  lastReportReason = reasonCode;
  Serial.printf_P(
      PSTR("#MEM_DENIED,%lu,op=%s,reason=%s,need=%luB,heap_free=%luKB,"
           "pool_refused=%lu/%luB,since_refusal=%lums,largest_free=%luKB\n"),
      static_cast<unsigned long>(micros()), operationName(operation),
      reserveFailed ? PSTR("reserve_failed")
                    : (heapLow ? PSTR("heap_low") : PSTR("pool_refusing")),
      static_cast<unsigned long>(requiredBytes), static_cast<unsigned long>(heapFree / 1024),
      static_cast<unsigned long>(pool.poolRefusedCount),
      static_cast<unsigned long>(pool.poolRefusedLargestBytes),
      static_cast<unsigned long>(sinceRefusalMs),
      static_cast<unsigned long>(largestFreeBytes / 1024));
#if defined(SESSION_CAPTURE)
  // Task 4.20: **no automatic ladder here.** An earlier revision ran the capacity ladder at this edge, and it was
  // withdrawn with the allocator-authoritative policy above for the same reason: at a low tail the ladder's own rungs
  // (up to 64 KB) are taken from the arena being judged, so the diagnostic perturbed the failure it was measuring.
  // The ladder is now on-demand only, behind `!HEAP_PROBE`, which is stopped-only and host-requested.
  (void)admissionNeedBytes;
#endif
  return false;
}

FLASHMEM void logExternalPoolAccounting(const char* phase) {
  // One-shot positive control: on a healthy boot the gate must report ADMIT here, so
  // the denial lines after a record are demonstrably a threshold crossing and not a
  // gate that simply always refuses.
  static bool bootProbeDone = false;
  if (!bootProbeDone) {
    bootProbeDone = true;
    (void)admitOperationMemory(OperationKind::BootProbe, 0);
  }
  const ExternalPoolAccounting::Counters& c = ExternalPoolAccounting::counters();
  // `snap=` is the *cumulative* snapshot-allocator counter, so a rising value is not by itself
  // evidence of retention. The arena's live view is `slab_live`/`slab_unused` plus the pinning
  // counts: a live slab holding <=1 node (`pin1`) has all of its remaining payload
  // unreachable, which is what distinguishes "a generation retained its nodes" from "a
  // generation stranded a mostly-empty slab". O(live slabs), and only on the once-per-second
  // tick line.
  const SnapshotSlabCensus slabCensus = snapshotSlabCensus();
  // Comma-separated and timestamped so a run can be reduced to a per-interval delta
  // table and correlated with the control-event log. `cls` is live KB per size class
  // (<=128B, <=1K, <=8K, <=32K, <=128K, >128K): a jump concentrated in one class names
  // the kind of buffer that grew, which a single total cannot.
  Serial.printf_P(
      PSTR("[Memory] ext,%lu,phase=%s,live=%luKB,high=%luKB,blocks=%lu/%lu,alloc=%luKB,"
           "freed=%luKB,untracked=%lu,unknown=%lu,refused=%lu/%luB,"
           "cls=%lu/%lu/%lu/%lu/%lu/%luKB,clsn=%lu/%lu/%lu/%lu/%lu/%lu,"
           "tags=%lu/%lu/%lu/%lu/%lu/%lu/%luKB,chunks_free=%lu,snap=%luKB,"
           "slab_live=%lu,slab_used=%luKB,slab_unused=%luKB,pin1=%lu,pin24=%lu,"
           "heap_free=%luKB,heap_min=%luKB,accounting=2\n"),
      static_cast<unsigned long>(micros()), phase != nullptr ? phase : "-",
      static_cast<unsigned long>(c.liveBytes / 1024),
      static_cast<unsigned long>(c.highWaterLiveBytes / 1024),
      static_cast<unsigned long>(c.liveBlocks),
      static_cast<unsigned long>(c.highWaterLiveBlocks),
      static_cast<unsigned long>(c.cumulativeAllocatedBytes / 1024),
      static_cast<unsigned long>(c.cumulativeFreedBytes / 1024),
      static_cast<unsigned long>(c.untrackedBlocks),
      static_cast<unsigned long>(c.unknownFrees),
      static_cast<unsigned long>(c.poolRefusedCount),
      static_cast<unsigned long>(c.poolRefusedLargestBytes),
      static_cast<unsigned long>(c.liveBytesByClass[0] / 1024),
      static_cast<unsigned long>(c.liveBytesByClass[1] / 1024),
      static_cast<unsigned long>(c.liveBytesByClass[2] / 1024),
      static_cast<unsigned long>(c.liveBytesByClass[3] / 1024),
      static_cast<unsigned long>(c.liveBytesByClass[4] / 1024),
      static_cast<unsigned long>(c.liveBytesByClass[5] / 1024),
      static_cast<unsigned long>(c.liveBlocksByClass[0]),
      static_cast<unsigned long>(c.liveBlocksByClass[1]),
      static_cast<unsigned long>(c.liveBlocksByClass[2]),
      static_cast<unsigned long>(c.liveBlocksByClass[3]),
      static_cast<unsigned long>(c.liveBlocksByClass[4]),
      static_cast<unsigned long>(c.liveBlocksByClass[5]),
      static_cast<unsigned long>(c.tagBytes[1] / 1024),
      static_cast<unsigned long>(c.tagBytes[2] / 1024),
      static_cast<unsigned long>(c.tagBytes[3] / 1024),
      static_cast<unsigned long>(c.tagBytes[4] / 1024),
      static_cast<unsigned long>(c.tagBytes[5] / 1024),
      static_cast<unsigned long>(c.tagBytes[6] / 1024),
      static_cast<unsigned long>(c.tagBytes[7] / 1024),
      // Existing O(1) owner counters, named so a class of growth can be attributed
      // without per-site instrumentation: LoopEventStore chunks are 256 x 28 B
      // (the <=8 KB class) and snapshot slabs are the large <=128 KB class.
      static_cast<unsigned long>(LoopEventStore::freeChunkCount()),
      static_cast<unsigned long>(snapshotAllocByteCount() / 1024),
      static_cast<unsigned long>(slabCensus.liveSlabs),
      static_cast<unsigned long>(slabCensus.usedBytes / 1024),
      static_cast<unsigned long>(slabCensus.unusedBytes / 1024),
      static_cast<unsigned long>(slabCensus.slabsAtMostOneNode),
      static_cast<unsigned long>(slabCensus.slabsTwoToFourNodes),
      static_cast<unsigned long>(getInternalHeapFreeBytes() / 1024),
      static_cast<unsigned long>(getInternalHeapMinEverFreeBytes() / 1024));
}

FLASHMEM void logWorkingSetOwners(const char* phase) {
  // S1.1's owner list is wider than the tick's tag window. `logExternalPoolAccounting`
  // prints `tagBytes[1..7]` only, so companion reconstruction/apply/bridge-map/passes,
  // canonical pass inputs, the reset pass timeline and prepare-first-bar (tags 8..16) were
  // reachable **only** through `#MEM_EXT_REFUSED` — that is, only once the pool had already
  // refused, which is precisely the failure a working-set budget exists to prevent. Same
  // vector, emitted deliberately. `formatTagVector` covers tags 1..17.
  const ExternalPoolAccounting::Counters& c = ExternalPoolAccounting::counters();
  uint32_t tagTotalBytes = 0;
  for (size_t tag = 1; tag < ExternalPoolAccounting::Counters::kTagCount; ++tag) {
    tagTotalBytes += c.tagBytes[tag];
  }
  char tagVector[200];
  ExternalPoolAccounting::formatTagVector(tagVector, sizeof(tagVector));
  const SnapshotSlabCensus slabCensus = snapshotSlabCensus();
  Serial.printf_P(
      PSTR("#MEM_OWNERS,%lu,phase=%s,tag_total=%luKB,tags=%s,"
           "slab_live=%lu,slab_used=%luKB,slab_unused=%luKB,slabs_at_most_one=%lu,"
           "slabs_two_to_four=%lu,snap=%luKB,chunks_free=%lu,live=%luKB,blocks=%lu,"
           "untracked=%lu,unknown=%lu,heap_free=%luKB,heap_min=%luKB\n"),
      static_cast<unsigned long>(micros()), phase != nullptr ? phase : "-",
      static_cast<unsigned long>(tagTotalBytes / 1024), tagVector,
      static_cast<unsigned long>(slabCensus.liveSlabs),
      static_cast<unsigned long>(slabCensus.usedBytes / 1024),
      static_cast<unsigned long>(slabCensus.unusedBytes / 1024),
      static_cast<unsigned long>(slabCensus.slabsAtMostOneNode),
      static_cast<unsigned long>(slabCensus.slabsTwoToFourNodes),
      static_cast<unsigned long>(snapshotAllocByteCount() / 1024),
      static_cast<unsigned long>(LoopEventStore::freeChunkCount()),
      static_cast<unsigned long>(c.liveBytes / 1024),
      static_cast<unsigned long>(c.liveBlocks),
      static_cast<unsigned long>(c.untrackedBlocks),
      static_cast<unsigned long>(c.unknownFrees),
      static_cast<unsigned long>(getInternalHeapFreeBytes() / 1024),
      static_cast<unsigned long>(getInternalHeapMinEverFreeBytes() / 1024));
}

void logStatusAtAddedNotes(uint32_t addedNoteOns, size_t loopEventCount,
                           const void* loopEventsData, size_t loopChunkRefCount,
                           bool loopChunkBacked) {
  const uint32_t freeK = getInternalHeapFreeBytes() / 1024;
  const uint32_t totalK = getInternalHeapTotalBytes() / 1024;
  const uint32_t usedK = getInternalHeapUsedBytes() / 1024;

  const auto poolStats = MemoryPool::globalMidiEventPool.getStats();
  const char* loopStorage = "empty";
  if (loopChunkBacked && loopChunkRefCount > 0) {
    loopStorage = "chunk_refs";
  } else if (loopEventCount > 0 && loopEventsData != nullptr) {
    loopStorage = isInExternalMemoryPool(loopEventsData) ? "psram" : "heap";
  }

  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] notes=%lu heap free=%lu used=%lu total=%lu KB",
             (unsigned long)addedNoteOns,
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK);

  // NOTE: do NOT call getExternalMemoryPoolFreeBytes()/getExternalMemoryPoolUsedBytes() here.
  // They walk the
  // entire PSRAM smalloc pool header chain (sm_malloc_stats_pool), which on an 8 MB
  // pool stalls the loop for hundreds of ms. This runs on the record/overdub note-on
  // hot path, so only O(1) fields are reported. Full free/used PSRAM stats are logged
  // off the hot path in logStatus() (boot/setup). pool=KB below is pool_size (O(1)).
  if (isExternalMemoryPoolAvailable()) {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] notes=%lu psram chip=%u MB pool=%lu KB "
               "midi_pool=%zu/%zu loop_events=%zu loop_chunks=%zu loop_buf=%s",
               (unsigned long)addedNoteOns,
               (unsigned)external_psram_size,
               (unsigned long)(getExternalMemoryPoolTotalBytes() / 1024),
               poolStats.first, poolStats.second,
               loopEventCount, loopChunkRefCount, loopStorage);
  } else {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] notes=%lu psram unavailable midi_pool=%zu/%zu "
               "loop_events=%zu loop_chunks=%zu loop_buf=%s",
               (unsigned long)addedNoteOns,
               poolStats.first, poolStats.second,
               loopEventCount, loopChunkRefCount, loopStorage);
  }

  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}

#define PRESSURE_MEM FLASHMEM

namespace {

MemoryPressureLevel sAdvisoryLevel = MemoryPressureLevel::Normal;
uint32_t sLowExitStableSinceMs = 0;
bool sCaptureAppendFailedLatch = false;
uint32_t sCaptureAppendFailedLatchSinceMs = 0;

MemoryPressurePolicy::Inputs gatherPressureInputs(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs{};
  inputs.heapFreeBytes = MemoryMonitor::getInternalHeapFreeBytes();
  inputs.chunksFree = LoopEventStore::freeChunkCount();
  inputs.persistQueueDepth = PersistenceQueue::queueDepth();
  (void)nowMs;
  if (sCaptureAppendFailedLatch) {
    inputs.captureAppendFailedLatch = true;
  }
  return inputs;
}

void maybeClearCaptureAppendFailedLatch(const MemoryPressurePolicy::Inputs& inputs,
                                        uint32_t nowMs) {
  if (!sCaptureAppendFailedLatch) {
    return;
  }
  const bool headroomRecovered =
      MemoryPressurePolicy::chunkHeadroomOk(inputs.chunksFree) &&
      inputs.heapFreeBytes >= Config::HEAP_PRESSURE_CRITICAL_EXIT_BYTES;
  const bool latchTimedOut =
      sCaptureAppendFailedLatchSinceMs != 0 &&
      nowMs - sCaptureAppendFailedLatchSinceMs >= Config::HEAP_PRESSURE_CAPTURE_APPEND_LATCH_MS;
  if (headroomRecovered || latchTimedOut) {
    sCaptureAppendFailedLatch = false;
    sCaptureAppendFailedLatchSinceMs = 0;
  }
}

PRESSURE_MEM void emitPressureTransition(MemoryPressureLevel from, MemoryPressureLevel to,
                                         const MemoryPressurePolicy::Inputs& inputs) {
  char transition[32];
  MemoryPressurePolicy::formatTransitionLabel(from, to, transition, sizeof(transition));
  SC_MEMORY_PRESSURE(transition, inputs.heapFreeBytes, inputs.chunksFree,
                     inputs.persistQueueDepth);
}

}  // namespace

PRESSURE_MEM MemoryPressureLevel getAdvisoryPressureLevel() { return sAdvisoryLevel; }

PRESSURE_MEM void notifyCaptureAppendFailed(uint32_t nowMs) {
  const bool wasLatched = sCaptureAppendFailedLatch;
  sCaptureAppendFailedLatch = true;
  sCaptureAppendFailedLatchSinceMs = nowMs;
#if defined(SESSION_CAPTURE)
  if (!wasLatched) {
    MemoryPressurePolicy::Inputs inputs = gatherPressureInputs(nowMs);
    SC_MEMORY_PRESSURE("latch,Critical", inputs.heapFreeBytes, inputs.chunksFree,
                       inputs.persistQueueDepth);
  }
#endif
}

PRESSURE_MEM void updateAdvisoryPressureLevel(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs = gatherPressureInputs(nowMs);
  maybeClearCaptureAppendFailedLatch(inputs, nowMs);
  inputs = gatherPressureInputs(nowMs);

  const MemoryPressureLevel previous = sAdvisoryLevel;
  sAdvisoryLevel =
      MemoryPressurePolicy::stepLevel(previous, inputs, nowMs, sLowExitStableSinceMs);

  if (sAdvisoryLevel != previous) {
    emitPressureTransition(previous, sAdvisoryLevel, inputs);
  }
}

#undef PRESSURE_MEM

}  // namespace MemoryMonitor

#elif defined(PIO_UNIT_TEST_NATIVE)

#include "Globals.h"
#include "Utils/MemoryPressurePolicy.h"

namespace MemoryMonitor {

uint32_t g_nativeTestFreeHeap = UINT32_MAX;
bool g_nativeTestHeapOverride = false;

void setNativeTestFreeHeap(uint32_t bytes) {
  g_nativeTestFreeHeap = bytes;
  g_nativeTestHeapOverride = true;
}

void resetNativeTestFreeHeap() {
  g_nativeTestHeapOverride = false;
  g_nativeTestFreeHeap = UINT32_MAX;
}
namespace {
// Task 4.20: the *modeled* largest allocatable block. Defaults to 0, meaning "capacity == the injected arena tail",
// which keeps every pre-existing fixture's tail-based meaning exactly as it was. A fixture raises it to model free-list
// holes below `__brkval` — the asymmetry the admission RED exists to pin.
uint32_t g_nativeTestAllocatableCapacity = 0;
}  // namespace

void setNativeTestAllocatableCapacity(uint32_t bytes) { g_nativeTestAllocatableCapacity = bytes; }

void resetNativeTestAllocatableCapacity() { g_nativeTestAllocatableCapacity = 0; }

uint32_t nativeAllocatableCapacityBytes() {
  return g_nativeTestAllocatableCapacity != 0 ? g_nativeTestAllocatableCapacity
                                              : getInternalHeapFreeBytes();
}

/// Task 4.20 native authority: the modeled largest free chunk (defaults to the injected tail, so a fixture that does
/// not model free-list holes keeps the old tail-based meaning).
uint32_t largestInternalFreeChunkBytes() { return nativeAllocatableCapacityBytes(); }

uint32_t getInternalHeapFreeBytes() {
  return g_nativeTestHeapOverride ? g_nativeTestFreeHeap : UINT32_MAX;
}
uint32_t getInternalHeapTotalBytes() { return UINT32_MAX; }
uint32_t getInternalHeapUsedBytes() { return 0; }
bool isExternalMemoryPoolAvailable() { return false; }
uint32_t getExternalMemoryPoolTotalBytes() { return 0; }

/// Task 4.20 native branch: the tail is whatever the fixture injected, and the allocatability answer is the host
/// allocator's — which is exactly the asymmetry under test, so a fixture can pin "tail below the admission sum while
/// the request is servable" without a device. `std::malloc` on the host keeps that honest: it is a real allocation,
/// not a simulated verdict.
HeapAllocatabilityProbe probeInternalHeapAllocatability(uint32_t bytes) {
  HeapAllocatabilityProbe out;
  out.tailFreeBytes = getInternalHeapFreeBytes();
  out.requestedBytes = bytes;
  if (bytes == 0) {
    return out;
  }
  void* block = std::malloc(bytes);
  if (block != nullptr) {
    std::free(block);
    out.internalMallocOk = true;
  }
  return out;
}
uint32_t getExternalMemoryPoolFreeBytes() { return 0; }
uint32_t getExternalMemoryPoolUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
uint32_t getInternalHeapMinEverFreeBytes() { return getInternalHeapFreeBytes(); }
void resetInternalHeapWatermark() {}
void logStatus(bool) {}
void logExternalPoolAccounting(const char*) {}
void logWorkingSetOwners(const char*) {}
void reportPrewarmAdmissionDenied(OperationKind) {}
namespace {
// S4.4: the declared-cap report is a device `printf`, so a host fixture cannot read the row. Recording
// what the owner asked to report is what makes the policy path host-assertable.
NativeCapDenial sNativeCapDenials{};
}  // namespace
void reportDeclaredCapDenied(OperationKind, uint8_t roleClassIndex, uint8_t holderSlot) {
  ++sNativeCapDenials.count;
  sNativeCapDenials.lastRoleClass = roleClassIndex;
  sNativeCapDenials.lastHolderSlot = holderSlot;
}
NativeCapDenial nativeTestCapDenials() { return sNativeCapDenials; }
void resetNativeTestCapDenials() { sNativeCapDenials = NativeCapDenial{}; }
// Native has no external pool. Exercise the snapshot's real internal threshold
// when heap headroom is injected; other operation stubs retain their prior behavior.
namespace {
bool sNativeForcedReserveDenied = false;
}
/// Task 4.20 native authority: the modeled largest block, not the modeled tail. Keeping them separable is what lets a
/// fixture express the device's real asymmetry — "tail small, block still servable".
bool allocatorCanServeInternalBlock(uint32_t bytes) {
  if (bytes == 0) return true;
  return nativeAllocatableCapacityBytes() >= bytes;
}

/// Task 4.20 native ladder: rungs are answered from the modeled capacity, so the descending/stop-at-first-success
/// semantics are pinned on the host without needing a real failing malloc.
HeapCapacityLadder probeHeapCapacityLadder(uint32_t needBytes) {
  HeapCapacityLadder out;
  out.tailFreeBytes = getInternalHeapFreeBytes();
  out.needBytes = needBytes;
  // Same computed rungs as the device branch (a table would be DTCM `.rodata` in the firmware script; kept identical
  // here so the host pins the device's semantics, not a variant).
  const auto rungBytes = [](int index) -> uint32_t {
    const uint32_t deltaKb = index == 0 ? 0u : (index == 1 ? 8u : (index == 2 ? 16u : (index == 3 ? 32u : 48u)));
    return (16u + deltaKb) * 1024u;
  };
  const uint32_t capacity = nativeAllocatableCapacityBytes();
  for (int rung = 4; rung >= 0; --rung) {
    if (rungBytes(rung) > capacity) continue;
    out.largestAllocatableBytes = rungBytes(rung);
    for (int served = 0; served <= rung; ++served) {
      out.servedMask = static_cast<uint8_t>(out.servedMask | (1u << static_cast<unsigned>(served)));
    }
    break;
  }
  return out;
}

bool admitOperationMemory(OperationKind operation, uint32_t marginBytes) {
  if (sNativeForcedReserveDenied) return false;
  // Task 4.20 (DEC-169): the native mirror matches the device — floor + margin, resolved against the modeled largest
  // free chunk (`nativeAllocatableCapacityBytes`, defaulting to the injected tail) for user-gesture admissions, and
  // against the tail for everything else. `SnapshotReserve` keeps the tail rule (it gates every publication turn).
  const uint32_t required =
      marginBytes != 0 ? marginBytes
                       : (operation == OperationKind::SnapshotReserve ? 8u * 1024u : 16u * 1024u);
  const uint32_t need = 32u * 1024u + required;
  const bool tailLooksLow = getInternalHeapFreeBytes() < need;
  const bool userGestureAdmission =
      operation == OperationKind::OverdubStart || operation == OperationKind::RecordStart ||
      operation == OperationKind::SlotSwitch;
  const bool heapLow =
      tailLooksLow && !(userGestureAdmission && nativeAllocatableCapacityBytes() >= need);
  return !heapLow;
}
void setNativeForcedReserveDenied(bool denied) { sNativeForcedReserveDenied = denied; }
void logStatusAtAddedNotes(uint32_t, size_t, const void*, size_t, bool) {}

namespace {

MemoryPressureLevel sAdvisoryLevel = MemoryPressureLevel::Normal;
uint32_t sLowExitStableSinceMs = 0;
bool sCaptureAppendFailedLatch = false;
uint32_t sCaptureAppendFailedLatchSinceMs = 0;
uint16_t g_nativeTestChunksFree = 512;
uint16_t g_nativeTestPersistQueueDepth = 0;
bool g_nativeTestPersistQueueOverride = false;

MemoryPressurePolicy::Inputs gatherPressureInputs(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs{};
  inputs.heapFreeBytes = getInternalHeapFreeBytes();
  inputs.chunksFree = g_nativeTestChunksFree;
  inputs.persistQueueDepth =
      g_nativeTestPersistQueueOverride ? g_nativeTestPersistQueueDepth : 0;
  (void)nowMs;
  if (sCaptureAppendFailedLatch) {
    inputs.captureAppendFailedLatch = true;
  }
  return inputs;
}

void maybeClearCaptureAppendFailedLatch(const MemoryPressurePolicy::Inputs& inputs,
                                        uint32_t nowMs) {
  if (!sCaptureAppendFailedLatch) {
    return;
  }
  const bool headroomRecovered =
      MemoryPressurePolicy::chunkHeadroomOk(inputs.chunksFree) &&
      inputs.heapFreeBytes >= Config::HEAP_PRESSURE_CRITICAL_EXIT_BYTES;
  const bool latchTimedOut =
      sCaptureAppendFailedLatchSinceMs != 0 &&
      nowMs - sCaptureAppendFailedLatchSinceMs >= Config::HEAP_PRESSURE_CAPTURE_APPEND_LATCH_MS;
  if (headroomRecovered || latchTimedOut) {
    sCaptureAppendFailedLatch = false;
    sCaptureAppendFailedLatchSinceMs = 0;
  }
}

}  // namespace

MemoryPressureLevel getAdvisoryPressureLevel() { return sAdvisoryLevel; }

void notifyCaptureAppendFailed(uint32_t nowMs) {
  sCaptureAppendFailedLatch = true;
  sCaptureAppendFailedLatchSinceMs = nowMs;
}

void updateAdvisoryPressureLevel(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs = gatherPressureInputs(nowMs);
  maybeClearCaptureAppendFailedLatch(inputs, nowMs);
  inputs = gatherPressureInputs(nowMs);

  const MemoryPressureLevel previous = sAdvisoryLevel;
  sAdvisoryLevel =
      MemoryPressurePolicy::stepLevel(previous, inputs, nowMs, sLowExitStableSinceMs);
}

void setNativeTestChunksFree(uint16_t chunksFree) { g_nativeTestChunksFree = chunksFree; }

void setNativeTestPersistQueueDepth(uint16_t depth) {
  g_nativeTestPersistQueueDepth = depth;
  g_nativeTestPersistQueueOverride = true;
}

void resetNativeTestPressureInputs() {
  g_nativeTestChunksFree = 512;
  g_nativeTestPersistQueueDepth = 0;
  g_nativeTestPersistQueueOverride = false;
  sAdvisoryLevel = MemoryPressureLevel::Normal;
  sLowExitStableSinceMs = 0;
  sCaptureAppendFailedLatch = false;
  sCaptureAppendFailedLatchSinceMs = 0;
}

}  // namespace MemoryMonitor

#else

namespace MemoryMonitor {

uint32_t getInternalHeapFreeBytes() { return 0; }
uint32_t getInternalHeapTotalBytes() { return 0; }
uint32_t getInternalHeapUsedBytes() { return 0; }
bool isExternalMemoryPoolAvailable() { return false; }
uint32_t getExternalMemoryPoolTotalBytes() { return 0; }
uint32_t getExternalMemoryPoolFreeBytes() { return 0; }
uint32_t getExternalMemoryPoolUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
uint32_t getInternalHeapMinEverFreeBytes() { return 0; }
void resetInternalHeapWatermark() {}
void logStatus(bool) {}
void logExternalPoolAccounting(const char*) {}
void logWorkingSetOwners(const char*) {}
void reportPrewarmAdmissionDenied(OperationKind) {}
void reportDeclaredCapDenied(OperationKind, uint8_t, uint8_t) {}
// Native/other-platform stub: the gate has no pool to consult and must not
// change host-test behaviour, so it always admits.
bool admitOperationMemory(OperationKind, uint32_t) { return true; }
void logStatusAtAddedNotes(uint32_t, size_t, const void*, size_t, bool) {}

}  // namespace MemoryMonitor

#endif

#if defined(__IMXRT1062__)
// ---------------------------------------------------------------------------------------------
// DEC-115 (pool containment): the display/note projection family's point-of-use admission.
//
// Device-side (out-of-line) definition: one flash-resident copy of the probe, which the RAM1
// measurement prefers over inlining it at every projection call site. The native builds take the
// header-inline definition instead (`include/Utils/MemoryMonitor.h`), so a test suite that compiles a
// projection translation unit needs no extra wiring; the two forms share the gate in
// `admitOperationMemory`, which is where the policy lives.
// ---------------------------------------------------------------------------------------------
namespace MemoryMonitor {
namespace {
/// Monotonic: incremented exactly when a projection reserve is refused. Mutable telemetry, so it sits
/// in RAM2 (`DMAMEM`) rather than DTCM -- the RAM1 budget books DTCM as `variables`.
DMAMEM volatile uint32_t sDisplayProjectionRefusals = 0;
}  // namespace

FLASHMEM bool admitDisplayProjectionReserve(size_t bytes) {
  if (bytes == 0) {
    return true;
  }
  // The point-of-use rule (DEC-065 amendment): the probe tests exactly the pair the allocator will use,
  // and it is taken immediately before the reserve, so its verdict is predictive. `bytes` is the exact
  // request -- an estimate here would refuse work that could have been served.
  if (admitOperationMemory(OperationKind::DisplayProjectionReserve,
                           static_cast<uint32_t>(bytes))) {
    return true;
  }
  ++sDisplayProjectionRefusals;
  return false;
}

FLASHMEM uint32_t displayProjectionRefusalCount() { return sDisplayProjectionRefusals; }

}  // namespace MemoryMonitor
#endif
