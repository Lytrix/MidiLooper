//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"

#include "ClockManager.h"
#include "DisplayManager.h"
#include "Logger.h"
#include "LooperState.h"
#include "StorageManager.h"
#include "Utils/PlaybackMidiOutput.h"
#include "Utils/RuntimeTimingTelemetry.h"

void TrackManager::startPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) {
    StorageManager::prioritizeLoopSlotRestoreForFocus(trackIndex,
                                                      tracks[trackIndex].getActiveLoopIndex());
    tracks[trackIndex].startPlaying(clockManager.getCurrentTick());
  }
}

void TrackManager::stopPlayingTrack(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  tracks[trackIndex].stopPlaying();
  clearPendingSlotSwitch(trackIndex);
  pendingEnabledSetReplacement[trackIndex] = false;
  cancelSlotSelectionHold(trackIndex);
  tracks[trackIndex].clearQueuedPlaybackStart();
}

void TrackManager::handleTransportStop() {
  uint32_t currentTick = clockManager.getCurrentTick();
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    Track& t = tracks[i];
    // Always clear queued quantized actions when transport stops.
    pendingRecord[i] = false;
    pendingRecordQueuedAtTick[i] = UINT32_MAX;
    pendingRecordRefSlot[i] = Config::INVALID_LOOP_SLOT;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[i][s] = false;
      heldLayerSlot[i][s] = false;
    }
    pendingStop[i] = false;
    slotStateMachine.clearPendingSlotSwitch(i);
    pendingEnabledSetReplacement[i] = false;
    cancelSlotSelectionHold(i);
    tracks[i].clearQueuedPlaybackStart();
    if (t.isRecording()) {
      // Stop recording BEFORE sendAllNotesOff(): finalizePendingNotes() must record
      // note-offs for still-held notes, but sendAllNotesOff() clears pendingNotes,
      // which left the last note-on orphaned so validation removed it.
      t.stopRecordingToStopped(currentTick);
      t.sendAllNotesOff();
      uint32_t recordedLength = t.getLoopLength();
      if (recordedLength > 0 && masterLoopLength == 0) {
        setMasterLoopLength(recordedLength);
      }
      if (autoAlignEnabled) {
        t.setLoopLength(masterLoopLength);
      }
    } else if (t.isOverdubbing()) {
      // Same order as record: finalize held notes before sendAllNotesOff()
      // clears pendingNotes. [`122152`] wrap_synth Off@168 was an orphaned On.
      t.stopOverdubbingToStopped();
      t.sendAllNotesOff();
    } else if (t.isPlaying()) {
      t.stopPlaying();  // sends All Notes Off internally
    } else if (t.isArmed()) {
      t.sendAllNotesOff();
      if (t.hasAnySlotData()) {
        t.setState(TRACK_STOPPED);
      } else {
        t.setState(TRACK_EMPTY);
      }
    } else {
      t.sendAllNotesOff();
    }
  }
  forceMidiLedUpdate(currentTick);
  if (StorageManager::shouldQueueCurrentWorkspaceSave()) {
    StorageManager::admitGlobalMeta();
  }
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackManager::queueBarPlaybackStart(uint8_t trackIndex, int32_t storageTick,
                                         uint32_t queuedAtTick) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  slotStateMachine.clearPendingSlotSwitch(trackIndex);
  pendingEnabledSetReplacement[trackIndex] = false;
  tracks[trackIndex].queuePlaybackStartAtGrid(storageTick, queuedAtTick);
}

void TrackManager::advanceJamTicks(uint32_t delta) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    tracks[i].advanceJamTick(delta);
  }
}

