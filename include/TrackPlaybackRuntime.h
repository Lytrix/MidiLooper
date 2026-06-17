#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "ActiveNoteLedger.h"
#include "Globals.h"
#include "PlaybackCursor.h"
#include "PlaybackWindow.h"

struct LoopPlaybackRuntime {
  PlaybackCursor cursor;
  ActiveNoteLedger ledger;
  PlaybackWindow primaryWindow;
  PlaybackWindow loopHeadWindow;

  void reset(bool preserveLedger) {
    cursor.reset(preserveLedger);
    primaryWindow.clear();
    loopHeadWindow.clear();
    if (!preserveLedger) {
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
