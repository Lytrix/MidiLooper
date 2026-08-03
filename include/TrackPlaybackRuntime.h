#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "ActiveNoteLedger.h"
#include "Globals.h"
#include "PlaybackMergedMidiEvents.h"

struct LoopPlaybackRuntime {
  uint32_t cachedLoopRevision = 0;
  uint32_t cachedTrackGeneration = 0;
  ActiveNoteLedger ledger;
  PlaybackMergedMidiEvents mergedMidiEvents;

  bool isStale(uint32_t loopRevision, uint32_t trackGeneration) const;

  void syncRevision(uint32_t loopRevision, uint32_t trackGeneration);

  void reset(bool preserveLedger);
};

struct LoopPlaybackRuntimeDeleter {
  void operator()(LoopPlaybackRuntime* runtime) const noexcept;
};

using LoopPlaybackRuntimePtr = std::unique_ptr<LoopPlaybackRuntime, LoopPlaybackRuntimeDeleter>;

LoopPlaybackRuntime* allocateLoopPlaybackRuntime();

class TrackPlaybackRuntime {
 public:
  void resetAll(bool preserveLedger);

  void clearAllLedgers();

  LoopPlaybackRuntime& slot(uint8_t slotIndex);

  const LoopPlaybackRuntime& slot(uint8_t slotIndex) const;

  /// Allocate-or-get without null dereference. nullptr if allocation fails.
  LoopPlaybackRuntime* trySlot(uint8_t slotIndex);

  /// Non-allocating peek. nullptr if this slot has no runtime yet.
  LoopPlaybackRuntime* slotIfAllocated(uint8_t slotIndex);
  const LoopPlaybackRuntime* slotIfAllocated(uint8_t slotIndex) const;

 private:
  std::array<LoopPlaybackRuntimePtr, Config::MAX_LOOPS_PER_TRACK> runtimeBySlot_{};
};
