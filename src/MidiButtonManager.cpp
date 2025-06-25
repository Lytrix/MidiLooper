//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "MidiButtonManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "EditManager.h"
#include "EditStates/EditLengthNoteState.h"
#include "NoteUtils.h"

MidiButtonManager midiButtonManager;

MidiButtonManager::MidiButtonManager() {
    buttonStates.clear();
}

void MidiButtonManager::setup() {
    // Initialize button states for our 3 MIDI buttons
    buttonStates.resize(3);
    
    buttonStates[MIDI_BUTTON_A].noteNumber = NOTE_C2;
    buttonStates[MIDI_BUTTON_B].noteNumber = NOTE_C2_SHARP;
    buttonStates[MIDI_BUTTON_ENCODER].noteNumber = NOTE_D2;
    
    for (auto& state : buttonStates) {
        state.isPressed = false;
        state.pressStartTime = 0;
        state.lastTapTime = 0;
        state.pendingShortPress = false;
        state.shortPressExpireTime = 0;
    }
    
    logger.info("MidiButtonManager setup complete. Monitoring USB Host Ch1: C2, D2, E2");
}

bool MidiButtonManager::isValidNote(uint8_t note) {
    return (note == NOTE_C2 || note == NOTE_C2_SHARP || note == NOTE_D2);
}

MidiButtonId MidiButtonManager::getNoteButtonId(uint8_t note) {
    switch (note) {
        case NOTE_C2: return MIDI_BUTTON_A;
        case NOTE_C2_SHARP: return MIDI_BUTTON_B;
        case NOTE_D2: return MIDI_BUTTON_ENCODER;
        default: return MIDI_BUTTON_A; // Should never happen if isValidNote is checked
    }
}

void MidiButtonManager::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    // Only process channel 1 and our specific notes
    if (channel != MIDI_CHANNEL || !isValidNote(note)) {
        return;
    }
    
    MidiButtonId buttonId = getNoteButtonId(note);
    MidiButtonState& state = buttonStates[buttonId];
    uint32_t now = millis();
    
    logger.log(CAT_MIDI, LOG_DEBUG, "MIDI Button %s: Note %d Ch%d Vel%d", 
               isNoteOn ? "ON" : "OFF", note, channel, velocity);
    
    if (isNoteOn && velocity > 0) {
        // Note On - Button Press
        if (!state.isPressed) {
            state.isPressed = true;
            state.pressStartTime = now;
            logger.log(CAT_BUTTON, LOG_DEBUG, "MIDI Button %d pressed (Note %d)", buttonId, note);
        }
    } else {
        // Note Off - Button Release
        if (state.isPressed) {
            state.isPressed = false;
            uint32_t duration = now - state.pressStartTime;
            
            logger.log(CAT_BUTTON, LOG_DEBUG, "MIDI Button %d released (Note %d), duration: %dms", 
                       buttonId, note, duration);
            
            if (duration >= LONG_PRESS_TIME) {
                // Long press
                handleButton(buttonId, MIDI_BUTTON_LONG_PRESS);
            } else {
                // Check for double press
                if (now - state.lastTapTime <= DOUBLE_TAP_WINDOW) {
                    // Second tap within window - double press
                    state.lastTapTime = 0;
                    state.pendingShortPress = false;
                    handleButton(buttonId, MIDI_BUTTON_DOUBLE_PRESS);
                } else {
                    // First tap - delay decision for potential double press
                    state.lastTapTime = now;
                    state.pendingShortPress = true;
                    state.shortPressExpireTime = now + DOUBLE_TAP_WINDOW;
                }
            }
        }
    }
}

