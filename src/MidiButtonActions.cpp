//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiButtonActions.h"
#include "Globals.h"
#include "TrackManager.h"
#include "EditManager.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackUndo.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "NoteEditManager.h"
#include "NoteEditSessionState.h"
#include "EditStates/EditSelectNoteState.h"
#include "TrackUndo.h"
#include "Loop.h"
#include "DisplayManager.h"
#include "SetBrowserOverlayPolicy.h"
#include "Utils/PressTiming.h"

namespace {

uint8_t refSlotPhaseForQueue(const Track& track, uint8_t previousSlot) {
  if (previousSlot >= ::Config::MAX_LOOPS_PER_TRACK) return ::Config::INVALID_LOOP_SLOT;
  const Loop& rl = track.getLoop(previousSlot);
  return (rl.loopLengthTicks > 0) ? previousSlot : ::Config::INVALID_LOOP_SLOT;
}

bool slotHasDataForPlaybackSwitch(uint8_t trackIdx, uint8_t slotIndex) {
  return trackManager.slotHasLoopContent(trackIdx, slotIndex, true);
}

void ensureActiveLoopForRecord(uint8_t trackIdx, Track& track, uint8_t slotIndex) {
  if (slotIndex != track.getActiveLoopIndex()) {
    trackManager.setActiveLoopIndex(trackIdx, slotIndex);
  }
}

void queuePlayingSlotSwitch(uint8_t trackIdx, Track& track, uint8_t slotIndex, uint32_t now) {
  const uint8_t enabledCount = trackManager.countEnabledSlots(trackIdx);
  const bool slotEnabled = trackManager.isSlotEnabled(trackIdx, slotIndex);
  trackManager.clearPendingSlotSwitch(trackIdx);
  const bool editAuditionSingleSlot =
      editManager.isLoopEditSession() || editManager.isNoteEditActive();
  if (clockManager.shouldQuantizeRecordStart()) {
    trackManager.setSelectedSlotIndex(trackIdx, slotIndex, SyncPlayback::No);
    // Pre-build destination send buffer while the current loop still plays.
    track.ensurePlaybackMergedEventsForSlot(slotIndex);
    if (enabledCount > 1 && !editAuditionSingleSlot) {
      trackManager.setPendingEnabledSetReplacement(trackIdx, false);
      trackManager.requestSlotSwitch(trackIdx, slotIndex, SlotQuantization::LoopEnd, now);
    } else {
      if (!slotEnabled || editAuditionSingleSlot) {
        trackManager.setPendingEnabledSetReplacement(trackIdx, true);
      }
      trackManager.requestSlotSwitch(trackIdx, slotIndex, SlotQuantization::LoopEnd, now);
    }
    if (trackIdx == trackManager.getSelectedTrackIndex()) {
      trackManager.refreshPreviewSlotFocus(trackIdx, slotIndex);
    }
  } else {
    trackManager.setPendingEnabledSetReplacement(trackIdx, false);
    if (enabledCount == 1) {
      for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
        trackManager.setSlotEnabled(trackIdx, s, (s == slotIndex));
        trackManager.setSlotMuted(trackIdx, s, false);
      }
    }
    trackManager.setSelectedSlotIndex(trackIdx, slotIndex);
    const Loop& targetLoop = track.getLoop(slotIndex);
    track.clearQueuedPlaybackStart();
    track.queuePlaybackStartAtGrid(static_cast<int32_t>(targetLoop.loopStartTick), now);
    track.commitQueuedPlaybackStart(now);
  }
  trackManager.forceLedUpdate(now);
}

uint32_t overlayPlayStopFirstShortAtMs = 0;

void resetOverlayPlayStopPairedShort() {
  overlayPlayStopFirstShortAtMs = 0;
}

void handleOverlayPlayStopShort(uint32_t now) {
  constexpr uint32_t kPairWindowMs = PressTiming::DOUBLE_TAP_WINDOW;
  if (overlayPlayStopFirstShortAtMs != 0 &&
      (now - overlayPlayStopFirstShortAtMs) > kPairWindowMs) {
    overlayPlayStopFirstShortAtMs = 0;
  }
  if (overlayPlayStopFirstShortAtMs != 0 &&
      (now - overlayPlayStopFirstShortAtMs) <= kPairWindowMs) {
    resetOverlayPlayStopPairedShort();
    looperState.exitLoadSaveMode();
    logger.info("Load/save mode exited (paired play/stop short press)");
    return;
  }
  overlayPlayStopFirstShortAtMs = now;
}

}  // namespace

// Global instances (matching your current system)
extern TrackManager trackManager;
extern EditManager editManager;
extern ClockManager clockManager;
extern MidiHandler midiHandler;
extern Logger logger;
extern NoteEditManager noteEditManager;

// Global instance
MidiButtonActions midiButtonActions;

