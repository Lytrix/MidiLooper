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

  bool isStale(uint32_t loopRevision, uint32_t trackGeneration) const {
    return cachedLoopRevision != loopRevision || cachedTrackGeneration != trackGeneration;
  }

  void syncRevision(uint32_t loopRevision, uint32_t trackGeneration) {
    cachedLoopRevision = loopRevision;
    cachedTrackGeneration = trackGeneration;
  }

  void reset(bool preserveLedger) {
    primaryWindow.clear();
    if (!preserveLedger) {
      cachedLoopRevision = 0;
      cachedTrackGeneration = 0;
      ledger.clear();
    }
  }
};

class TrackPlaybackRuntime {
 public:
  void resetAll(bool preserveLedger) {
    for (auto& rt : runtimeBySlot_) {
      if (!rt) {
        continue;
      }
      rt->reset(preserveLedger);
    }
  }

  LoopPlaybackRuntime& slot(uint8_t slotIndex) {
    std::unique_ptr<LoopPlaybackRuntime>& rt = runtimeBySlot_[slotIndex];
    if (!rt) {
      rt = std::unique_ptr<LoopPlaybackRuntime>(new LoopPlaybackRuntime());
    }
    return *rt;
  }

  const LoopPlaybackRuntime& slot(uint8_t slotIndex) const {
    static const LoopPlaybackRuntime kEmpty{};
    const std::unique_ptr<LoopPlaybackRuntime>& rt = runtimeBySlot_[slotIndex];
    if (!rt) {
      return kEmpty;
    }
    return *rt;
  }

 private:
  std::array<std::unique_ptr<LoopPlaybackRuntime>, Config::MAX_LOOPS_PER_TRACK> runtimeBySlot_{};
};
