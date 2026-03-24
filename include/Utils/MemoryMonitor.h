//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MemoryMonitor.h
 * @brief Runtime memory monitoring for embedded systems (Teensy 4.1).
 *
 * Reports free heap so you can detect when approaching memory limits.
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

/**
 * @brief Check if free heap is below a threshold (e.g. 10 KB).
 */
bool isLowMemory(uint32_t thresholdBytes = 10 * 1024);

/**
 * @brief Log current memory stats to Serial (uses Logger if available).
 */
void logStatus();

}  // namespace MemoryMonitor