namespace {

void restoreAudiblePlaybackAfterSlotClear(uint8_t trackIndex, Track& track, uint32_t now) {
  bool foundAudible = false;
  uint8_t newActiveSlot = 0;
  for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (trackManager.isSlotEnabled(trackIndex, s) &&
        !trackManager.isSlotMuted(trackIndex, s) &&
        track.hasDataInSlot(s)) {
      foundAudible = true;
      newActiveSlot = s;
      break;
    }
  }
  if (foundAudible) {
    trackManager.setActiveLoopIndex(trackIndex, newActiveSlot);
    for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (trackManager.isSlotEnabled(trackIndex, s) &&
          !trackManager.isSlotMuted(trackIndex, s) &&
          track.hasDataInSlot(s)) {
        track.resetPlaybackStateForSlot(s, now);
      }
    }
    track.forceSetState(TRACK_STOPPED);
    track.startPlaying(now);
  } else {
    track.sendAllNotesOff();
  }
}

// SavedSet gesture map (M2):
// - Edit mode (note 38) double-press: toggle load/save set browser (enter + exit).
// - In overlay: record (36) short = scroll down; track (37) short = scroll up;
//   edit mode (38) short = confirm row (MidiButtonManager); edit long/double = overlay nav;
//   NOTELEN (35) double = favorite on Set row (root only). Confirm is not on NOTELEN.
// - Play/Stop (note 40) double-press: also toggles load/save (extended transport).
// - Play/Stop long-press release: center detailed window on playhead; hold tracks playhead.
// - SAVE NEW: dedicated combo remains TBD until Set Browser UX is wired.
// - LOAD INTO CURRENT: routed from browser selection, not a direct transport shortcut.

/// Armed + transport stopped: record press starts transport (DIN Start). Armed + transport running: cancel arm.
bool handleArmedRecordPress(uint8_t trackIdx) {
  Track& track = trackManager.getTrack(trackIdx);
  if (!track.isArmed() && !trackManager.hasQueuedRecordingTrack(trackIdx)) {
    return false;
  }
  if (!clockManager.isTransportRunning()) {
    logger.info("Armed for record — starting transport (MIDI Start on DIN/USB)");
    midiButtonActions.handleToggleTransport();
  } else {
    logger.info("Cancel record arm");
    trackManager.cancelPendingRecordArm(trackIdx);
  }
  return true;
}

}  // namespace

// Constructor
MidiButtonActions::MidiButtonActions() {
    copiedNote.hasData = false;
}

