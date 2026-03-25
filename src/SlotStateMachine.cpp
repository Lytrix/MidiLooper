#include "SlotStateMachine.h"
#include "TickPhase.h"

#include "Track.h"
#include "Globals.h"

SlotStateMachine::SlotStateMachine() {
  for (uint8_t t = 0; t < Config::NUM_TRACKS; t++) {
    selectedSlotIndex[t] = 0;
    pendingSlotIndex[t] = SlotIndex::None;
    pendingSlotQuantization[t] = SlotQuantization::NextGrid;
    pendingQueuedAtTick[t] = UINT32_MAX;
  }
}

uint8_t SlotStateMachine::getSelectedSlotIndex(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? selectedSlotIndex[trackIndex] : 0;
}

void SlotStateMachine::setSelectedSlotIndex(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  selectedSlotIndex[trackIndex] = slotIndex;
}

uint8_t SlotStateMachine::getPendingSlotIndex(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? pendingSlotIndex[trackIndex] : SlotIndex::None;
}

SlotQuantization SlotStateMachine::getPendingSlotQuantization(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? pendingSlotQuantization[trackIndex] : SlotQuantization::NextGrid;
}

void SlotStateMachine::requestPendingSlotSwitch(uint8_t trackIndex,
                                                  uint8_t slotIndex,
                                                  SlotQuantization quantization,
                                                  uint32_t queuedAtTick) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;

  pendingSlotIndex[trackIndex] = slotIndex;
  pendingSlotQuantization[trackIndex] = quantization;
  pendingQueuedAtTick[trackIndex] = queuedAtTick;
}

void SlotStateMachine::clearPendingSlotSwitch(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  pendingSlotIndex[trackIndex] = SlotIndex::None;
  pendingQueuedAtTick[trackIndex] = UINT32_MAX;
}

bool SlotStateMachine::hasPendingSlotSwitch(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return false;
  return pendingSlotIndex[trackIndex] != SlotIndex::None;
}

bool SlotStateMachine::shouldCommitPendingSlotSwitch(uint8_t trackIndex,
                                                       const Track& track,
                                                       uint32_t currentTick) const {
  if (trackIndex >= Config::NUM_TRACKS) return false;

  const uint8_t slotIdx = pendingSlotIndex[trackIndex];
  if (slotIdx == SlotIndex::None) return false;

  // Prevent committing immediately in the same tick the request was made.
  if (currentTick == pendingQueuedAtTick[trackIndex]) return false;

  switch (pendingSlotQuantization[trackIndex]) {
    case SlotQuantization::NextGrid:
      return (currentTick % Config::TICKS_PER_16TH_STEP) == 0;

    case SlotQuantization::LoopEnd: {
      const uint32_t loopLen = track.getLoopLength();
      if (loopLen == 0) return false;
      const uint32_t startLoopTick = track.getStartLoopTick();
      return tickPhaseInLoop(currentTick, startLoopTick, loopLen) == 0;
    }
  }

  return false;
}

