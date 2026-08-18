//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"
#include "TrackManagerInternal.h"

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "LooperState.h"
#include "StorageManager.h"
#include "Utils/SlotLoopContent.h"

uint8_t TrackManager::getActiveLoopIndex(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getActiveLoopIndex() : 0;
}

void TrackManager::setActiveLoopIndex(uint8_t trackIndex, uint8_t index) {
  if (trackIndex < Config::NUM_TRACKS) {
    Track& t = tracks[trackIndex];
    const uint8_t prev = t.getActiveLoopIndex();
    if (prev != index && (t.isRecording() || t.isOverdubbing())) {
      finalizeCaptureAndSelectSlot(trackIndex, index, clockManager.getCurrentTick());
      return;
    }
    t.setActiveLoopIndex(index);
    StorageManager::prioritizeLoopSlotRestoreForFocus(trackIndex, index);
    forceMidiLedUpdate(clockManager.getCurrentTick());
  }
}

void TrackManager::setLayeredSlotHeld(uint8_t trackIndex, uint8_t slotIndex, bool held) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  heldLayerSlot[trackIndex][slotIndex] = held;
}

bool TrackManager::isSlotEnabled(uint8_t trackIndex, uint8_t slotIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return slotEnabled[trackIndex][slotIndex];
}

bool TrackManager::isSlotMuted(uint8_t trackIndex, uint8_t slotIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return slotMuted[trackIndex][slotIndex];
}

void TrackManager::setSlotEnabled(uint8_t trackIndex, uint8_t slotIndex, bool enabled) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  slotEnabled[trackIndex][slotIndex] = enabled;
}

void TrackManager::setSlotMuted(uint8_t trackIndex, uint8_t slotIndex, bool mutedValue) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  const bool becomingMuted = mutedValue && !slotMuted[trackIndex][slotIndex];
  slotMuted[trackIndex][slotIndex] = mutedValue;
  if (becomingMuted) {
    tracks[trackIndex].silenceSlotMidiOutput(slotIndex);
  }
}

void TrackManager::toggleSlotMuted(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  slotMuted[trackIndex][slotIndex] = !slotMuted[trackIndex][slotIndex];
  if (slotMuted[trackIndex][slotIndex]) {
    tracks[trackIndex].silenceSlotMidiOutput(slotIndex);
  }
}

uint8_t TrackManager::countEnabledSlots(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return 0;
  uint8_t count = 0;
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    if (slotEnabled[trackIndex][slot]) count++;
  }
  return count;
}

void TrackManager::beginSlotSelectionHold(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (!tracks[trackIndex].isPlaying()) {
    return;
  }
  if (tracks[trackIndex].isRecording() || tracks[trackIndex].isOverdubbing() ||
      pendingRecord[trackIndex]) {
    return;
  }
  if (pendingHoldActive[trackIndex][slotIndex]) return;

  // First hold armed: start a fresh pending enabled set.
  if (pendingHoldCount[trackIndex] == 0) {
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      pendingSlotEnabled[trackIndex][slot] = false;
      pendingHoldActive[trackIndex][slot] = false;
    }
    pendingMultiSlotCommit[trackIndex] = false;
    pendingMultiSlotQueuedAtTick[trackIndex] = UINT32_MAX;
    pendingEnabledSetReplacement[trackIndex] = false;
  }

  pendingHoldActive[trackIndex][slotIndex] = true;
  pendingHoldCount[trackIndex]++;

  // Include this slot in the pending enabled set.
  pendingSlotEnabled[trackIndex][slotIndex] = true;
  // Default: newly enabled slots start unmuted.
  pendingMultiSlotCommit[trackIndex] = false;  // cancel any in-progress commit build
}

