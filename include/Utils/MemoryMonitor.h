//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MemoryMonitor.h
 * @brief Runtime memory monitoring for embedded systems (Teensy 4.1).
 *
 * Reports internal heap and external memory pool usage.
 * Integrates with PerformanceMonitor and can be logged periodically.
 */
#pragma once

#include <cstdint>
#include <cstddef>

#include "Utils/MemoryPressureLevel.h"

namespace MemoryMonitor {

/**
 * @brief Get free heap in bytes (malloc/new pool).
 *
 * On Teensy 4.1: Uses the sbrk-managed heap (RAM used by malloc/new).
 * Returns 0 if the platform has no supported heap reporting.
 */
uint32_t getInternalHeapFreeBytes();

/**
 * @brief Get total heap size in bytes (configured pool).
 * Returns 0 if unknown.
 */
uint32_t getInternalHeapTotalBytes();

/**
 * @brief Get used heap in bytes (total - free).
 */
uint32_t getInternalHeapUsedBytes();

/** @brief True when Teensy PSRAM extmem pool is configured (chip detected). */
bool isExternalMemoryPoolAvailable();

/** @brief Total PSRAM pool size in bytes (0 when unavailable). */
uint32_t getExternalMemoryPoolTotalBytes();

/** @brief Free bytes in the PSRAM extmem pool (0 when unavailable). */
uint32_t getExternalMemoryPoolFreeBytes();

/** @brief Used bytes in the PSRAM extmem pool (0 when unavailable). */
uint32_t getExternalMemoryPoolUsedBytes();

/**
 * @brief Check if free heap is below a threshold (e.g. 10 KB).
 */
bool isLowMemory(uint32_t thresholdBytes = 10 * 1024);

/** Lowest internal-heap free bytes observed since boot (updated on each free-bytes read). */
uint32_t getInternalHeapMinEverFreeBytes();

/** Reset min-ever-free tracking (call once after setup baseline). */
void resetInternalHeapWatermark();

/**
 * @brief Log current memory stats to Serial (uses Logger if available).
 * @param includeExternalPoolUsage Report external-pool free/used bytes. This walks the whole
 *        smalloc header chain (`sm_malloc_stats_pool`) and measured 593 ms on an 8 MiB pool
 *        in session_20260812_141815, long enough to lose external MIDI clock. Only pass true
 *        from setup, before the transport can run.
 */
void logStatus(bool includeExternalPoolUsage = false);

/**
 * @brief Log O(1) external-pool accounting plus the internal-heap counters.
 *
 * Reads bookkeeping only — it never touches the pool header chain, so unlike
 * `logStatus(true)` it is safe to call from the transport thread while PLAYING or
 * OVERDUBBING. The numbers come from `ExternalPoolAccounting`, fed by linker
 * wrappers around `extmem_malloc`/`extmem_free`. Read the *change* between samples,
 * not the absolute value, and check `untracked`/`unknown` are zero before trusting
 * `live`.
 *
 * @param phase Short tag for the call site (for example "tick").
 */
void logExternalPoolAccounting(const char* phase);

/**
 * @brief Log the full per-owner working-set census (runtime-working-set-ownership S1.1).
 *
 * `logExternalPoolAccounting` prints only `tagBytes[1..7]`, so the owners S1.1 names —
 * companion reconstruction / apply / bridge-map / passes, canonical pass inputs, the
 * reset pass timeline and prepare-first-bar (tags 8..16) — are invisible on the 1 Hz
 * tick. The full vector exists in `ExternalPoolAccounting::formatTagVector`, whose only
 * caller was the pool-*refusal* path, which meant the complete per-owner census could be
 * read only at a refusal. This emits the same vector deliberately, at a slower cadence
 * than the tick, together with the snapshot-slab census that carries the per-loop derived
 * bytes.
 *
 * Read it as "net bytes attributed to each owner right now". It is O(1) bookkeeping, so
 * it is safe from the transport thread; it is compiled only under `SESSION_CAPTURE`.
 *
 * Not covered, deliberately: whole-loop `VisualCache` bytes (no byte accessor exists) and
 * the lease holder/owning-slot/role triple (no playback-window lease exists until S2).
 *
 * @param phase Short tag for the call site (for example "owners").
 */
void logWorkingSetOwners(const char* phase);

/**
 * @brief Log heap, PSRAM, pool, and active-loop storage stats at a record/overdub milestone.
 * @param addedNoteOns Total note-ons stored this record/overdub pass.
 * @param loopEventCount Events in the active loop vector (all types).
 * @param loopEventsData Pointer to loop.midiEvents storage, or nullptr when empty.
 * @param loopChunkRefCount Active chunk refs for chunk-backed storage metadata.
 * @param loopChunkBacked True when loop events are represented by chunk refs.
 */
void logStatusAtAddedNotes(uint32_t addedNoteOns, size_t loopEventCount = 0,
                          const void* loopEventsData = nullptr,
                          size_t loopChunkRefCount = 0,
                          bool loopChunkBacked = false);

/**
 * @brief Operation kinds that require pool/heap headroom at their admission boundary.
 *
 * Passed as a number, not a string: the gate is called from transport code and this
 * tree has essentially no ITCM or DTCM-rodata headroom, so the call site must not
 * carry a literal. Names are resolved in the .cpp.
 */
enum class OperationKind : uint8_t {
  RecordStart = 1,
  OverdubStart = 2,
  SnapshotReserve = 3,
  /// Diagnostic only: report the decision whether it admits or denies, so a healthy
  /// boot shows the gate admitting rather than staying silent. Never gates anything.
  BootProbe = 4,
  /// A reserve made at its point of use, where a probe taken immediately beforehand is
  /// predictive: nothing allocates between the probe and the real reserve. This is the
  /// kind `Loop::prewarmPreparedCompanionHistory` uses, so an exhausted device returns
  /// the `false` its contract already allows instead of reaching `abort()`.
  CompanionHistoryReserve = 5,
};

/**
 * @brief Admission gate for an operation that needs memory headroom.
 *
 * Call this *before* the operation mutates any state, so a denial is a clean
 * refusal rather than a rollback of a half-started interval. It is safe from the
 * transport thread: it reads the O(1) heap counters and the O(1) external-pool
 * refusal record, and never walks the pool.
 *
 * Denies when the internal heap is below its floor plus `marginBytes`, or when the
 * pool has refused an allocation within its recency window — an exact "the pool is
 * full right now" signal, which matters because the accounting's live-byte figure
 * over-reports and cannot be used for headroom.
 *
 * The point-of-use reserve (`CompanionHistoryReserve`) is the exception: its caller
 * probes for the exact block immediately before handing it to an allocator that
 * falls back from malloc to extmem, so the probe is the authority and the heap-floor
 * proxy must not veto a request the pool can serve. Denying such a request would make
 * an available operation fail. See DEC-065.
 *
 * On denial it emits `#MEM_DENIED` with the operation, the reason, the requested
 * margin, heap free, and the pool refusal count/size/age.
 *
 * @return true when the operation may proceed.
 */
bool admitOperationMemory(OperationKind operation, uint32_t marginBytes);

/**
 * @brief Report that an operation was denied because a required prewarm failed.
 *
 * Used where admission is decided by the prewarm's own result rather than by a
 * headroom gate, so the denial is still explicit and attributable. Emits the same
 * `#MEM_DENIED` line with `reason=prewarm_failed`.
 */
void reportPrewarmAdmissionDenied(OperationKind operation);

/**
 * @brief Report that an activation meets a **declared** working-set envelope cap.
 *
 * S4.4: a class held beyond its declared cardinality is a policy outcome, not an allocation failure, so it
 * is reported with its own reason (`cap_reached`) and the class index that is full. Reporting it separately
 * keeps "we declined to exceed the envelope" from being read as "the pool ran out".
 *
 * `holderSlot` is the slot that already holds that class, or `kNoWorkingSetClassHolder` (0xFF) when none
 * does. The holder is what makes the signal usable: the census reads raw role bits, so a slot whose capture
 * has already stopped still counts as a holder, and the row must name it rather than leave it to be guessed.
 *
 * The row is `#MEM_DENIED,<us>,op=<op>,reason=cap_reached,class=<bit index>,holder=<slot0|255>,need=0B,…`.
 */
void reportDeclaredCapDenied(OperationKind operation, uint8_t roleClassIndex, uint8_t holderSlot);

/**
 * @brief Advisory pressure signal (Phase 1A — compute + telemetry only).
 * Subsystems consult; owners decide safe actions. Updated once per main-loop turn.
 */
MemoryPressureLevel getAdvisoryPressureLevel();

/** Sample inputs, apply hysteresis, emit DIAG on transition. No reclaim side effects. */
void updateAdvisoryPressureLevel(uint32_t nowMs);

/** Latch Critical until chunk headroom recovers or latch timeout (capture append failure). */
void notifyCaptureAppendFailed(uint32_t nowMs);

#if defined(PIO_UNIT_TEST_NATIVE)
void setNativeTestFreeHeap(uint32_t bytes);
void resetNativeTestFreeHeap();
void setNativeTestChunksFree(uint16_t chunksFree);
void setNativeTestPersistQueueDepth(uint16_t depth);
void resetNativeTestPressureInputs();
/// Force `admitOperationMemory` to deny, so a test can assert that a failed required
/// prewarm refuses admission rather than being ignored.
void setNativeForcedReserveDenied(bool denied);
/// The declared-cap report is a device `printf`, so a host fixture cannot read the row. This records what
/// the owner asked to report, which is what makes S4.4's policy path host-assertable for the first time:
/// the count of reports, the class bit last named and the holder last named (`kNoWorkingSetClassHolder`
/// when none). Nothing consults it outside `PIO_UNIT_TEST_NATIVE`.
struct NativeCapDenial {
  uint16_t count = 0;
  uint8_t lastRoleClass = 0;
  uint8_t lastHolderSlot = 0;
};
NativeCapDenial nativeTestCapDenials();
void resetNativeTestCapDenials();
#endif

}  // namespace MemoryMonitor
