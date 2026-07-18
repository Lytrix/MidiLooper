//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackPlaybackRuntime.h"

#include <cstdlib>
#include <new>

#include "Utils/InternalHeapFirstAllocator.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define TRACK_PLAYBACK_RUNTIME_COLD_MEM FLASHMEM
#else
#define TRACK_PLAYBACK_RUNTIME_COLD_MEM
#endif

bool LoopPlaybackRuntime::isStale(uint32_t loopRevision, uint32_t trackGeneration) const {
  return cachedLoopRevision != loopRevision || cachedTrackGeneration != trackGeneration;
}

void LoopPlaybackRuntime::syncRevision(uint32_t loopRevision, uint32_t trackGeneration) {
  cachedLoopRevision = loopRevision;
  cachedTrackGeneration = trackGeneration;
}

void LoopPlaybackRuntime::reset(bool preserveLedger) {
  primaryWindow.clear();
  if (!preserveLedger) {
    cachedLoopRevision = 0;
    cachedTrackGeneration = 0;
    ledger.clear();
  }
}

void LoopPlaybackRuntimeDeleter::operator()(LoopPlaybackRuntime* runtime) const noexcept {
  if (runtime == nullptr) {
    return;
  }
  runtime->~LoopPlaybackRuntime();
  if (isInExternalMemoryPool(runtime)) {
    extmem_free(runtime);
  } else {
    std::free(runtime);
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime* allocateLoopPlaybackRuntime() {
  void* mem = extmem_malloc(sizeof(LoopPlaybackRuntime));
  if (mem == nullptr) {
    mem = std::malloc(sizeof(LoopPlaybackRuntime));
  }
  if (mem == nullptr) {
    return nullptr;
  }
  return new (mem) LoopPlaybackRuntime();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::resetAll(bool preserveLedger) {
  for (auto& rt : runtimeBySlot_) {
    if (!rt) {
      continue;
    }
    rt->reset(preserveLedger);
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM void TrackPlaybackRuntime::clearAllLedgers() {
  for (auto& rt : runtimeBySlot_) {
    if (!rt) {
      continue;
    }
    rt->ledger.clear();
  }
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime* TrackPlaybackRuntime::trySlot(
    uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return nullptr;
  }
  LoopPlaybackRuntimePtr& rt = runtimeBySlot_[slotIndex];
  if (!rt) {
    rt.reset(allocateLoopPlaybackRuntime());
  }
  return rt.get();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime* TrackPlaybackRuntime::slotIfAllocated(
    uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return nullptr;
  }
  return runtimeBySlot_[slotIndex].get();
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const LoopPlaybackRuntime* TrackPlaybackRuntime::slotIfAllocated(
    uint8_t slotIndex) const {
  return const_cast<TrackPlaybackRuntime*>(this)->slotIfAllocated(slotIndex);
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM LoopPlaybackRuntime& TrackPlaybackRuntime::slot(uint8_t slotIndex) {
  // Hot path: runtime must already exist (boot/prewarm/trySlot). Deferred/cold paths
  // must use trySlot — never embed a static LoopPlaybackRuntime fallback (ActiveNoteLedger
  // ≈24KB would overflow RAM1; session_20260718_235713 null-deref was alloc failure).
  LoopPlaybackRuntime* runtime = trySlot(slotIndex);
  return *runtime;
}

TRACK_PLAYBACK_RUNTIME_COLD_MEM const LoopPlaybackRuntime& TrackPlaybackRuntime::slot(
    uint8_t slotIndex) const {
  return const_cast<TrackPlaybackRuntime*>(this)->slot(slotIndex);
}
