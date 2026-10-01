//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"
#include "TrackManagerInternal.h"

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "LooperState.h"
// S4b.7: the `SlotProvision` turn's own predicates (`slotProvisionNeeded`) live with the drain owners.
#include "Utils/MidiServiceDrain.h"
// S4b.7: the bounded hold's age test and named terminal (`SlotProvisionHold`, `holdExceeded`).
#include "Utils/SlotProvisionHold.h"
#include "StorageManager.h"
#include "Utils/SlotLoopContent.h"

uint8_t TrackManager::getActiveLoopIndex(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getActiveLoopIndex() : 0;
}

void TrackManager::setActiveLoopIndex(uint8_t trackIndex, uint8_t index) {
  if (trackIndex < Config::NUM_TRACKS) {
    Track& t = tracks[trackIndex];
    const uint8_t prev = t.getActiveLoopIndex();
    if (prev != index &&
        (t.isRecording() || t.isOverdubbing() || t.hasPendingDeferredOverdubStop())) {
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
      tracks[trackIndex].hasPendingDeferredOverdubStop() ||
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
      tracks[trackIndex].hasPendingDeferredOverdubStop() ||
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
      track.isAudiblePlaybackActive() ? activeSlot : selectedSlot;
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
  if (!track.isAudiblePlaybackActive()) {
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
          track.isAudiblePlaybackActive();
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
  // S4b.7 (Rev 20.3/21.7): this is a MIDI-input/action edge, so the inbound occupancy may **not** be taken
  // inline. Nothing is marked either: the pending switch *is* the eligibility for the `SlotProvision`
  // deferred-idle turn (`MidiServiceDrain::slotProvisionNeeded` reads exactly this state), so the switch
  // needs no separate request record and cannot be starved by one being consumed elsewhere.
}

FLASHMEM bool TrackManager::provisionSlotForSwitch(uint8_t trackIndex, uint8_t slotIndex) {
  // S4b.6 (b) / S4b.7: the **off-clock** allocation point for a switch's inbound occupancy. It is the only
  // place a pending switch may take its runtime: the commit path (`updateAllTracks`) and the MIDI edges
  // (`requestSlotSwitch`, `MidiButtonActions`) must stay allocation-free. Idempotent -- a slot that already
  // has occupancy is not re-provisioned -- and a refusal is reported with its own operation kind instead of
  // leaving the switch silently un-provisioned.
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  Track& track = tracks[trackIndex];
  if (track.hasPlaybackOccupancyForSlot(slotIndex)) {
    return true;
  }
  if (!track.prewarmPlaybackForSlot(slotIndex, /*prepareCapture=*/false)) {
    MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::SlotSwitch);
    return false;
  }
  return true;
}

FLASHMEM bool TrackManager::servicePendingSlotProvision() {
  // S4b.7 (Rev 20.3/21.7): one `SlotProvision` turn. It serves the **first** track whose pending switch
  // needs provisioning -- `slotProvisionNeeded` is the state-driven eligibility, so a switch needs no
  // request record -- and performs both halves the contract names: the provisioning (`provisionSlotForSwitch`,
  // idempotent, reporting its own refusal) and the target's merged-window build
  // (`ensurePlaybackMergedEventsForSlot`), which is what releases the long-loop pending hold. Nothing else
  // moves here: the commit stays the clock path's own step.
  //
  // Episode bookkeeping first: a track with no pending switch has no hold, so its report bit clears and a
  // later switch on the same track reports on its own account (once per episode, not once per boot).
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    if (!slotStateMachine.hasPendingSlotSwitch(trackIndex)) {
      slotSwitchHoldReportedMask_ =
          static_cast<uint8_t>(slotSwitchHoldReportedMask_ & ~(1u << trackIndex));
    }
  }
  const uint32_t nowTick = clockManager.getCurrentTick();
  const uint32_t holdBoundTicks = SlotProvisionHold::boundTicksFor(Config::TICKS_PER_BAR);
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    if (!slotStateMachine.hasPendingSlotSwitch(trackIndex)) {
      continue;
    }
    const uint8_t targetSlot = slotStateMachine.getPendingSlotIndex(trackIndex);
    if (targetSlot >= Config::MAX_LOOPS_PER_TRACK) {
      continue;
    }
    Track& track = tracks[trackIndex];
    bool didWork = false;
    if (MidiServiceDrain::slotProvisionNeeded(true, track.hasPlaybackOccupancyForSlot(targetSlot))) {
      if (!provisionSlotForSwitch(trackIndex, targetSlot)) {
        return false;  // refused and reported; the switch stays pending for a later turn
      }
      track.ensurePlaybackMergedEventsForSlot(targetSlot);
      didWork = true;
    }
    // The bounded hold, checked **whether or not** this turn could make progress: the case the bound exists
    // for is precisely a switch whose occupancy was taken and which still cannot commit (a demoted target
    // whose merged window needs a published root that never arrives). One report per episode; nothing moves,
    // so a later turn can still satisfy the switch (DEC-078 item 1).
    const uint32_t bit = 1u << trackIndex;
    if ((slotSwitchHoldReportedMask_ & bit) == 0) {
      const uint32_t queuedAtTick = slotStateMachine.getPendingQueuedAtTick(trackIndex);
      if (SlotProvisionHold::holdExceeded(nowTick, queuedAtTick, holdBoundTicks)) {
        reportSlotSwitchHold(trackIndex, targetSlot,
                             static_cast<uint32_t>(nowTick - queuedAtTick));
        slotSwitchHoldReportedMask_ = static_cast<uint8_t>(slotSwitchHoldReportedMask_ | bit);
      }
    }
    return didWork;
  }
  return false;
}

FLASHMEM void TrackManager::reportSlotSwitchHold(uint8_t trackIndex, uint8_t slotIndex,
                                                uint32_t ageTicks) {
  // A **report**, not a decision: the pending switch, the accepted-interaction latch, the enabled-set
  // replacement and any retained record start all stay exactly as they were, so a later turn can still
  // satisfy the switch. The terminal names the class (`SlotProvisionHold::kTerminalName`) and this row names
  // the switch it is about, so an operator can tell a slow switch from a dropped press.
  logger.info("Slot switch hold %s track=%u target=%u age=%lu ticks",
              SlotProvisionHold::kTerminalName, static_cast<unsigned>(trackIndex),
              static_cast<unsigned>(slotIndex), static_cast<unsigned long>(ageTicks));
#if defined(PIO_UNIT_TEST_NATIVE)
  SlotProvisionHold::recordReportForTest(trackIndex, slotIndex, ageTicks);
#endif
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