// Execute action based on type
void MidiButtonActions::executeAction(MidiButtonConfig::ActionType actionType, uint32_t parameter) {
    if (looperState.isLoadSaveModeActive()) {
        switch (SetBrowserOverlayPolicy::mapLoadSaveOverlayInputAction(actionType)) {
            case SetBrowserOverlayPolicy::LoadSaveOverlayInputAction::ScrollDown:
                displayManager.adjustLoadSaveListSelection(1);
                return;
            case SetBrowserOverlayPolicy::LoadSaveOverlayInputAction::ScrollUp:
                displayManager.adjustLoadSaveListSelection(-1);
                return;
            default:
                break;
        }
        if (SetBrowserOverlayPolicy::shouldSuppressGlobalMidiAction(actionType)) {
            return;
        }
    }

    switch (actionType) {
        case MidiButtonConfig::ActionType::TOGGLE_RECORD:
            handleToggleRecord();
            break;
        case MidiButtonConfig::ActionType::TOGGLE_PLAY:
            if (looperState.isLoadSaveModeActive()) {
                handleOverlayPlayStopShort(millis());
            } else {
                resetOverlayPlayStopPairedShort();
                handleTogglePlay();
            }
            break;
        case MidiButtonConfig::ActionType::TOGGLE_LOAD_SAVE_MODE:
            handleToggleLoadSaveMode();
            break;
        case MidiButtonConfig::ActionType::CENTER_DETAILED_WINDOW_ON_PLAYHEAD:
            handleCenterDetailedWindowOnPlayhead();
            break;
        case MidiButtonConfig::ActionType::MOVE_CURRENT_TICK:
            handleMoveCurrentTick(static_cast<int32_t>(parameter));
            break;
        case MidiButtonConfig::ActionType::SELECT_TRACK:
            handleSelectTrack(static_cast<uint8_t>(parameter));
            break;
        case MidiButtonConfig::ActionType::UNDO:
            handleUndo();
            break;
        case MidiButtonConfig::ActionType::REDO:
            handleRedo();
            break;
        case MidiButtonConfig::ActionType::UNDO_CLEAR_TRACK:
            handleUndoClearTrack();
            break;
        case MidiButtonConfig::ActionType::REDO_CLEAR_TRACK:
            handleRedoClearTrack();
            break;
        case MidiButtonConfig::ActionType::CYCLE_EDIT_MODE:
            handleCycleEditMode();
            break;
        case MidiButtonConfig::ActionType::EXIT_EDIT_MODE:
            handleExitEditMode();
            break;
        case MidiButtonConfig::ActionType::DELETE_NOTE:
            handleDeleteNote();
            break;
        case MidiButtonConfig::ActionType::CREATE_NOTE_AT_BRACKET:
            handleCreateNoteAtBracket();
            break;
        case MidiButtonConfig::ActionType::DELETE_OR_CREATE_NOTE:
            handleDeleteOrCreateNote();
            break;
        case MidiButtonConfig::ActionType::TOGGLE_LENGTH_EDIT_MODE:
            handleToggleLengthEditMode();
            break;
        case MidiButtonConfig::ActionType::CLEAR_TRACK:
            handleClearTrack();
            break;
        case MidiButtonConfig::ActionType::TOGGLE_RECORD_FOR_SLOT:
            handleToggleRecordForSlot(static_cast<uint8_t>(parameter));
            break;
        case MidiButtonConfig::ActionType::CLEAR_TRACK_FOR_SLOT:
            if (parameter < ::Config::MAX_LOOPS_PER_TRACK) {
                uint8_t tidx = trackManager.getSelectedTrackIndex();
                uint8_t slot = static_cast<uint8_t>(parameter);
                uint32_t now = getCurrentTick();
                Track& track = getCurrentTrack();

                const bool slotHasData = track.hasDataInSlot(slot);
                const uint8_t selectedSlot = trackManager.getSelectedSlotIndex(tidx);

                // Long-press semantics:
                // - If the slot is selected: clear it immediately.
                // - If the slot is not selected (and filled): queue playback to start at loop-end.
                if (slot != selectedSlot) {
                    if (slotHasData) {
                        trackManager.clearPendingSlotSwitch(tidx);
                        if (track.isPlaying()) {
                            if (clockManager.shouldQuantizeRecordStart()) {
                                trackManager.setSelectedSlotIndex(tidx, slot, SyncPlayback::No);
                                trackManager.setPendingEnabledSetReplacement(tidx, true);
                                trackManager.requestSlotSwitch(
                                    tidx, slot, SlotQuantization::LoopEnd, now);
                                logger.info("Loop %d: long-press queued at loop-end", slot + 1);
                            } else {
                                for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
                                    trackManager.setSlotEnabled(tidx, s, (s == slot));
                                    trackManager.setSlotMuted(tidx, s, false);
                                }
                                trackManager.setSelectedSlotIndex(tidx, slot);
                                const Loop& targetLoop = track.getLoop(slot);
                                track.clearQueuedPlaybackStart();
                                track.queuePlaybackStartAtGrid(
                                    static_cast<int32_t>(targetLoop.loopStartTick), now);
                                track.commitQueuedPlaybackStart(now);
                                trackManager.forceLedUpdate(now);
                                logger.info("Loop %d: long-press switch (transport stopped)",
                                            slot + 1);
                            }
                        } else {
                            trackManager.setSelectedSlotIndex(tidx, slot);
                            // No active playback loop: start immediately (and avoid a pending
                            // transition that would never commit while STOPPED/EMPTY).
                            for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
                                trackManager.setSlotEnabled(tidx, s, (s == slot));
                                trackManager.setSlotMuted(tidx, s, false);
                            }
                            track.startPlaying(now);
                            trackManager.forceLedUpdate(now);
                            logger.info("Loop %d: long-press start (not playing)", slot + 1);
                        }
                    }
                    return;
                }

                // slot == selectedSlot => clear (only when it is actually filled)
                if (!slotHasData) {
                    if (track.isRecording() || track.isOverdubbing()) {
                        trackManager.cancelSlotSelectionHold(tidx);
                    }
                    return;
                }
                handleClearTrack();
            }
            break;
        case MidiButtonConfig::ActionType::UNDO_FOR_SLOT:
            if (parameter < ::Config::MAX_LOOPS_PER_TRACK) {
                uint8_t tidx = trackManager.getSelectedTrackIndex();
                uint8_t slot = static_cast<uint8_t>(parameter);
                trackManager.setSelectedSlotIndex(tidx, slot);
                handleUndo();
            }
            break;
        case MidiButtonConfig::ActionType::REDO_FOR_SLOT:
            if (parameter < ::Config::MAX_LOOPS_PER_TRACK) {
                uint8_t tidx = trackManager.getSelectedTrackIndex();
                uint8_t slot = static_cast<uint8_t>(parameter);
                trackManager.setSelectedSlotIndex(tidx, slot);
                handleRedo();
            }
            break;

        case MidiButtonConfig::ActionType::OVERDUB_FOR_SLOT:
            if (parameter < ::Config::MAX_LOOPS_PER_TRACK) {
                uint8_t tidx = trackManager.getSelectedTrackIndex();
                uint8_t slot = static_cast<uint8_t>(parameter);
                uint32_t now = getCurrentTick();
                Track& track = getCurrentTrack();

                const bool slotHasData = track.hasDataInSlot(slot);
                if (!slotHasData) {
                    // Keep behavior consistent with short-press for empty slots.
                    handleToggleRecordForSlot(slot);
                    return;
                }

                trackManager.clearPendingSlotSwitch(tidx);
                trackManager.setSelectedSlotIndex(tidx, slot);

                // Ensure we are not in TRACK_EMPTY before starting overdub.
                if (track.isEmpty()) {
                    track.startPlaying(now);
                } else if (!track.isPlaying() && !track.isOverdubbing()) {
                    track.startPlaying(now);
                }

                trackManager.startOverdubbingTrack(tidx);
            }
            break;
        case MidiButtonConfig::ActionType::MUTE_TRACK:
            handleMuteTrack(static_cast<uint8_t>(parameter));
            break;
        case MidiButtonConfig::ActionType::SOLO_TRACK:
            handleSoloTrack(static_cast<uint8_t>(parameter));
            break;
        case MidiButtonConfig::ActionType::TOGGLE_TRANSPORT:
            handleToggleTransport();
            break;
        case MidiButtonConfig::ActionType::RESET_TO_LOOP_START:
            handleResetToLoopStart();
            break;
        default:
            // For unimplemented actions, just log them
            logger.info("Action type %d not yet implemented", static_cast<int>(actionType));
            break;
    }
}

