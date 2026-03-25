#pragma once

#include <Arduino.h>
#include "Globals.h"

class Track; // Forward declaration

// Central sentinel for "no slot".
namespace SlotIndex {
  constexpr uint8_t None = UINT8_MAX;
}

// When a slot transition should occur.
enum class SlotQuantization : uint8_t {
  NextGrid = 0,  // On the next 16th-step boundary
  LoopEnd  = 1,  // When the currently playing loop wraps (tickInLoopStorage == 0)
};

/**
 * SlotStateMachine (Phase 1):
 * - Stores per-track UI focus: `selectedSlotIndex`.
 * - Stores one pending slot switch per track with a quantization rule:
 *   `pendingSlotIndex + pendingSlotQuantization`.
 *
 * This phase focuses on quantized playback switching; recording/overdubbing
 * is still tied to the Track's active loop.
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

