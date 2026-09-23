//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MemoryMonitor.h"
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

#include <smalloc.h>

// Linker symbols from imxrt1062_t41.ld
extern "C" {
  extern char *__brkval;
  extern unsigned long _heap_start;
  extern unsigned long _heap_end;
  extern uint8_t external_psram_size;  // MB detected at startup (Teensy core)
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

uint32_t getInternalHeapFreeBytes() {
  int32_t freeBytes = reinterpret_cast<char*>(&_heap_end) - __brkval;
  const uint32_t free = freeBytes > 0 ? static_cast<uint32_t>(freeBytes) : 0;
  updateInternalHeapWatermark(free);
  return free;
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
FLASHMEM bool probeBlockAvailable(size_t bytes) {
  if (bytes == 0) {
    return true;
  }
  void* block = std::malloc(bytes);
  if (block != nullptr) {
    std::free(block);
    return true;
  }
  block = extmem_malloc(bytes);
  if (block != nullptr) {
    extmem_free(block);
    return true;
  }
  return false;
}
}  // namespace

FLASHMEM const char* operationName(OperationKind operation) {
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
  }
  return PSTR("unknown");
}
}  // namespace

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
      operation == OperationKind::CompanionHistoryReserve && requiredBytes != 0;
  // For that kind the probe is the *authority*, not an extra veto: its caller will hand
  // the same byte count to an allocator that falls back from malloc to extmem, so a
  // request the pool can serve is not a heap depletion. 2026-09-17 device evidence
  // (`stepE-staging-admission-01`): the history reserve was denied `heap_low` with
  // `pool_refused=0/0B` and ~1.27 MB of pool free, which made an available operation fail.
  const bool heapLow =
      !pointOfUseReserve && heapFree < (kOperationHeapFloorBytes + requiredBytes);
  const bool poolRefusing = sinceRefusalMs <= kPoolRefusalWindowMs;
  const bool reserveFailed = pointOfUseReserve && !probeBlockAvailable(requiredBytes);
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
           "pool_refused=%lu/%luB,since_refusal=%lums\n"),
      static_cast<unsigned long>(micros()), operationName(operation),
      reserveFailed ? PSTR("reserve_failed")
                    : (heapLow ? PSTR("heap_low") : PSTR("pool_refusing")),
      static_cast<unsigned long>(requiredBytes), static_cast<unsigned long>(heapFree / 1024),
      static_cast<unsigned long>(pool.poolRefusedCount),
      static_cast<unsigned long>(pool.poolRefusedLargestBytes),
      static_cast<unsigned long>(sinceRefusalMs));
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
           "heap_free=%luKB,heap_min=%luKB\n"),
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

uint32_t getInternalHeapFreeBytes() {
  return g_nativeTestHeapOverride ? g_nativeTestFreeHeap : UINT32_MAX;
}
uint32_t getInternalHeapTotalBytes() { return UINT32_MAX; }
uint32_t getInternalHeapUsedBytes() { return 0; }
bool isExternalMemoryPoolAvailable() { return false; }
uint32_t getExternalMemoryPoolTotalBytes() { return 0; }
uint32_t getExternalMemoryPoolFreeBytes() { return 0; }
uint32_t getExternalMemoryPoolUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
uint32_t getInternalHeapMinEverFreeBytes() { return getInternalHeapFreeBytes(); }
void resetInternalHeapWatermark() {}
void logStatus(bool) {}
void logExternalPoolAccounting(const char*) {}
void logWorkingSetOwners(const char*) {}
void reportPrewarmAdmissionDenied(OperationKind) {}
// Native/other-platform stub: the gate has no pool to consult and must not
// change host-test behaviour, so it always admits.
namespace {
bool sNativeForcedReserveDenied = false;
}
bool admitOperationMemory(OperationKind, uint32_t) { return !sNativeForcedReserveDenied; }
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
// Native/other-platform stub: the gate has no pool to consult and must not
// change host-test behaviour, so it always admits.
bool admitOperationMemory(OperationKind, uint32_t) { return true; }
void logStatusAtAddedNotes(uint32_t, size_t, const void*, size_t, bool) {}

}  // namespace MemoryMonitor

#endif
