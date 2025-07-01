//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiButtonActions.h"
#include "Globals.h"
#include "TrackManager.h"
#include "EditManager.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackUndo.h"
#include "Logger.h"
#include "NoteEditManager.h"

// Global instances (matching your current system)
extern TrackManager trackManager;
extern EditManager editManager;
extern ClockManager clockManager;
extern MidiHandler midiHandler;
extern Logger logger;
extern NoteEditManager noteEditManager;

// Global instance
MidiButtonActions midiButtonActions;

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
        case MidiButtonConfig::ActionType::MUTE_TRACK:
            handleMuteTrack(static_cast<uint8_t>(parameter));
            break;
        default:
            // For unimplemented actions, just log them
            logger.info("Action type %d not yet implemented", static_cast<int>(actionType));
            break;
    }
}

// Core actions that match your current 3-button system
void MidiButtonActions::handleToggleRecord() {
    Track& track = getCurrentTrack();
    uint8_t idx = trackManager.getSelectedTrackIndex();
    uint32_t now = getCurrentTick();
    
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
    // Match the exact logic from the original Button A double press
    if (TrackUndo::canUndo(track)) {
        logger.info("MIDI Button A: Undo Overdub (snapshots=%d)", TrackUndo::getUndoCount(track));
        TrackUndo::undoOverdub(track);
    } else {
        logger.info("MIDI Button A: No undo snapshots available (count=%d)", TrackUndo::getUndoCount(track));
    }
}

void MidiButtonActions::handleRedo() {
    Track& track = getCurrentTrack();
    // Match the exact logic from the original Button A triple press
    if (TrackUndo::canRedo(track)) {
        logger.info("MIDI Button A: Redo Overdub (redo_snapshots=%d)", TrackUndo::getRedoCount(track));
        TrackUndo::redoOverdub(track);
    } else {
        logger.info("MIDI Button A: No redo snapshots available (count=%d)", TrackUndo::getRedoCount(track));
    }
}

void MidiButtonActions::handleUndoClearTrack() {
    Track& track = getCurrentTrack();
    // Match the exact logic from the original Button B double press
    if (TrackUndo::canUndoClearTrack(track)) {
        logger.info("MIDI Button B: Undo Clear Track");
        TrackUndo::undoClearTrack(track);
    } else {
        logger.info("Nothing to undo for clear/mute.");
    }
}

void MidiButtonActions::handleRedoClearTrack() {
    Track& track = getCurrentTrack();
    // Match the exact logic from the original Button B triple press
    if (TrackUndo::canRedoClearTrack(track)) {
        logger.info("MIDI Button B: Redo Clear Track");
        TrackUndo::redoClearTrack(track);
    } else {
        logger.info("Nothing to redo for clear/mute.");
    }
}

void MidiButtonActions::handleClearTrack() {
    Track& track = getCurrentTrack();
    
    // Match the exact logic from the original Button A long press
    if (!track.hasData()) {
        logger.debug("Clear ignored — track is empty");
    } else {
        track.clear();
        logger.info("MIDI Button A: Clear Track");
    }
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