void TrackManager::endSlotSelectionHold(uint8_t trackIndex, uint8_t slotIndex, uint32_t nowTick) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (!pendingHoldActive[trackIndex][slotIndex]) return;

  pendingHoldActive[trackIndex][slotIndex] = false;
  if (pendingHoldCount[trackIndex] > 0) pendingHoldCount[trackIndex]--;

  if (tracks[trackIndex].isRecording() || tracks[trackIndex].isOverdubbing() ||
      pendingRecord[trackIndex]) {
    if (pendingHoldCount[trackIndex] == 0) {
      cancelSlotSelectionHold(trackIndex);
    }
    return;
  }

  // Commit when the last held slot is released.
  if (pendingHoldCount[trackIndex] == 0) {
    bool anyPending = false;
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      if (pendingSlotEnabled[trackIndex][slot]) {
        anyPending = true;
        break;
      }
    }
    if (anyPending) {
      pendingMultiSlotCommit[trackIndex] = true;
      pendingMultiSlotQueuedAtTick[trackIndex] = nowTick;
    } else {
      // Nothing selected: keep existing enabled set.
      for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        pendingSlotEnabled[trackIndex][slot] = false;
      }
    }
  }
}

void TrackManager::cancelSlotSelectionHold(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;

  pendingHoldCount[trackIndex] = 0;
  pendingMultiSlotCommit[trackIndex] = false;
  pendingMultiSlotQueuedAtTick[trackIndex] = UINT32_MAX;

  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    pendingHoldActive[trackIndex][slot] = false;
    pendingSlotEnabled[trackIndex][slot] = false;
  }
}

void TrackManager::setPendingEnabledSetReplacement(uint8_t trackIndex, bool enabled) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  pendingEnabledSetReplacement[trackIndex] = enabled;
}

uint8_t TrackManager::getSelectedSlotIndex(uint8_t trackIndex) const {
  return slotStateMachine.getSelectedSlotIndex(trackIndex);
}

uint8_t TrackManager::getPlayingSlotIndex(uint8_t trackIndex) const {
  return getActiveLoopIndex(trackIndex);
}

uint8_t TrackManager::getPreviewSlotIndex(uint8_t trackIndex) const {
  return getSelectedSlotIndex(trackIndex);
}

uint8_t TrackManager::getPendingSlotIndex(uint8_t trackIndex) const {
  return slotStateMachine.getPendingSlotIndex(trackIndex);
}

bool TrackManager::hasPendingSlotSwitch(uint8_t trackIndex) const {
  return slotStateMachine.hasPendingSlotSwitch(trackIndex);
}

bool TrackManager::slotHasLoopContent(uint8_t trackIndex, uint8_t slotIndex, bool restoreFromSd) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  Track& track = tracks[trackIndex];
  const bool hasDataInRam = track.hasDataInSlot(slotIndex);
  StorageManager::refreshLoopSlotPayloadOnSdInRam(trackIndex, slotIndex);
  const bool hasPayloadOnSd = StorageManager::hasLoopSlotPayloadOnSdInRam(trackIndex, slotIndex);
  if (!slotHasLoopContentInRamOrSd(hasDataInRam, hasPayloadOnSd)) {
    return false;
  }
  if (!hasDataInRam && restoreFromSd) {
    // Queue deferred Commit; do not block on sync SD. Payload-on-SD counts as content.
    StorageManager::prioritizeLoopSlotRestoreForFocus(trackIndex, slotIndex);
    return track.hasDataInSlot(slotIndex) || hasPayloadOnSd;
  }
  return true;
}

uint8_t TrackManager::getSelectedLoopIndex(uint8_t trackIndex) const {
  return getSelectedSlotIndex(trackIndex);
}

Loop& TrackManager::getSelectedLoop(uint8_t trackIndex) {
  return tracks[trackIndex].getLoop(getSelectedSlotIndex(trackIndex));
}

const Loop& TrackManager::getSelectedLoop(uint8_t trackIndex) const {
  return tracks[trackIndex].getLoop(getSelectedSlotIndex(trackIndex));
}

Loop& TrackManager::getSelectedLoop(Track& track) {
  return getSelectedLoop(resolveTrackIndex(track));
}

const Loop& TrackManager::getSelectedLoop(const Track& track) const {
  return getSelectedLoop(resolveTrackIndex(track));
}