void MidiButtonActions::handleToggleRecordForSlot(uint8_t slotIndex) {
    if (slotIndex >= ::Config::MAX_LOOPS_PER_TRACK) return;

    Track& track = getCurrentTrack();
    uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    uint32_t now = getCurrentTick();
    uint8_t previousSlot = track.getActiveLoopIndex();  // currently playing slot (defines phase)
    uint8_t selectedSlot = trackManager.getSelectedSlotIndex(trackIdx);
    const bool slotHasData = slotHasDataForPlaybackSwitch(trackIdx, slotIndex);

    // If capturing and the user selects another filled slot: commit capture first.
    if ((track.isRecording() || track.isOverdubbing()) && previousSlot != slotIndex) {
        trackManager.setSelectedSlotIndex(trackIdx, slotIndex);
        trackManager.finalizeCaptureAndSelectSlot(trackIdx, slotIndex, now);
        logger.info("Loop %d: Switched from slot %d (capture finalized)", slotIndex + 1, previousSlot + 1);
        return;
    }

    // Leaving the slot that had a pending arm/queue (e.g. empty slot 4 armed, then select slot 1):
    // clear pending so LEDs and transport state stay aligned with the newly focused slot.
    const uint8_t queuedRecSlot = trackManager.getQueuedRecordingSlot(trackIdx);
    if (queuedRecSlot != ::Config::INVALID_LOOP_SLOT && slotIndex != queuedRecSlot) {
        trackManager.cancelPendingRecordArm(trackIdx);
    }

    // Filled + currently playing: quantize active sync to grid when transport is running.
    if (track.isPlaying() && slotHasData) {
        const bool slotEnabled = trackManager.isSlotEnabled(trackIdx, slotIndex);

        if (slotIndex == selectedSlot) {
            const uint8_t playingSlot = track.getActiveLoopIndex();
            if (playingSlot != slotIndex) {
                queuePlayingSlotSwitch(trackIdx, track, slotIndex, now);
                logger.info("Loop %d: Reaffirmed playback switch", slotIndex + 1);
                return;
            }
            // Toggle mute only for this slot. Track keeps running.
            if (!slotEnabled) {
                // Safety: keep focus slot enabled.
                trackManager.setSlotEnabled(trackIdx, slotIndex, true);
                trackManager.setSlotMuted(trackIdx, slotIndex, false);
                track.resetPlaybackStateForSlot(slotIndex, now);
            } else {
                trackManager.toggleSlotMuted(trackIdx, slotIndex);
                const bool nowMuted = trackManager.isSlotMuted(trackIdx, slotIndex);
                if (!nowMuted) {
                    // Align playback indices when unmuting.
                    track.resetPlaybackStateForSlot(slotIndex, now);
                }
            }
            trackManager.forceLedUpdate(now);
            return;
        }

        // Short press on a non-selected filled slot — queue playback, never overdub.
        queuePlayingSlotSwitch(trackIdx, track, slotIndex, now);
        logger.info("Loop %d: Queued playback switch", slotIndex + 1);
        return;
    }

    // Otherwise: fall back to the existing immediate record/start toggle behavior
    // for empty slots, and immediate play/stop toggle for filled slots when not playing.
    const bool slotSelectionChanged = (slotIndex != selectedSlot);
    trackManager.clearPendingSlotSwitch(trackIdx);
    trackManager.setSelectedSlotIndex(trackIdx, slotIndex);

    const bool editFocusSlotChange =
        editManager.isLoopEditSession() || editManager.isNoteEditActive();
    if (!track.isPlaying() && !track.isRecording() && !track.isOverdubbing() && slotHasData &&
        slotSelectionChanged && editFocusSlotChange) {
        trackManager.forceLedUpdate(now);
        return;
    }

    const bool slotHasCommittedMidi = track.hasCommittedPassesInSlot(slotIndex);
    const bool slotCanArmForRecord = !slotHasCommittedMidi;

    if (track.isRecording()) {
        logger.info("Loop %d: Stop Recording", slotIndex + 1);
        trackManager.stopRecordingTrack(trackIdx);
    } else if (track.isOverdubbing()) {
        logger.info("Loop %d: Stop Overdub", slotIndex + 1);
        track.stopOverdubbing();
    } else if (track.isPlaying()) {
        // Empty slot while playing => existing short-press record/queue flow.
        if (slotCanArmForRecord) {
            if (clockManager.shouldQuantizeRecordStart() && track.isPlaying()) {
                if (trackManager.isRecordingQueued(trackIdx, slotIndex)) {
                    trackManager.clearQueuedRecordingTrack(trackIdx, slotIndex);
                    logger.info("Loop %d: Immediate punch-in", slotIndex + 1);
                    track.setAlignLoopOriginOnNextStop(true);
                    ensureActiveLoopForRecord(trackIdx, track, slotIndex);
                    trackManager.startRecordingTrack(trackIdx, now);
                } else {
                    trackManager.queueRecordingTrack(
                        trackIdx, slotIndex, refSlotPhaseForQueue(track, previousSlot));
                    logger.info("Loop %d: Queued recording (loop phase or bar)", slotIndex + 1);
                }
            } else {
                logger.info("Loop %d: Start Recording", slotIndex + 1);
                ensureActiveLoopForRecord(trackIdx, track, slotIndex);
                trackManager.startRecordingTrack(trackIdx, now);
            }
            trackManager.forceLedUpdate(now);
            return;
        }

        // Filled slot while playing: queue playback switch (e.g. data restored after focus
        // change). Short-press slot buttons must not enter track overdub — use OVERDUB_FOR_SLOT.
        logger.info("Loop %d: Queued playback switch (filled slot)", slotIndex + 1);
        queuePlayingSlotSwitch(trackIdx, track, slotIndex, now);
    } else if (slotCanArmForRecord) {
        if (handleArmedRecordPress(trackIdx)) {
            trackManager.forceLedUpdate(now);
            return;
        }
        if (clockManager.shouldQuantizeRecordStart() && track.isPlaying()) {
            if (trackManager.isRecordingQueued(trackIdx, slotIndex)) {
                trackManager.clearQueuedRecordingTrack(trackIdx, slotIndex);
                logger.info("Loop %d: Immediate punch-in", slotIndex + 1);
                track.setAlignLoopOriginOnNextStop(true);
                ensureActiveLoopForRecord(trackIdx, track, slotIndex);
                trackManager.startRecordingTrack(trackIdx, now);
            } else {
                trackManager.queueRecordingTrack(
                    trackIdx, slotIndex, refSlotPhaseForQueue(track, previousSlot));
                logger.info("Loop %d: Queued recording (loop phase or bar)", slotIndex + 1);
            }
        } else {
            logger.info("Loop %d: Start Recording", slotIndex + 1);
            ensureActiveLoopForRecord(trackIdx, track, slotIndex);
            trackManager.startRecordingTrack(trackIdx, now);
        }
        trackManager.forceLedUpdate(now);
    } else {
        logger.info("Loop %d: Toggle Play/Stop", slotIndex + 1);
        if (track.isPlaying()) {
            track.stopPlaying();
        } else {
            if (!clockManager.isTransportRunning()) {
                clockManager.toggleTransport();
            }
            if (!track.isPlaying()) {
                track.startPlaying(now);
            }
        }
        trackManager.forceLedUpdate(now);
    }
}

