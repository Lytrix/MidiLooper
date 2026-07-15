#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "ActiveNoteLedger.h"
#include "Globals.h"
#include "PlaybackWindow.h"

struct LoopPlaybackRuntime {
  uint32_t cachedLoopRevision = 0;
  uint32_t cachedTrackGeneration = 0;
  ActiveNoteLedger ledger;
  PlaybackWindow primaryWindow;

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

  LoopPlaybackRuntime& slot(uint8_t slotIndex);

  const LoopPlaybackRuntime& slot(uint8_t slotIndex) const;

 private:
  std::array<LoopPlaybackRuntimePtr, Config::MAX_LOOPS_PER_TRACK> runtimeBySlot_{};
};