void MidiButtonManager::update() {
    uint32_t now = millis();
    
    // Process pending short presses that have timed out
    for (size_t i = 0; i < buttonStates.size(); ++i) {
        MidiButtonState& state = buttonStates[i];
        
        if (state.pendingShortPress && now >= state.shortPressExpireTime) {
            state.pendingShortPress = false;
            handleButton(static_cast<MidiButtonId>(i), MIDI_BUTTON_SHORT_PRESS);
        }
    }
    
    // --- Encoder button hold/release logic for pitch edit mode ---
    bool encoderButtonHeld = false;
    if (buttonStates.size() > MIDI_BUTTON_ENCODER) {
        encoderButtonHeld = buttonStates[MIDI_BUTTON_ENCODER].isPressed;
    }
    
    if (encoderButtonHeld && !wasEncoderButtonHeld) {
        // Just started holding
        encoderButtonHoldStart = now;
    }
    
    if (encoderButtonHeld && (now - encoderButtonHoldStart >= ENCODER_HOLD_DELAY) &&
        (editManager.getCurrentState() == editManager.getNoteState() ||
         editManager.getCurrentState() == editManager.getStartNoteState())) {
        if (!pitchEditActive) {
            editManager.enterPitchEditMode(trackManager.getSelectedTrack());
            pitchEditActive = true;
            logger.info("MIDI Encoder Button: Entered Pitch Edit Mode");
        }
    }
    
    if (!encoderButtonHeld && wasEncoderButtonHeld) {
        // Just released
        if (editManager.getCurrentState() == editManager.getPitchNoteState()) {
            editManager.exitPitchEditMode(trackManager.getSelectedTrack());
            logger.info("MIDI Encoder Button: Exited Pitch Edit Mode");
        }
        encoderButtonHoldStart = 0;
        pitchEditActive = false;
    }
    
    wasEncoderButtonHeld = encoderButtonHeld;
}

