//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <cstddef>

namespace HotPathTelemetry {

#if defined(PERF_TELEMETRY)

struct Metric {
  uint32_t samples = 0;
  uint32_t overBudget = 0;
  uint32_t maxUs = 0;
  uint64_t totalUs = 0;
};

struct SnapshotStats {
  uint32_t samples = 0;
  uint32_t maxSourceEvents = 0;
  uint32_t maxCopiedEvents = 0;
  uint32_t droppedEvents = 0;
};

void reset();
void recordOverdubStart(uint32_t elapsedUs, uint32_t sourceEvents, uint32_t undoDepth);
void recordOverdubSessionOpen(uint32_t elapsedUs, uint32_t sourceEvents);
void recordUndoSnapshot(uint32_t elapsedUs, uint32_t sourceEvents, uint32_t copiedEvents);
void recordSaveState(uint32_t elapsedUs, bool ok);
void recordDisplayUpdate(uint32_t elapsedUs);
void emitSummary(const char* checkpoint);
void requestDeferredSummary(const char* checkpoint);
void processDeferredSummary();

class ScopedSaveState {
public:
  ScopedSaveState();
  ~ScopedSaveState();
  void setOk(bool ok);

private:
  uint32_t startUs;
  bool ok_;
};

#else

inline void reset() {}
inline void recordOverdubStart(uint32_t, uint32_t, uint32_t) {}
inline void recordOverdubSessionOpen(uint32_t, uint32_t) {}
inline void recordUndoSnapshot(uint32_t, uint32_t, uint32_t) {}
inline void recordSaveState(uint32_t, bool) {}
inline void recordDisplayUpdate(uint32_t) {}
inline void emitSummary(const char*) {}
inline void requestDeferredSummary(const char*) {}
inline void processDeferredSummary() {}

class ScopedSaveState {
public:
  ScopedSaveState() {}
  ~ScopedSaveState() {}
  void setOk(bool) {}
};

#endif

}  // namespace HotPathTelemetry

