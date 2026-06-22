//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MemoryMonitor.h
 * @brief Runtime memory monitoring for embedded systems (Teensy 4.1).
 *
 * Reports internal malloc heap and (on Teensy 4.1) PSRAM pool usage.
 * Integrates with PerformanceMonitor and can be logged periodically.
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace MemoryMonitor {

/**
 * @brief Get free heap in bytes (malloc/new pool).
 *
 * On Teensy 4.1: Uses the sbrk-managed heap (RAM used by malloc/new).
 * Returns 0 if the platform has no supported heap reporting.
 */
uint32_t getFreeHeap();

/**
 * @brief Get total heap size in bytes (configured pool).
 * Returns 0 if unknown.
 */
uint32_t getTotalHeap();

/**
 * @brief Get used heap in bytes (total - free).
 */
uint32_t getUsedHeap();

/** @brief True when Teensy PSRAM extmem pool is configured (chip detected). */
bool isPsramAvailable();

/** @brief Total PSRAM pool size in bytes (0 when unavailable). */
uint32_t getPsramTotalBytes();

/** @brief Free bytes in the PSRAM extmem pool (0 when unavailable). */
uint32_t getPsramFreeBytes();

/** @brief Used bytes in the PSRAM extmem pool (0 when unavailable). */
uint32_t getPsramUsedBytes();

/**
 * @brief Check if free heap is below a threshold (e.g. 10 KB).
 */
bool isLowMemory(uint32_t thresholdBytes = 10 * 1024);

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

#if defined(PIO_UNIT_TEST_NATIVE)
void setNativeTestFreeHeap(uint32_t bytes);
void resetNativeTestFreeHeap();
#endif

}  // namespace MemoryMonitor
