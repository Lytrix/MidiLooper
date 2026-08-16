//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"

#include "ClockManager.h"
#include "Globals.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/SlotFocusDisplay.h"

uint8_t TrackManager::getMidiLedPhaseSlotIndex(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) {
    return 0;
  }
  const Track& track = tracks[trackIndex];
  if (track.isPlaying() || track.isOverdubbing()) {
    return getPlayingSlotIndex(trackIndex);
  }
  return getPreviewSlotIndex(trackIndex);
}

void TrackManager::refreshTrackAndLoopSelectMidiLeds() {
  if (!ledManager) return;
  static constexpr uint8_t VEL_SELECTED_SLOT = 127;

  bool trackHasData[Config::NUM_TRACKS];
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    trackHasData[i] = false;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (tracks[i].hasDataInSlot(s)) {
        trackHasData[i] = true;
        break;
      }
    }
  }

  const uint8_t previewSlot = getPreviewSlotIndex(selectedTrack);
  const uint8_t playingSlot = getPlayingSlotIndex(selectedTrack);
  const bool pendingSwitch = hasPendingSlotSwitch(selectedTrack);
  const uint8_t pendingSlot =
      pendingSwitch ? getPendingSlotIndex(selectedTrack) : Config::INVALID_LOOP_SLOT;
  const bool pendingPulseBright = previewPlayheadFlashVisible(millis());
  uint8_t slotVelocities[Config::MAX_LOOPS_PER_TRACK] = {0};
  Track& st = tracks[selectedTrack];

  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    const bool focus = (s == previewSlot);
    const bool hasData = st.hasDataInSlot(s);

    SlotOpState opState = st.getSlotOpState(s);
    if (opState == SlotOpState::SLOT_OP_RECORDING || opState == SlotOpState::SLOT_OP_OVERDUBBING) {
      slotVelocities[s] = 96;
      continue;
    }
    if (isRecordingQueued(selectedTrack, s)) {
      slotVelocities[s] = 96;
      continue;
    }

    if (!hasData) {
      const bool armedHere = (playingSlot == s && st.isArmed());
      if (armedHere) {
        slotVelocities[s] = 80;
      } else if (focus) {
        slotVelocities[s] = 40;
      } else {
        slotVelocities[s] = 0;
      }
      continue;
    }

    const bool enabled = slotEnabled[selectedTrack][s];
    const bool mutedSlot = slotMuted[selectedTrack][s];
    const bool transportActive = st.isPlaying() || st.isOverdubbing();
    if (!enabled) {
      slotVelocities[s] = 32;
    } else if (mutedSlot && transportActive && s == playingSlot) {
      slotVelocities[s] = 16;
    } else if (transportActive && s == playingSlot) {
      slotVelocities[s] = 48;
    } else {
      slotVelocities[s] = 32;
    }
  }

  // LED precedence: pending launch pulse > preview selection > playing phase (already set).
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (slotVelocities[s] == 96) {
      continue;
    }
    if (pendingSwitch && s == pendingSlot) {
      slotVelocities[s] = pendingPulseBright ? 96U : 64U;
      continue;
    }
    if (s == previewSlot) {
      slotVelocities[s] = VEL_SELECTED_SLOT;
    }
  }

  ledManager->updateTrackSelectLeds(selectedTrack, trackHasData, previewSlot, slotVelocities);
}

FLASHMEM void TrackManager::updateMidiLedsDeferred() {
  if (!ledManager) return;
  Track& selTrack = getSelectedTrack();
  const uint8_t phaseSlot = getMidiLedPhaseSlotIndex(selectedTrack);
  uint32_t currentTick = clockManager.getCurrentTick();
  uint32_t ledPhaseTick = currentTick;
  if (selTrack.isJamPlaybackActive() && selTrack.isJamming()) {
    ledPhaseTick = selTrack.getEffectivePlaybackTick(currentTick);
  }
#if defined(SESSION_CAPTURE)
  const bool measure = anyLoopPrefixMeasureAfterUndo();
  uint32_t childStartUs = 0;
  if (measure) {
    childStartUs = micros();
  }
#endif
  ledManager->updateLeds(selTrack, ledPhaseTick, phaseSlot);
#if defined(SESSION_CAPTURE)
  if (measure) {
    DebugSessionCapture::recordLoopRemainderSpan("midi_led_phase", micros() - childStartUs);
  }
#endif
  if (selTrack.getLoopLengthForSlot(phaseSlot) > 0) {
#if defined(SESSION_CAPTURE)
    if (measure) {
      childStartUs = micros();
    }
#endif
    ledManager->updateCurrentTick(selTrack, ledPhaseTick, phaseSlot);
#if defined(SESSION_CAPTURE)
    if (measure) {
      DebugSessionCapture::recordLoopRemainderSpan("midi_led_tick", micros() - childStartUs);
    }
#endif
  }
#if defined(SESSION_CAPTURE)
  if (measure) {
    childStartUs = micros();
  }
#endif
  refreshTrackAndLoopSelectMidiLeds();
#if defined(SESSION_CAPTURE)
  if (measure) {
    DebugSessionCapture::recordLoopRemainderSpan("midi_led_select", micros() - childStartUs);
  }
#endif
}

void TrackManager::updateMidiLeds(uint32_t currentTick) {
  if (ledManager) {
    ledManager->updateLeds(getSelectedTrack(), currentTick,
                           getMidiLedPhaseSlotIndex(selectedTrack));
  }
}

void TrackManager::forceMidiLedUpdate(uint32_t currentTick) {
  if (bootLoadInProgress_) {
    return;
  }
  if (ledManager) {
    Track& selTrack = getSelectedTrack();
    const uint8_t phaseSlot = getMidiLedPhaseSlotIndex(selectedTrack);
    uint32_t ledPhaseTick = currentTick;
    if (selTrack.isJamPlaybackActive() && selTrack.isJamming()) {
      ledPhaseTick = selTrack.getEffectivePlaybackTick(currentTick);
    }
    ledManager->forceUpdate(selTrack, ledPhaseTick, phaseSlot);
    if (selTrack.getLoopLengthForSlot(phaseSlot) > 0) {
      ledManager->updateCurrentTick(selTrack, ledPhaseTick, phaseSlot);
    }
    refreshTrackAndLoopSelectMidiLeds();
  }
}

void TrackManager::clearMidiLeds() {
  if (ledManager) {
    ledManager->clearAllLeds();  // This now also clears the current tick indicator
  }
}