void MidiButtonActions::beginSlotLayerHold(uint8_t slotIndex) {
    if (slotIndex >= ::Config::MAX_LOOPS_PER_TRACK) return;
    uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    // Multi-hold selection builds a pending enabled set; it is committed on release.
    trackManager.beginSlotSelectionHold(trackIdx, slotIndex);
    logger.info("Loop %d hold: selected for next multi-slot playback", slotIndex + 1);
}

void MidiButtonActions::endSlotLayerHold(uint8_t slotIndex) {
    if (slotIndex >= ::Config::MAX_LOOPS_PER_TRACK) return;
    uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const uint32_t now = getCurrentTick();
    trackManager.endSlotSelectionHold(trackIdx, slotIndex, now);
    logger.info("Loop %d hold released", slotIndex + 1);
}

// Core actions that match your current 3-button system
void MidiButtonActions::handleToggleRecord() {
    Track& track = getCurrentTrack();
    uint8_t idx = trackManager.getSelectedTrackIndex();
    
    if (handleArmedRecordPress(idx)) {
        return;
    }

    if (track.isRecording()) {
        logger.info("MIDI Button A: Stop Recording");
        trackManager.stopRecordingTrack(idx);
        return;
    }
    if (track.isOverdubbing()) {
        logger.info("MIDI Button A: Stop Overdub");
        track.stopOverdubbing();
        return;
    }

    const uint8_t slot = trackManager.getSelectedSlotIndex(idx);
    if (!track.hasCommittedPassesInSlot(slot)) {
        handleToggleRecordForSlot(slot);
        return;
    }

    if (track.isPlaying()) {
        logger.info("MIDI Button A: Live Overdub");
        trackManager.startOverdubbingTrack(idx);
    } else {
        logger.info("MIDI Button A: Toggle Play/Stop");
        track.togglePlayStop();
    }
}

