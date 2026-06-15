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
#include "Loop.h"

namespace {
uint8_t refSlotPhaseForQueue(const Track& track, uint8_t previousSlot) {
  if (previousSlot >= ::Config::MAX_LOOPS_PER_TRACK) return ::Config::INVALID_LOOP_SLOT;
  const Loop& rl = track.getLoop(previousSlot);
  return (rl.loopLengthTicks > 0) ? previousSlot : ::Config::INVALID_LOOP_SLOT;
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
    switch (actionType) {
        case MidiButtonConfig::ActionType::TOGGLE_RECORD:
            handleToggleRecord();
            break;
        case MidiButtonConfig::ActionType::TOGGLE_PLAY:
            handleTogglePlay();
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
                        trackManager.setSelectedSlotIndex(tidx, slot);
                        trackManager.clearPendingSlotSwitch(tidx);
                        if (track.isPlaying()) {
                            // Selecting a single slot: replace enabled set when the switch commits.
                            trackManager.setPendingEnabledSetReplacement(tidx, true);
                            trackManager.requestSlotSwitch(
                                tidx, slot, SlotQuantization::LoopEnd, now);
                            logger.info("Loop %d: long-press queued at loop-end", slot + 1);
                        } else {
                            // No active playback loop: start immediately (and avoid a pending
                            // transition that would never commit while STOPPED/EMPTY).
                            for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
                                trackManager.setSlotEnabled(tidx, s, (s == slot));
                                trackManager.setSlotMuted(tidx, s, false);
                            }
                            trackManager.setActiveLoopIndex(tidx, slot);
                            track.startPlaying(now);
                            trackManager.forceLedUpdate(now);
                            logger.info("Loop %d: long-press start (not playing)", slot + 1);
                        }
                    }
                    return;
                }

                // slot == selectedSlot => clear (only when it is actually filled)
                if (!slotHasData) return;

                // Cancels any pending quantized slot switching.
                trackManager.clearPendingSlotSwitch(tidx);
                trackManager.setPendingEnabledSetReplacement(tidx, false);
                trackManager.cancelSlotSelectionHold(tidx);

                // If capturing, finalize before clearing to avoid corrupting state.
                if (track.isRecording() || track.isOverdubbing()) {
                    trackManager.finalizeCaptureAndSelectSlot(tidx, slot, now);
                } else {
                    trackManager.setActiveLoopIndex(tidx, slot);
                }

                // Revert to codebase "clear" semantics (delete content using Track::clear()).
                // Update slot-level flags first so multi-slot state matches deletion.
                trackManager.setSlotEnabled(tidx, slot, false);
                trackManager.setSlotMuted(tidx, slot, false);
                trackManager.clearQueuedRecordingTrack(tidx, slot);
                trackManager.setLayeredSlotHeld(tidx, slot, false);

                // Active loop is the cleared slot; reuse existing clear/undo/storage logic.
                handleClearTrack();

                // Track::clear() sets TrackState to TRACK_EMPTY, which would silence all
                // other enabled slots. Restore playback if any other enabled+unmuted slot
                // still has data.
                bool foundAudible = false;
                uint8_t newActiveSlot = 0;
                for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
                    if (trackManager.isSlotEnabled(tidx, s) &&
                        !trackManager.isSlotMuted(tidx, s) &&
                        track.hasDataInSlot(s)) {
                        foundAudible = true;
                        newActiveSlot = s;
                        break;
                    }
                }

                if (foundAudible) {
                    trackManager.setActiveLoopIndex(tidx, newActiveSlot);
                    // Reset playback indices for all enabled+unmuted slots.
                    for (uint8_t s = 0; s < ::Config::MAX_LOOPS_PER_TRACK; ++s) {
                        if (trackManager.isSlotEnabled(tidx, s) &&
                            !trackManager.isSlotMuted(tidx, s) &&
                            track.hasDataInSlot(s)) {
                            track.resetPlaybackStateForSlot(s, now);
                        }
                    }
                    track.startPlaying(now);
                } else {
                    // No enabled audible slots remain: ensure silence.
                    track.sendAllNotesOff();
                }
            }
            break;
        case MidiButtonConfig::ActionType::UNDO_FOR_SLOT:
            if (parameter < ::Config::MAX_LOOPS_PER_TRACK) {
                uint8_t tidx = trackManager.getSelectedTrackIndex();
                uint8_t slot = static_cast<uint8_t>(parameter);
                trackManager.setSelectedSlotIndex(tidx, slot);
                trackManager.setActiveLoopIndex(tidx, slot);
                handleUndo();
            }
            break;
        case MidiButtonConfig::ActionType::REDO_FOR_SLOT:
            if (parameter < ::Config::MAX_LOOPS_PER_TRACK) {
                uint8_t tidx = trackManager.getSelectedTrackIndex();
                uint8_t slot = static_cast<uint8_t>(parameter);
                trackManager.setSelectedSlotIndex(tidx, slot);
                trackManager.setActiveLoopIndex(tidx, slot);
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
                trackManager.setActiveLoopIndex(tidx, slot);

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
    const bool slotHasData = track.hasDataInSlot(slotIndex);

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

    // Filled + currently playing: schedule quantized switch when not selected.
    if (track.isPlaying() && slotHasData) {
        const uint8_t enabledCount = trackManager.countEnabledSlots(trackIdx);
        const bool slotEnabled = trackManager.isSlotEnabled(trackIdx, slotIndex);

        if (slotIndex == selectedSlot) {
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

        // Short press on a non-selected filled slot:
        // - Always move focus/capture target.
        trackManager.setSelectedSlotIndex(trackIdx, slotIndex);

        // Multi-slot mode: keep the enabled set and mute state as-is.
        // A non-selected short press should only move focus/capture target.
        if (enabledCount > 1) {
            // Schedule active-loop focus switch for capture/overdub on next 16th boundary.
            trackManager.requestSlotSwitch(trackIdx, slotIndex, SlotQuantization::NextGrid, now);
            return;
        }

        // Single-slot mode: switch enabled set to the pressed slot at next 16th.
        if (!slotEnabled) {
            trackManager.setPendingEnabledSetReplacement(trackIdx, true);
        }
        trackManager.requestSlotSwitch(trackIdx, slotIndex, SlotQuantization::NextGrid, now);
        return;
    }

    // Otherwise: fall back to the existing immediate record/start toggle behavior
    // for empty slots, and immediate play/stop toggle for filled slots when not playing.
    trackManager.clearPendingSlotSwitch(trackIdx);
    trackManager.setSelectedSlotIndex(trackIdx, slotIndex);
    trackManager.setActiveLoopIndex(trackIdx, slotIndex);

    if (track.isRecording()) {
        logger.info("Loop %d: Stop Recording", slotIndex + 1);
        trackManager.stopRecordingTrack(trackIdx);
        track.startPlaying(now);
    } else if (track.isOverdubbing()) {
        logger.info("Loop %d: Stop Overdub", slotIndex + 1);
        track.stopOverdubbing();
    } else if (track.isPlaying()) {
        // Empty slot while playing => existing short-press record/queue flow.
        if (!slotHasData) {
            if (clockManager.shouldQuantizeRecordStart() && track.isPlaying()) {
                if (trackManager.isRecordingQueued(trackIdx, slotIndex)) {
                    trackManager.clearQueuedRecordingTrack(trackIdx, slotIndex);
                    logger.info("Loop %d: Immediate punch-in", slotIndex + 1);
                    track.setAlignLoopOriginOnNextStop(true);
                    trackManager.startRecordingTrack(trackIdx, now);
                } else {
                    trackManager.queueRecordingTrack(
                        trackIdx, slotIndex, refSlotPhaseForQueue(track, previousSlot));
                    logger.info("Loop %d: Queued recording (loop phase or bar)", slotIndex + 1);
                }
            } else {
                logger.info("Loop %d: Start Recording", slotIndex + 1);
                trackManager.startRecordingTrack(trackIdx, now);
            }
            trackManager.forceLedUpdate(now);
            return;
        }

        // This branch is reached for "filled + playing + slotHasData == true" only when
        // `slotIndex == selectedSlot` is handled above, so keep it as a safety fallback.
        logger.info("Loop %d: Live Overdub (safety fallback)", slotIndex + 1);
        trackManager.startOverdubbingTrack(trackIdx);
    } else if (!slotHasData) {
        if (handleArmedRecordPress(trackIdx)) {
            trackManager.forceLedUpdate(now);
            return;
        }
        if (clockManager.shouldQuantizeRecordStart() && track.isPlaying()) {
            if (trackManager.isRecordingQueued(trackIdx, slotIndex)) {
                trackManager.clearQueuedRecordingTrack(trackIdx, slotIndex);
                logger.info("Loop %d: Immediate punch-in", slotIndex + 1);
                track.setAlignLoopOriginOnNextStop(true);
                trackManager.startRecordingTrack(trackIdx, now);
            } else {
                trackManager.queueRecordingTrack(
                    trackIdx, slotIndex, refSlotPhaseForQueue(track, previousSlot));
                logger.info("Loop %d: Queued recording (loop phase or bar)", slotIndex + 1);
            }
        } else {
            logger.info("Loop %d: Start Recording", slotIndex + 1);
            trackManager.startRecordingTrack(trackIdx, now);
        }
        trackManager.forceLedUpdate(now);
    } else {
        logger.info("Loop %d: Toggle Play/Stop", slotIndex + 1);
        track.togglePlayStop();
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
    uint32_t now = getCurrentTick();
    
    if (handleArmedRecordPress(idx)) {
        return;
    }

    // Match the exact logic from the original Button A short press
    if (track.isEmpty()) {
        logger.info("MIDI Button A: Start Recording");
        trackManager.startRecordingTrack(idx, now);
    } else if (track.isRecording()) {
        logger.info("MIDI Button A: Stop Recording");
        trackManager.stopRecordingTrack(idx);
        track.startPlaying(now);
    } else if (track.isOverdubbing()) {
        logger.info("MIDI Button A: Stop Overdub");
        track.stopOverdubbing();
    } else if (track.isPlaying()) {
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
    Track& track = getCurrentTrack();
    if (TrackUndo::canUndo(track)) {
        logger.info("MIDI: Undo overdub (snapshots=%d)", TrackUndo::getUndoCount(track));
        TrackUndo::undoOverdub(track);
        return;
    }
    if (TrackUndo::canUndoClearTrack(track)) {
        logger.info("MIDI: Undo clear slot");
        TrackUndo::undoClearTrack(track);
        return;
    }
    logger.info("MIDI: No undo available (overdub=%d)", TrackUndo::getUndoCount(track));
}

void MidiButtonActions::handleRedo() {
    Track& track = getCurrentTrack();
    if (TrackUndo::canRedo(track)) {
        logger.info("MIDI: Redo overdub (redo_snapshots=%d)", TrackUndo::getRedoCount(track));
        TrackUndo::redoOverdub(track);
        return;
    }
    if (TrackUndo::canRedoClearTrack(track)) {
        logger.info("MIDI: Redo clear slot");
        TrackUndo::redoClearTrack(track);
        return;
    }
    logger.info("MIDI: No redo available (overdub redo=%d)", TrackUndo::getRedoCount(track));
}

void MidiButtonActions::handleUndoClearTrack() {
    Track& track = getCurrentTrack();
    if (TrackUndo::canUndoClearTrack(track)) {
        logger.info("MIDI Button B: Undo Clear Track");
        TrackUndo::undoClearTrack(track);
    } else {
        logger.info("Nothing to undo for clear/mute.");
    }
}

void MidiButtonActions::handleRedoClearTrack() {
    Track& track = getCurrentTrack();
    if (TrackUndo::canRedoClearTrack(track)) {
        logger.info("MIDI Button B: Redo Clear Track");
        TrackUndo::redoClearTrack(track);
    } else {
        logger.info("Nothing to redo for clear/mute.");
    }
}

void MidiButtonActions::handleClearTrack() {
    Track& track = getCurrentTrack();

    if (!track.hasData()) {
        logger.debug("Clear ignored — track is empty");
    } else {
        TrackUndo::pushClearTrackSnapshot(track);
        track.clear();
        StorageManager::saveState(looperState.getLooperState());
        logger.info("MIDI: Clear Track");
    }
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
    // Call the original NoteEditManager's cycleEditMode method
    noteEditManager.cycleEditMode(track);
}

void MidiButtonActions::handleExitEditMode() {
    Track& track = getCurrentTrack();
    // Match the exact logic from the original Encoder button long press
    logger.info("MIDI Encoder: Long press - exited edit mode");
    editManager.exitEditMode(track);
}

void MidiButtonActions::handleDeleteNote() {
    Track& track = getCurrentTrack();
    // Call the original NoteEditManager's deleteSelectedNote method
    noteEditManager.deleteSelectedNote(track);
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
    editManager.setBracketTick(newTick);
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