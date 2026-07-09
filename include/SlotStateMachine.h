#pragma once

#include <Arduino.h>
#include "Globals.h"

class Track; // Forward declaration

// Central sentinel for "no slot".
namespace SlotIndex {
  constexpr uint8_t None = Config::INVALID_LOOP_SLOT;
}

// When a slot transition should occur.
enum class SlotQuantization : uint8_t {
  NextGrid = 0,  // On the next 16th-step boundary
  LoopEnd  = 1,  // When the playing loop projection cycle wraps (same as playback wrap detection)
};

/**
 * SlotStateMachine:
 * - `selectedSlotIndex` — preview slot (piano roll, edit focus) while transport runs.
 * - `pendingSlotIndex` — queued launch target until scheduled commit.
 * - Playing slot remains `Track::activeLoopIndex` (see TrackManager::getPlayingSlotIndex).
 */
class SlotStateMachine {
public:
  SlotStateMachine();

  uint8_t getSelectedSlotIndex(uint8_t trackIndex) const;
  void setSelectedSlotIndex(uint8_t trackIndex, uint8_t slotIndex);

  uint8_t getPendingSlotIndex(uint8_t trackIndex) const;
  SlotQuantization getPendingSlotQuantization(uint8_t trackIndex) const;

  /// Queue a pending slot switch (transient; cleared when committed or cancelled).
  void requestPendingSlotSwitch(uint8_t trackIndex,
                                 uint8_t slotIndex,
                                 SlotQuantization quantization,
                                 uint32_t queuedAtTick);

  void clearPendingSlotSwitch(uint8_t trackIndex);

  bool hasPendingSlotSwitch(uint8_t trackIndex) const;

  /// Returns whether the pending transition should be committed at `currentTick`.
  bool shouldCommitPendingSlotSwitch(uint8_t trackIndex,
                                      const Track& track,
                                      uint32_t currentTick) const;

private:
  uint8_t selectedSlotIndex[Config::NUM_TRACKS];
  uint8_t pendingSlotIndex[Config::NUM_TRACKS];
  SlotQuantization pendingSlotQuantization[Config::NUM_TRACKS];
  uint32_t pendingQueuedAtTick[Config::NUM_TRACKS];
};