void MidiButtonActions::handleSelectTrack(uint8_t trackNumber) {
    if (trackNumber == 255) {
        // Special case: cycle to next track (matches current Button B behavior)
        uint8_t newIndex = (trackManager.getSelectedTrackIndex() + 1) % trackManager.getTrackCount();
        trackManager.setSelectedTrack(newIndex);
        logger.info("Switched to next track: %d", newIndex + 1);
    } else if (isValidTrackNumber(trackNumber)) {
        trackManager.setSelectedTrack(trackNumber);
        logger.info("Selected track %d", trackNumber + 1);
    } else {
        logger.warning("Invalid track number: %d", trackNumber);
    }
}

void MidiButtonActions::handleUndo() {
    StorageManager::restoreDeferredUndoSnapshotsBeforeUse();
    Track& track = getCurrentTrack();
    if (editManager.isNoteEditActive()) {
        if (editManager.getEditSession().undoStack.canUndo()) {
            if (editManager.sessionUndo(track)) {
                logger.info("MIDI: EditSession undo");
                return;
            }
        }
        logger.info("MIDI: No session undo available (entries=%u)",
                    static_cast<unsigned>(editManager.getEditSession().undoStack.undoCount()));
        return;
    }
    const uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const uint8_t slotIndex = trackManager.getSelectedSlotIndex(trackIdx);
    Loop& loop = track.getLoop(slotIndex);
    if (TrackUndo::canUndoForLoop(track, loop)) {
        logger.info("MIDI: Undo (entries=%d)", static_cast<int>(TrackUndo::undoDepthForLoop(track, loop)));
        TrackUndo::undoForLoop(track, loop);
        return;
    }
    logger.info("MIDI: No undo available (entries=%d)",
                static_cast<int>(TrackUndo::undoDepthForLoop(track, loop)));
}

void MidiButtonActions::handleRedo() {
    Track& track = getCurrentTrack();
    if (editManager.isNoteEditActive()) {
        if (editManager.getEditSession().undoStack.canRedo()) {
            if (editManager.sessionRedo(track)) {
                logger.info("MIDI: EditSession redo");
                return;
            }
        }
        logger.info("MIDI: No session redo available (entries=%u)",
                    static_cast<unsigned>(editManager.getEditSession().undoStack.redoCount()));
        return;
    }
    const uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const uint8_t slotIndex = trackManager.getSelectedSlotIndex(trackIdx);
    Loop& loop = track.getLoop(slotIndex);
    if (TrackUndo::canRedoForLoop(track, loop)) {
        logger.info("MIDI: Redo (entries=%d)", static_cast<int>(TrackUndo::redoDepthForLoop(track, loop)));
        TrackUndo::redoForLoop(track, loop);
        return;
    }
    logger.info("MIDI: No redo available (entries=%d)",
                static_cast<int>(TrackUndo::redoDepthForLoop(track, loop)));
}

void MidiButtonActions::handleUndoClearTrack() {
    Track& track = getCurrentTrack();
    const uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const uint8_t slotIndex = trackManager.getSelectedSlotIndex(trackIdx);
    Loop& loop = track.getLoop(slotIndex);
    if (TrackUndo::canUndoClearTrackForLoop(track, loop)) {
        logger.info("MIDI Button B: Undo Clear Track");
        TrackUndo::undoForLoop(track, loop);
    } else {
        logger.info("Nothing to undo for clear/mute.");
    }
}

void MidiButtonActions::handleRedoClearTrack() {
    Track& track = getCurrentTrack();
    const uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const uint8_t slotIndex = trackManager.getSelectedSlotIndex(trackIdx);
    Loop& loop = track.getLoop(slotIndex);
    if (TrackUndo::canRedoClearTrackForLoop(track, loop)) {
        logger.info("MIDI Button B: Redo Clear Track");
        TrackUndo::redoForLoop(track, loop);
    } else {
        logger.info("Nothing to redo for clear/mute.");
    }
}