void MidiButtonManager::handleButton(MidiButtonId button, MidiButtonAction action) {
    auto& track = trackManager.getSelectedTrack();
    uint8_t idx = trackManager.getSelectedTrackIndex();
    uint32_t now = clockManager.getCurrentTick();
    
    const char* actionName = "";
    switch (action) {
        case MIDI_BUTTON_SHORT_PRESS: actionName = "SHORT"; break;
        case MIDI_BUTTON_DOUBLE_PRESS: actionName = "DOUBLE"; break;
        case MIDI_BUTTON_LONG_PRESS: actionName = "LONG"; break;
        default: actionName = "NONE"; break;
    }
    
    logger.log(CAT_BUTTON, LOG_DEBUG, "MIDI Button %d action: %s", button, actionName);
    
    switch (button) {
        case MIDI_BUTTON_A:
            switch (action) {
                case MIDI_BUTTON_DOUBLE_PRESS:
                    logger.info("MIDI Button A: Double press detected");
                    if (TrackUndo::canUndo(track)) {
                        logger.info("MIDI Button A: Undo Overdub (snapshots=%d)", TrackUndo::getUndoCount(track));
                        TrackUndo::undoOverdub(track);
                    } else {
                        logger.info("MIDI Button A: No undo snapshots available (count=%d)", TrackUndo::getUndoCount(track));
                    }
                    break;
                case MIDI_BUTTON_SHORT_PRESS:
                    if (track.isEmpty()) {
                        logger.info("MIDI Button A: Start Recording");
                        looperState.requestStateTransition(LOOPER_RECORDING);
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
                    break;
                case MIDI_BUTTON_LONG_PRESS:
                    if (!track.hasData()) {
                        logger.debug("Clear ignored — track is empty");
                    } else {
                        TrackUndo::pushClearTrackSnapshot(track);
                        track.clear();
                        StorageManager::saveState(looperState.getLooperState());
                        logger.info("MIDI Button A: Clear Track");
                    }
                    break;
                default:
                    break;
            }
            break;
            
        case MIDI_BUTTON_B:
            switch (action) {
                case MIDI_BUTTON_DOUBLE_PRESS:
                    if (TrackUndo::canUndoClearTrack(track)) {
                        logger.info("MIDI Button B: Undo Clear Track");
                        TrackUndo::undoClearTrack(track);
                        StorageManager::saveState(looperState.getLooperState());
                    } else {
                        logger.info("Nothing to undo for clear/mute.");
                    }                
                    break;
                case MIDI_BUTTON_SHORT_PRESS: {
                    uint8_t newIndex = (trackManager.getSelectedTrackIndex() + 1)
                                       % trackManager.getTrackCount();
                    trackManager.setSelectedTrack(newIndex);
                    logger.info("MIDI Button B: Switched to track %d", newIndex);
                    break;
                }
                case MIDI_BUTTON_LONG_PRESS:
                    if (!track.hasData()) {
                        logger.debug("Mute ignored — track is empty");
                    } else {
                        track.toggleMuteTrack();            
                        logger.info("MIDI Button B: Toggled mute on track %d", 
                                   trackManager.getSelectedTrackIndex());
                    }
                    break;
                default:
                    break;
            }
            break;
            
        case MIDI_BUTTON_ENCODER:
            switch (action) {
                case MIDI_BUTTON_SHORT_PRESS:
                    // Cycle through edit modes: Start → Length → Pitch → Exit
                    cycleEditMode(track);
                    break;
                case MIDI_BUTTON_DOUBLE_PRESS:
                    // Delete selected note
                    deleteSelectedNote(track);
                    break;
                case MIDI_BUTTON_LONG_PRESS:
                    // Exit edit mode completely
                    if (currentEditMode != EDIT_MODE_NONE) {
                        currentEditMode = EDIT_MODE_NONE;
                        editManager.exitEditMode(track);
                        logger.info("MIDI Encoder: Long press - exited edit mode");
                    }
                    break;
                default:
                    break;
            }
            break;
    }
}

void MidiButtonManager::handleMidiEncoder(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    // Only handle encoder CC on the specified channel and CC number
    if (channel != ENCODER_CC_CHANNEL || ccNumber != ENCODER_CC_NUMBER) {
        return;
    }
    
    // Determine encoder direction based on CC value
    int delta = 0;
    if (value == ENCODER_UP_VALUE) {
        delta = 1;   // Clockwise/Up
    } else if (value == ENCODER_DOWN_VALUE) {
        delta = -1;  // Counter-clockwise/Down
    } else {
        // Ignore other CC values
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "MIDI Encoder: CC ch=%d num=%d val=%d delta=%d", 
               channel, ccNumber, value, delta);
    
    processEncoderMovement(delta);
}

void MidiButtonManager::processEncoderMovement(int rawDelta) {
    if (rawDelta == 0) return;
    
    uint32_t now = millis();
    uint32_t interval = now - lastEncoderTime;
    lastEncoderTime = now;
    
    // Apply acceleration based on timing and current edit state
    int accel = 1;
    if (editManager.getCurrentState() == editManager.getStartNoteState()) {
        if (interval < 25) accel = 24;
        else if (interval < 50) accel = 8;
        else if (interval < 100) accel = 4;
    } else if (editManager.getCurrentState() == editManager.getLengthNoteState()) {
        // Length editing: use moderate acceleration
        if (interval < 25) accel = 8;
        else if (interval < 50) accel = 4;
        else if (interval < 100) accel = 2;
    } else if (editManager.getCurrentState() == editManager.getPitchNoteState()) {
        // Pitch editing: slower acceleration for precision
        if (interval < 50) accel = 4;
        else if (interval < 75) accel = 3;
        else if (interval < 100) accel = 2;
    } else {
        // Default edit mode acceleration
        if (interval < 50) accel = 4;
        else if (interval < 75) accel = 3;
        else if (interval < 100) accel = 2;
    }
    
    int finalDelta = rawDelta * accel;
    
    if (editManager.getCurrentState() != nullptr) {
        // In edit mode: encoder changes value
        editManager.onEncoderTurn(trackManager.getSelectedTrack(), finalDelta);
        logger.log(CAT_MIDI, LOG_DEBUG, "[EDIT] MIDI Encoder value change: %d (accel=%d, raw=%d, state=%s)", 
                   finalDelta, accel, rawDelta, editManager.getCurrentState()->getName());
    } else {
        // Not in edit mode - just log for debug
        logger.log(CAT_MIDI, LOG_DEBUG, "MIDI Encoder delta: %d (not in edit mode)", finalDelta);
    }
    
    // Update encoder position for consistency
    midiEncoderPosition += rawDelta;
}

void MidiButtonManager::cycleEditMode(Track& track) {
    logger.log(CAT_BUTTON, LOG_DEBUG, "cycleEditMode: current mode = %d", currentEditMode);
    
    switch (currentEditMode) {
        case EDIT_MODE_NONE: {
            // First click: Enter select mode
            currentEditMode = EDIT_MODE_SELECT;
            uint32_t currentTick = clockManager.getCurrentTick();
            editManager.setState(editManager.getSelectNoteState(), track, currentTick);
            logger.info("MIDI Encoder: Entered SELECT mode (tick=%lu)", currentTick);
            break;
        }
            
        case EDIT_MODE_SELECT: {
            // Second click: Let the select state handle this (create note or enter start mode)
            editManager.onButtonPress(track);
            if (editManager.getCurrentState() == editManager.getStartNoteState()) {
                currentEditMode = EDIT_MODE_START;
                logger.info("MIDI Encoder: Entered START note edit mode");
            }
            break;
        }
            
        case EDIT_MODE_START: {
            // Third click: Enter length edit mode
            currentEditMode = EDIT_MODE_LENGTH;
            editManager.setState(editManager.getLengthNoteState(), track, editManager.getBracketTick());
            logger.info("MIDI Encoder: Entered LENGTH edit mode (bracket=%lu)", editManager.getBracketTick());
            break;
        }
            
        case EDIT_MODE_LENGTH: {
            // Fourth click: Enter pitch edit mode
            currentEditMode = EDIT_MODE_PITCH;
            editManager.setState(editManager.getPitchNoteState(), track, editManager.getBracketTick());
            logger.info("MIDI Encoder: Entered PITCH edit mode (bracket=%lu)", editManager.getBracketTick());
            break;
        }
            
        case EDIT_MODE_PITCH: {
            // Fifth click: Exit edit mode completely
            currentEditMode = EDIT_MODE_NONE;
            editManager.exitEditMode(track);
            logger.info("MIDI Encoder: Exited edit mode");
            break;
        }
    }
    
    logger.log(CAT_BUTTON, LOG_DEBUG, "cycleEditMode: new mode = %d, edit state = %s", 
               currentEditMode, editManager.getCurrentState() ? editManager.getCurrentState()->getName() : "NULL");
}

void MidiButtonManager::enterNextEditMode(Track& track) {
    cycleEditMode(track);
}

void MidiButtonManager::deleteSelectedNote(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Reconstruct notes to find the selected one
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    if (editManager.getSelectedNoteIdx() >= (int)notes.size()) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return;
    }
    
    auto& selectedNote = notes[editManager.getSelectedNoteIdx()];
    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t noteEnd = selectedNote.endTick;
    
    logger.info("MIDI Encoder: Deleting note pitch=%d, start=%lu, end=%lu", 
                notePitch, noteStart, noteEnd);
    
    // Push undo snapshot before deletion
    TrackUndo::pushUndoSnapshot(track);
    
    // Remove NoteOn and NoteOff events
    auto it = midiEvents.begin();
    int deletedCount = 0;
    while (it != midiEvents.end()) {
        bool matchOn = (it->type == midi::NoteOn && it->data.noteData.velocity > 0 && 
                       it->data.noteData.note == notePitch && it->tick == noteStart);
        bool matchOff = ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                        it->data.noteData.note == notePitch && it->tick == noteEnd);
        
        if (matchOn || matchOff) {
            it = midiEvents.erase(it);
            deletedCount++;
        } else {
            ++it;
        }
    }
    
    logger.info("MIDI Encoder: Deleted %d MIDI events for note", deletedCount);
    
    // Return to SELECT mode after deletion
    currentEditMode = EDIT_MODE_SELECT;
    editManager.setState(editManager.getSelectNoteState(), track, editManager.getBracketTick());
    
    logger.info("MIDI Encoder: Returned to SELECT mode after note deletion");
} 