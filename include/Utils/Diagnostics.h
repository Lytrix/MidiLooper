//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file Diagnostics.h
 * @brief Lightweight diagnostics facade — binary trace via DebugSessionCapture PSRAM ring.
 *
 * Diagnostics explain firmware behaviour; they never create firmware behaviour.
 * Active when SESSION_CAPTURE is enabled. DIAG_LEVEL gates verbosity (see below).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/DiagnosticsTypes.h"

#ifndef DIAG_LEVEL
#if defined(SESSION_CAPTURE)
#define DIAG_LEVEL 2
#else
#define DIAG_LEVEL 0
#endif
#endif

// 0=OFF, 1=ERROR, 2=TRACE, 3=FULL
namespace Diagnostics {

enum class Counter : uint8_t {
  PlaybackMergeRebuild = 0,
  VisualCacheRebuild,
  Materialize,
  CacheInvalidateBroad,
  CacheInvalidateScoped,
  AllocatorFailure,
  SessionUndoPush,
  Count,
};

#if defined(SESSION_CAPTURE)

void init();
void emitBootCheckpoint();
void flushTraceRecords(size_t maxRecords = 32);

void recordEvent(uint16_t eventId, uint32_t payload = 0, bool includeHeapSnapshot = false);
void recordMemorySnapshot(uint16_t eventId, uint32_t payload = 0);
void incrementCounter(Counter counter, uint32_t delta = 1);
uint32_t readCounter(Counter counter);

#else

inline void init() {}
inline void emitBootCheckpoint() {}
inline void flushTraceRecords(size_t = 32) {}
inline void recordEvent(uint16_t, uint32_t = 0, bool = false) {}
inline void recordMemorySnapshot(uint16_t, uint32_t = 0) {}
inline void incrementCounter(Counter, uint32_t = 0) {}
inline uint32_t readCounter(Counter) { return 0; }

#endif

}  // namespace Diagnostics

#if defined(SESSION_CAPTURE) && DIAG_LEVEL >= 2
#define DIAG_EVENT(eventId) Diagnostics::recordEvent(static_cast<uint16_t>(eventId))
#define DIAG_EVENT_PAYLOAD(eventId, payload) \
  Diagnostics::recordEvent(static_cast<uint16_t>(eventId), static_cast<uint32_t>(payload))
#define DIAG_COUNTER_INC(counter) Diagnostics::incrementCounter(Diagnostics::Counter::counter)
#elif defined(SESSION_CAPTURE) && DIAG_LEVEL >= 1
#define DIAG_EVENT(eventId) ((void)0)
#define DIAG_EVENT_PAYLOAD(eventId, payload) ((void)0)
#define DIAG_COUNTER_INC(counter) ((void)0)
#else
#define DIAG_EVENT(eventId) ((void)0)
#define DIAG_EVENT_PAYLOAD(eventId, payload) ((void)0)
#define DIAG_COUNTER_INC(counter) ((void)0)
#endif

#if defined(SESSION_CAPTURE) && DIAG_LEVEL >= 2
#define DIAG_MEMORY(eventId) Diagnostics::recordMemorySnapshot(static_cast<uint16_t>(eventId))
#else
#define DIAG_MEMORY(eventId) ((void)0)
#endif