void MidiButtonActions::handleClearTrack() {
    // Always clear the selected slot (record long-press and slot long-press when selected).
    // TODO: long press >5 s → "Clear All Slots?" overlay; release when shown; single click
    // = YES, double click = NO ("YES: Click NO: Double Click").
    uint8_t tidx = trackManager.getSelectedTrackIndex();
    Track& track = getCurrentTrack();
    const uint8_t slot = trackManager.getSelectedSlotIndex(tidx);
    const uint32_t now = getCurrentTick();

    // Clear mutates pass ownership; complete any in-flight deferred save first so
    // the writer cannot read a slot while it is being reset.
    if (StorageManager::hasDeferredSaveWork()) {
        logger.info("Clear waiting for deferred save completion");
        if (!StorageManager::saveState(looperState.getLooperState())) {
            logger.error("Clear aborted: could not complete deferred save first");
            return;
        }
    }

    if (!track.hasDataInSlot(slot)) {
        if (!track.hasAnySlotData()) {
            if (!track.isEmpty()) {
                track.setState(TRACK_EMPTY);
                logger.info("MIDI: Clear reset non-empty track state to EMPTY (no loop data)");
            } else {
                logger.debug("Clear ignored — track is empty");
            }
        } else {
            logger.debug("Clear ignored — selected slot is empty");
        }
        return;
    }

    trackManager.clearPendingSlotSwitch(tidx);
    trackManager.setPendingEnabledSetReplacement(tidx, false);
    trackManager.cancelSlotSelectionHold(tidx);

    if (track.isRecording() || track.isOverdubbing()) {
        trackManager.finalizeCaptureAndSelectSlot(tidx, slot, now);
    } else {
        trackManager.setActiveLoopIndex(tidx, slot);
    }

    TrackUndo::pushClearTrackSnapshot(track);

    trackManager.setSlotEnabled(tidx, slot, false);
    trackManager.setSlotMuted(tidx, slot, false);
    trackManager.clearQueuedRecordingTrack(tidx, slot);
    trackManager.setLayeredSlotHeld(tidx, slot, false);

    track.clear();
    StorageManager::markCurrentSetTrackDirty(tidx);
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
    logger.info("MIDI: Clear selected slot %u", static_cast<unsigned>(slot) + 1u);

    restoreAudiblePlaybackAfterSlotClear(tidx, track, now);
}

void MidiButtonActions::handleSoloTrack(uint8_t trackNumber) {
    if (!isValidTrackNumber(trackNumber)) {
        logger.warning("Solo: invalid track %u", static_cast<unsigned>(trackNumber));
        return;
    }
    trackManager.toggleSoloTrack(trackNumber);
    logger.info("Track %u: solo toggled (exclusive)", static_cast<unsigned>(trackNumber) + 1u);
    trackManager.forceLedUpdate(getCurrentTick());
}

void MidiButtonActions::handleMuteTrack(uint8_t trackNumber) {
    Track& track = getCurrentTrack();
    
    if (trackNumber == 255) {
        // Special case: mute current track (matches current Button B long press behavior)
        if (!track.hasData()) {
            logger.debug("Mute ignored — track is empty");
        } else {
            track.toggleMuteTrack();
            logger.info("MIDI Button B: Toggled mute on track %d", trackManager.getSelectedTrackIndex());
        }
    } else if (isValidTrackNumber(trackNumber)) {
        Track& targetTrack = trackManager.getTrack(trackNumber);
        targetTrack.toggleMuteTrack();
        logger.info("Track %d mute toggled", trackNumber + 1);
    }
}

void MidiButtonActions::handleCycleEditMode() {
    Track& track = getCurrentTrack();
    editManager.cycleEditSession(track);
    logger.info("MIDI Edit Mode: Short press - cycled LOOP_EDIT ↔ NOTE_EDIT");
}

void MidiButtonActions::handleCycleNoteEditType() {
    Track& track = getCurrentTrack();
    const bool wasInEditOverlay = editManager.getCurrentState() != nullptr;

    if (editManager.getEditSessionType() != EditSessionType::Note) {
        editManager.sendEditSessionChange(EditSessionType::Note);
    }

    if (!shouldCycleNoteEditTypeOnShortPress(wasInEditOverlay)) {
        if (!editManager.isNoteEditActive()) {
            editManager.openNoteEditSession(track);
        } else if (editManager.getCurrentState() == nullptr) {
            editManager.enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
        }
        logger.info("MIDI Encoder: Short press - entered note edit mode");
        return;
    }

    editManager.cycleNoteEditType(track);
}

void MidiButtonActions::handleExitEditMode() {
    Track& track = getCurrentTrack();
    // Match the exact logic from the original Encoder button long press
    logger.info("MIDI Encoder: Long press - exited edit mode");
    editManager.exitEditMode(track);
}

void MidiButtonActions::handleDeleteNote() {
    Track& track = getCurrentTrack();
    noteEditManager.deleteSelectedNote(track);
}

namespace {

constexpr uint32_t kBracketSnapWindow = 24;

bool hasNoteNearBracket(const Track& track, uint32_t selectedTick) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }
    const uint32_t bracket = selectedTick % loopLength;
    const auto& notes = track.getCachedNotes();
    for (const auto& n : notes) {
        const uint32_t noteTick = n.startTick % loopLength;
        const uint32_t dist = std::min((noteTick + loopLength - bracket) % loopLength,
                                       (bracket + loopLength - noteTick) % loopLength);
        if (dist <= kBracketSnapWindow) {
            return true;
        }
    }
    return false;
}

}  // namespace

