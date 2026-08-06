//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "Globals.h"
#include "TrackManager.h"
#include "Utils/TrackMem.h"

extern TrackManager trackManager;

void Track::syncSlotRefsFromPool() {
  for (uint8_t i = 0; i < Config::MAX_LOOPS_PER_TRACK; ++i) {
    slots_[i].loopId = loopPool_.loopIdAt(i);
  }
}

void Track::ensureLoopsAllocated() {
  if (loopPool_.initialized()) {
    return;
  }
  loopPool_.ensureInitialized();
  if (!loopPool_.initialized()) {
    while (1) {
      delay(1);
    }  // Out of heap - should not happen
  }
  syncSlotRefsFromPool();
}

Loop& Track::loopForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  const uint8_t idx = slotIndex < Config::MAX_LOOPS_PER_TRACK ? slotIndex : 0;
  const LoopId id = slots_[idx].loopId;
  if (id != kInvalidLoopId) {
    Loop* found = loopPool_.findById(id);
    if (found != nullptr) {
      return *found;
    }
  }
  return loopPool_.at(idx);
}

const Loop& Track::loopForSlot(uint8_t slotIndex) const {
  return const_cast<Track*>(this)->loopForSlot(slotIndex);
}

LoopId Track::loopIdForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return kInvalidLoopId;
  }
  return slots_[slotIndex].loopId;
}

const Slot& Track::slotRef(uint8_t slotIndex) const {
  static const Slot kEmpty{};
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return kEmpty;
  }
  return slots_[slotIndex];
}

Loop& Track::getLoop(uint8_t index) {
  return loopForSlot(index);
}

const Loop& Track::getLoop(uint8_t index) const {
  return loopForSlot(index);
}

bool Track::hasDataInSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return loopForSlot(slotIndex).hasData();
}

TRACK_COLD_MEM bool Track::hasAnySlotData() const {
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (hasDataInSlot(s)) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM void Track::reconcileTransportStateAfterSlotMutation() {
  if (isRecording() || isOverdubbing() || isStoppedRecording()) {
    return;
  }
  if (trackState == TRACK_ARMED && getActiveLoop().hasCommittedPasses()) {
    armedPreRollNotes.clear();
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
      if (&trackManager.getTrack(i) == this) {
        trackManager.cancelPendingRecordArm(i);
        break;
      }
    }
    return;
  }
  if (hasAnySlotData()) {
    if (trackState == TRACK_EMPTY || trackState == TRACK_ARMED) {
      setState(TRACK_STOPPED);
    } else if (trackState == TRACK_PLAYING && !getActiveLoop().hasData()) {
      // Cleared the playing slot while other slots still have data — do not stay PLAYING
      // on empty active (blocks re-arm; session_20260803_170251).
      setState(TRACK_STOPPED);
    }
  } else if (trackState == TRACK_ARMED || trackState == TRACK_STOPPED ||
             trackState == TRACK_PLAYING || trackState == TRACK_OVERDUBBING) {
    setState(TRACK_EMPTY);
  } else if (trackState != TRACK_EMPTY) {
    setState(TRACK_EMPTY);
  }
}
