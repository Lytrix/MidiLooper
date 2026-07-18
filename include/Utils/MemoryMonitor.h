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
 */
void logStatus();

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
#endif

}  // namespace MemoryMonitor