void TrackManager::updateAllTracks(uint32_t currentTick) {
  const uint32_t tracksStartUs = micros();
  handlePendingRecordStart(currentTick);

  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    // Commit multi-hold selection (enabled set + start at next 16th).
    if (pendingMultiSlotCommit[i] &&
        currentTick != pendingMultiSlotQueuedAtTick[i] &&
        (currentTick % Config::TICKS_PER_16TH_STEP) == 0) {
      if (tracks[i].isRecording() || tracks[i].isOverdubbing() || pendingRecord[i]) {
        pendingMultiSlotCommit[i] = false;
        pendingMultiSlotQueuedAtTick[i] = UINT32_MAX;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
          pendingSlotEnabled[i][s] = false;
        }
        continue;
      }

      // Apply pendingSlotEnabled -> slotEnabled and default unmute.
      bool anyEnabledUnmutedWithData = false;
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        slotEnabled[i][s] = pendingSlotEnabled[i][s];
        // Keep mute if the slot stays enabled, but default to unmuted for new selection.
        slotMuted[i][s] = false;
        if (slotEnabled[i][s] && !slotMuted[i][s] && tracks[i].hasDataInSlot(s)) {
          anyEnabledUnmutedWithData = true;
        }
      }
      pendingMultiSlotCommit[i] = false;
      pendingMultiSlotQueuedAtTick[i] = UINT32_MAX;
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) pendingSlotEnabled[i][s] = false;

      // Choose active slot: prefer selected slot if it is enabled and has data.
      uint8_t selectedSlot = slotStateMachine.getSelectedSlotIndex(i);
      if (!slotEnabled[i][selectedSlot] || !tracks[i].hasDataInSlot(selectedSlot)) {
        selectedSlot = 0;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
          if (slotEnabled[i][s] && tracks[i].hasDataInSlot(s)) {
            selectedSlot = s;
            break;
          }
        }
      }
      slotStateMachine.setSelectedSlotIndex(i, selectedSlot);
      setActiveLoopIndex(i, selectedSlot);
      // Reset playback indices so all newly enabled slots start at the commit phase.
      tracks[i].resetPlaybackStateForSlot(selectedSlot, currentTick);
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        if (slotEnabled[i][s] && !slotMuted[i][s] && tracks[i].hasDataInSlot(s)) {
          tracks[i].resetPlaybackStateForSlot(s, currentTick);
        }
      }

      // Ensure playback is running if we have any audible slot data.
      if (anyEnabledUnmutedWithData && !tracks[i].isPlaying() && !tracks[i].isOverdubbing()) {
        // Track state machine may block EMPTY->PLAYING; we keep this guard minimal.
        startPlayingTrack(i);
      }

      forceMidiLedUpdate(currentTick);
    }

    const bool audible = isTrackAudible(i);
    const uint32_t playTick = tracks[i].getEffectivePlaybackTick(currentTick);

    // Commit before playMidiEvents so lastTickInLoop still reflects the previous tick.
    if (tracks[i].isPlaying() && slotStateMachine.hasPendingSlotSwitch(i) &&
        slotStateMachine.shouldCommitPendingSlotSwitch(i, tracks[i], playTick)) {
      const uint8_t targetSlot = slotStateMachine.getPendingSlotIndex(i);
      if (targetSlot >= Config::MAX_LOOPS_PER_TRACK) {
        slotStateMachine.clearPendingSlotSwitch(i);
        pendingEnabledSetReplacement[i] = false;
      } else if (!tracks[i].hasCommittedPassesInSlot(targetSlot)) {
        // HEADER_READY / SD still loading: keep pending until Commit.
        // hasDataInSlot is true from metadata length alone — launching that empty 64-bar
        // slot then letting LoadLoopJob adopt into the *active* playing loop hard-faults
        // on fast track→slot3 (session_20260718_205809).
        if (!StorageManager::hasLoopSlotPayloadOnSdInRam(i, targetSlot)) {
          logger.info("LoopEnd playback commit cancelled track=%u target=%u (no committed MIDI)",
                      static_cast<unsigned>(i), static_cast<unsigned>(targetSlot));
          slotStateMachine.clearPendingSlotSwitch(i);
          pendingEnabledSetReplacement[i] = false;
        }
      } else if (!tracks[i].isPlaybackMergedMidiEventsReadyForSlot(targetSlot) &&
                 tracks[i].getLoop(targetSlot).shouldAvoidFullVisualRebuild(
                     tracks[i].getLoop(targetSlot).loopLengthTicks)) {
        // Long loop just Committed: do not full-gather on the clock path (210001).
        // Main prewarms after LoadLoopJob Commit; keep pending until window is ready.
      } else if (tracks[i].hasDataInSlot(targetSlot)) {
        const uint8_t previousPlaying = getPlayingSlotIndex(i);
        slotStateMachine.clearPendingSlotSwitch(i);
        // Wire silence before swapping the audible slot (same pattern as overdub→play).
        tracks[i].sendAllNotesOff();
        tracks[i].ensurePlaybackMergedEventsForSlot(targetSlot);
        setActiveLoopIndex(i, targetSlot);
        const Loop& targetLoop = tracks[i].getLoop(targetSlot);
        tracks[i].clearQueuedPlaybackStart();
        tracks[i].queuePlaybackStartAtGrid(static_cast<int32_t>(targetLoop.loopStartTick),
                                            currentTick);
        tracks[i].commitQueuedPlaybackStart(currentTick);
        logger.info("LoopEnd playback commit track=%u %u->%u start=%lu len=%lu",
                    static_cast<unsigned>(i), static_cast<unsigned>(previousPlaying),
                    static_cast<unsigned>(targetSlot),
                    static_cast<unsigned long>(targetLoop.loopStartTick),
                    static_cast<unsigned long>(targetLoop.loopLengthTicks));

        if (i == selectedTrack) {
          if (targetSlot != previousPlaying) {
            displayManager.invalidateForSlotChange(i, previousPlaying, getPreviewSlotIndex(i));
          }
          forceMidiLedUpdate(currentTick);
        }

        // If this slot switch came from a "select single slot" gesture, replace enabled set.
        if (pendingEnabledSetReplacement[i]) {
          for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            slotEnabled[i][s] = (s == targetSlot);
            slotMuted[i][s] = false;
          }
          pendingEnabledSetReplacement[i] = false;
          forceMidiLedUpdate(currentTick);
        }
      } else {
        // Safety: pending target no longer has loop data, cancel it.
        logger.info("LoopEnd playback commit cancelled track=%u target=%u (no RAM data)",
                    static_cast<unsigned>(i), static_cast<unsigned>(targetSlot));
        slotStateMachine.clearPendingSlotSwitch(i);
        pendingEnabledSetReplacement[i] = false;
      }
    }

    // Bar-queued playback start (no pending slot switch).
    if (tracks[i].isPlaying() && !slotStateMachine.hasPendingSlotSwitch(i) &&
        tracks[i].shouldCommitQueuedPlaybackStart(currentTick)) {
      tracks[i].commitQueuedPlaybackStart(currentTick);
    }

    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }

    const uint8_t activeSlot = tracks[i].getActiveLoopIndex();

    // Primary (active) slot playback. Mute/solo/slot-mute suppress MIDI send, not the engine.
    if (PlaybackMidiOutput::engineShouldRun(slotEnabled[i][activeSlot])) {
      tracks[i].playMidiEvents(
          playTick, PlaybackMidiOutput::shouldSend(audible, slotMuted[i][activeSlot]));
    }

    // Additional enabled slots.
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (s == activeSlot) continue;
      if (PlaybackMidiOutput::engineShouldRun(slotEnabled[i][s])) {
        tracks[i].playMidiEventsForSlot(
            s, playTick, PlaybackMidiOutput::shouldSend(audible, slotMuted[i][s]));
      }
    }
  }
  RuntimeTimingTelemetry::noteTracksUpdate(micros() - tracksStartUs);
}