void MidiButtonActions::handleCreateNoteAtBracket() {
    Track& track = getCurrentTrack();
    if (editManager.getCurrentState() == nullptr) {
        logger.info("Create note ignored (not in edit mode)");
        return;
    }
    if (!editManager.isNoteEditActive()) {
        editManager.openNoteEditSession(track);
    }
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    uint32_t selectedTick = editManager.getSelectedTick() % loopLength;
    if (hasNoteNearBracket(track, selectedTick)) {
        logger.info("Create note ignored (note at bracket)");
        return;
    }

    editManager.setSelectedNoteIdx(-1);
    editManager.beginGeometryMutation(track, NoteEditKind::Add, false);
    const std::array<MidiEvent, 2> created =
        EditSelectNoteState::createNoteAtTick(track, selectedTick);
    EditPass add{};
    add.passType = EditPassType::Note;
    add.actionType = EditActionType::Create;
    add.propertyType = EditPropertyType::None;
    add.addedEvents.push_back(created[0]);
    add.addedEvents.push_back(created[1]);
    const EditPassId id = editManager.commitEditAction(track, EditPassVec{add});
    if (id == kInvalidEditPassId) {
        logger.info("Create note failed (edit session commit rejected)");
        return;
    }
    editManager.setSelectedTick(selectedTick);
    track.invalidateCaches();
    editManager.selectNoteAtBracket(track, selectedTick);
}

void MidiButtonActions::handleDeleteOrCreateNote() {
    Track& track = getCurrentTrack();
    if (editManager.getCurrentState() == nullptr) {
        logger.info("NOTELEN double: ignored (not in edit mode)");
        return;
    }
    if (editManager.getSelectedNoteIdx() >= 0 ||
        editManager.getLastFader1SelectNoteId() != kInvalidNoteId) {
        logger.info("NOTELEN double: delete selected note");
        handleDeleteNote();
        return;
    }
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }
    const uint32_t selectedTick = editManager.getSelectedTick() % loopLength;
    if (hasNoteNearBracket(track, selectedTick)) {
        logger.info("NOTELEN double: ignored (note at bracket, none selected)");
        return;
    }
    logger.info("NOTELEN double: create note at bracket");
    handleCreateNoteAtBracket();
}

void MidiButtonActions::handleResetToLoopStart() {
    clockManager.resetToLoopStart();
}

void MidiButtonActions::handleToggleTransport() {
    const uint8_t transportNote = 39;  // D#2
    bool wasRunning = clockManager.isTransportRunning();
    if (wasRunning) {
        midiHandler.sendLedFeedbackNoteOff(transportNote);
    }
    clockManager.toggleTransport();
    if (!wasRunning) {
        midiHandler.sendLedFeedbackNoteOn(transportNote, 127);
    }
    logger.info("Transport LED: sent %s on ch%d note%d",
                !wasRunning ? "NoteOn" : "NoteOff",
                MidiButtonConfig::Channels::TRANSPORT,
                transportNote);
}

void MidiButtonActions::syncTransportLed() {
    const uint8_t transportNote = 39;  // D#2 - same as handleToggleTransport
    if (clockManager.isTransportRunning()) {
        midiHandler.sendLedFeedbackNoteOn(transportNote, 127);
    } else {
        midiHandler.sendLedFeedbackNoteOff(transportNote);
    }
}

// Stubbed implementations for future expansion
void MidiButtonActions::handleTogglePlay() {
    Track& track = getCurrentTrack();
    track.togglePlayStop();
    logger.info("Track play/stop toggled");
}

void MidiButtonActions::handleToggleLoadSaveMode() {
    resetOverlayPlayStopPairedShort();
    if (looperState.isLoadSaveModeActive()) {
        looperState.exitLoadSaveMode();
        logger.info("Load/save mode exited");
        return;
    }
    looperState.enterLoadSaveMode();
    logger.info("Load/save mode entered");
}

void MidiButtonActions::handleCenterDetailedWindowOnPlayhead() {
    if (looperState.isLoadSaveModeActive()) {
        return;
    }
    Track& track = getCurrentTrack();
    const uint8_t trackIdx = trackManager.getSelectedTrackIndex();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackIdx);
    const uint32_t now = getCurrentTick();
    displayManager.centerDetailedWindowOnPlayhead(track, displaySlot, now);
    logger.info("Detailed window centered on playhead");
}

void MidiButtonActions::handleMoveCurrentTick(int32_t tickOffset) {
    Track& track = getCurrentTrack();
    uint32_t currentTick = getCurrentTick();
    uint32_t loopLength = track.getLoopLength();
    
    // Calculate new position with wrapping
    int64_t newTickSigned = static_cast<int64_t>(currentTick) + tickOffset;
    uint32_t newTick;
    
    if (newTickSigned < 0) {
        // Wrap backwards
        newTick = loopLength + (newTickSigned % static_cast<int64_t>(loopLength));
    } else {
        // Normal forward or wrap forward
        newTick = static_cast<uint32_t>(newTickSigned) % loopLength;
    }
    
    // Use EditManager to set the bracket tick position
    editManager.setSelectedTick(newTick);
    logger.info("Moved to tick %d (offset: %d)", newTick, tickOffset);
}

// Helper methods
Track& MidiButtonActions::getCurrentTrack() {
    return trackManager.getSelectedTrack();
}

uint32_t MidiButtonActions::getCurrentTick() {
    return clockManager.getCurrentTick();
}

void MidiButtonActions::handleToggleLengthEditMode() {
    noteEditManager.toggleLengthEditingMode();
}

bool MidiButtonActions::isValidTrackNumber(uint8_t trackNumber) {
    return trackNumber < trackManager.getTrackCount();
} 