void TrackManager::loadTransportSlotIndices(uint8_t trackIndex, uint8_t activeSlot,
                                            uint8_t selectedSlot) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  if (selectedSlot >= Config::MAX_LOOPS_PER_TRACK) {
    selectedSlot = 0;
  }
  if (activeSlot >= Config::MAX_LOOPS_PER_TRACK) {
    activeSlot = 0;
  }
  slotStateMachine.setSelectedSlotIndex(trackIndex, selectedSlot);
  Track& track = tracks[trackIndex];
  const uint8_t playingSlot =
      (track.isPlaying() || track.isOverdubbing()) ? activeSlot : selectedSlot;
  track.setActiveLoopIndex(playingSlot);
  // updateAllTracks only calls playMidiEvents when slotEnabled[playingSlot].
  // Bundle slotEnabled can leave the selected slot disabled while another slot
  // remains enabled — after stopped remap (active:=selected) that yields piano-roll
  // notes with no MO. Ensure the slot that will play is enabled (do not clear
  // other enabled layers).
  slotEnabled[trackIndex][playingSlot] = true;
}

void TrackManager::setSelectedSlotIndex(uint8_t trackIndex, uint8_t slotIndex,
                                        SyncPlayback syncPlayback) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  const uint8_t previousSlot = slotStateMachine.getSelectedSlotIndex(trackIndex);
  Track& track = tracks[trackIndex];
  if (!track.isPlaying() && !track.isOverdubbing()) {
    replaceSingleEnabledSlotWithTarget(slotEnabled, slotMuted, trackIndex, slotIndex, true);
  }
  if (slotIndex == previousSlot) {
    if (trackIndex == selectedTrack) {
      const bool splitFocus =
          track.isPlaying() && getPlayingSlotIndex(trackIndex) != slotIndex;
      if (splitFocus || hasPendingSlotSwitch(trackIndex)) {
        displayManager.invalidateForSlotChange(trackIndex, slotIndex, slotIndex);
        forceMidiLedUpdate(clockManager.getCurrentTick());
      }
    }
    return;
  }
  if (trackIndex == selectedTrack) {
    editManager.beforeSelectedSlotChange(track);
  }
  slotStateMachine.setSelectedSlotIndex(trackIndex, slotIndex);
  StorageManager::prioritizeLoopSlotRestoreForFocus(trackIndex, slotIndex);
  if (syncPlayback == SyncPlayback::Yes) {
    setActiveLoopIndex(trackIndex, slotIndex);
  }
  if (trackIndex == selectedTrack) {
    if (track.isEmpty() && tracks[trackIndex].hasDataInSlot(slotIndex)) {
      tracks[trackIndex].forceSetState(TRACK_STOPPED);
    }
    editManager.onSelectedSlotChanged(tracks[trackIndex], previousSlot);
    displayManager.invalidateForSlotChange(trackIndex, previousSlot, slotIndex);
    forceMidiLedUpdate(clockManager.getCurrentTick());
    if (!bootLoadInProgress_) {
      // Selected-slot index always needs a light footer persist. A full workspace save on
      // every preview select while playing blocks the LoopEnd launch (FinalizeWorkspace).
      StorageManager::requestWorkspaceFooterPersistWhenSafe();
      const bool previewOnlyWhilePlaying =
          syncPlayback == SyncPlayback::No &&
          (track.isPlaying() || track.isOverdubbing());
      if (!previewOnlyWhilePlaying) {
        StorageManager::requestDeferredSaveState(looperState.getLooperState());
      }
    }
  }
}

void TrackManager::requestSlotSwitch(uint8_t trackIndex, uint8_t slotIndex,
                                     SlotQuantization quantization, uint32_t queuedAtTick) {
  StorageManager::refreshLoopSlotPayloadOnSdInRam(trackIndex, slotIndex);
  slotStateMachine.requestPendingSlotSwitch(trackIndex, slotIndex, quantization, queuedAtTick);
}

void TrackManager::refreshPreviewSlotFocus(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex != selectedTrack || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  displayManager.invalidateForSlotChange(trackIndex, slotIndex, slotIndex);
}

void TrackManager::clearPendingSlotSwitch(uint8_t trackIndex) {
  slotStateMachine.clearPendingSlotSwitch(trackIndex);
}
