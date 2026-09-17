//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file ExternalPoolAccounting.h
 * @brief O(1) accounting for external-pool allocations. Never walks the pool.
 *
 * The PSRAM pool cannot be queried from the transport thread: the free/used walk
 * (`sm_malloc_stats_pool`) measured 593 ms on an 8 MiB pool in
 * session_20260812_141815 and lost external MIDI clock, which is why
 * `MemoryMonitor::logStatus(true)` is restricted to `setup()`. An 8-bar record plus
 * overdub then exhausted the pool with no trend visible at all (2026-09-17).
 *
 * So the pool is accounted by bookkeeping instead of probing. The counters are fed
 * from linker wrappers around `extmem_malloc`/`extmem_free`
 * (`-Wl,--wrap=...`, enabled by `EXTERNAL_POOL_WRAP`), which costs **no** call-site
 * code — this tree has only ~700 bytes of ITCM headroom, and adding a recording call
 * to every allocator instantiation overflowed RAM1 by ~4 KB.
 *
 * How live bytes are derived: `extmem_free` takes no size, so the wrapper keeps a
 * bounded shadow table of pointer -> bytes, filled by `recordPoolAllocation`. Entries
 * that cannot be shadowed are counted in `untrackedBlocks` and are excluded from
 * `liveBytes`; a free with no entry counts in `unknownFrees`. Both must stay at zero
 * for `liveBytes` to be trusted — they are reported for exactly that reason.
 */
namespace ExternalPoolAccounting {

struct Counters {
  /// Bytes live in counted pool blocks (shadowed allocations only).
  uint32_t liveBytes = 0;
  /// Highest `liveBytes` seen since the last reset.
  uint32_t highWaterLiveBytes = 0;
  /// Live counted pool blocks.
  uint32_t liveBlocks = 0;
  /// Highest `liveBlocks` seen since the last reset.
  uint32_t highWaterLiveBlocks = 0;
  /// Bytes handed out by the pool over the whole run (monotonic).
  uint32_t cumulativeAllocatedBytes = 0;
  /// Bytes returned to the pool over the whole run (monotonic).
  uint32_t cumulativeFreedBytes = 0;
  /// Allocations that got a shadow entry.
  uint32_t trackedBlocks = 0;
  /// Allocations that could not get a shadow entry (table full). Excluded from live.
  uint32_t untrackedBlocks = 0;
  /// Frees with no shadow entry (never counted as live).
  uint32_t unknownFrees = 0;
  /// Pool requests the pool refused (the caller then fell back to the heap).
  /// This is the exhaustion signal: it goes non-zero before `abort()` does.
  uint32_t poolRefusedCount = 0;
  /// Largest single pool request that was refused, in bytes.
  uint32_t poolRefusedLargestBytes = 0;
  /// `millis()` of the most recent refusal, or 0 when none. Lets an admission gate
  /// ask "is the pool refusing right now?" without walking it — an exact signal,
  /// unlike the live-byte counter, which over-reports.
  uint32_t poolLastRefusalMs = 0;

  /// Number of size classes used by `liveBytesByClass` / `liveBlocksByClass`.
  static constexpr size_t kSizeClassCount = 6;
  /// Live bytes per size class, derived by re-reading the shadowed size when a block
  /// is freed. This is what attributes growth: "+1.26 MB in 8-32 KB blocks" and
  /// "+1.26 MB in hundreds of small blocks" call for completely different fixes.
  /// Classes: <=128 B, <=1 KB, <=8 KB, <=32 KB, <=128 KB, >128 KB.
  uint32_t liveBytesByClass[kSizeClassCount] = {};
  uint32_t liveBlocksByClass[kSizeClassCount] = {};

  /// Size classes plus bracket tags for attribution.
  static constexpr size_t kTagCount = 8;
  /// Net bytes attributed to each bracket tag (see `beginTag`). Exact: the tag is
  /// stored per shadow entry, so a free decrements the tag that allocated it
  /// whatever scope is active at free time. Slot 0 is untagged.
  uint32_t tagBytes[kTagCount] = {};
  uint32_t tagBlocks[kTagCount] = {};
};

/// Bracket tags. A scope attributes every allocation made inside it, including
/// nested calls, which is what identifies a prewarm step's buffers without
/// instrumenting each allocation site.
enum Tag : uint8_t {
  TagNone = 0,
  TagCaptureStopJobs = 1,
  TagViewPrewarm = 2,
  TagCaptureStaging = 3,
  TagCanonicalOutput = 4,
  TagTransferHorizon = 5,
  TagCompanionHistory = 6,
  TagPlaybackOrder = 7,
};

/// Size-class index for `bytes` (0..kSizeClassCount-1).
constexpr size_t sizeClassFor(size_t bytes) {
  if (bytes <= 128) return 0;
  if (bytes <= 1024) return 1;
  if (bytes <= 8 * 1024) return 2;
  if (bytes <= 32 * 1024) return 3;
  if (bytes <= 128 * 1024) return 4;
  return 5;
}

/// Read-only view of the counters. O(1).
const Counters& counters();

/// Shadow one pool allocation and add it to the live totals.
void recordPoolAllocation(void* pointer, size_t bytes);

/// Retire one pool allocation, subtracting its shadowed size from the live totals.
void recordPoolFree(void* pointer);

/// Record that the pool refused a request of `bytes`. Emits a bounded diagnostic so
/// the refused size and the denial context survive on serial; never allocates.
void recordPoolRefusal(size_t bytes);

/// Zero the counters and the shadow table. Cold path only.
void reset();

#if defined(__IMXRT1062__)
/// Enter/leave an attribution bracket. Nesting is supported to depth 4; the tag is
/// recorded on each allocation made while it is current. Device-only by design, so
/// a native build inlines the no-ops below and never needs the definitions.
void beginTag(uint8_t tag);
void endTag();
#else
inline void beginTag(uint8_t) {}
inline void endTag() {}
#endif

}  // namespace ExternalPoolAccounting
