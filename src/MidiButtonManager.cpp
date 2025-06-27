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
#include "EditStates/EditSelectNoteState.h"
#include "NoteUtils.h"

MidiButtonManager midiButtonManager;

MidiButtonManager::MidiButtonManager() {
    // Initialize button states for each midi button
    buttonStates.resize(3); // A, B, Encoder
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
    
    // Initialize unified fader state machine
    initializeFaderStates();
    
    logger.info("MidiButtonManager setup complete with unified fader system. Monitoring USB Host Ch1: C2, D2, E2");
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
    
    // Check if grace period has elapsed to enable start editing
    if (noteSelectionTime > 0 && !startEditingEnabled) {
        enableStartEditing();
    }
    
    // Process unified fader state updates
    updateFaderStates();
    
    // DISABLED: Legacy scheduling system - now handled by unified system
    // if (pendingSelectnoteUpdate && now >= selectnoteUpdateTime) {
    //     pendingSelectnoteUpdate = false;
    //     performSelectnoteFaderUpdate(trackManager.getSelectedTrack());
    //     logger.log(CAT_MIDI, LOG_DEBUG, "Executed scheduled selectnote fader update");
    // }
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
                        sendEditModeProgram(currentEditMode);  // Send program 0 for NONE mode
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
    // Handle fine control CC2 on channel 15
    if (channel == FINE_CC_CHANNEL && ccNumber == FINE_CC_NUMBER) {
        handleMidiCC2Fine(channel, ccNumber, value);
        return;
    }
    
    // Handle note value control CC3 on channel 15
    if (channel == NOTE_VALUE_CC_CHANNEL && ccNumber == NOTE_VALUE_CC_NUMBER) {
        handleMidiCC3NoteValue(channel, ccNumber, value);
        return;
    }
    
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

void MidiButtonManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    // Log all pitchbend messages for debugging
    logger.log(CAT_MIDI, LOG_DEBUG, "Received pitchbend: ch=%d value=%d", channel, pitchValue);
    
    // Route to unified fader system
    if (channel == PITCHBEND_SELECT_CHANNEL) {
        handleFaderInput(FADER_SELECT, pitchValue, 0);
        return;
    } else if (channel == PITCHBEND_START_CHANNEL) {
        handleFaderInput(FADER_COARSE, pitchValue, 0);
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ignored: not on monitored channels (%d or %d)", 
               PITCHBEND_SELECT_CHANNEL, PITCHBEND_START_CHANNEL);
    
    // DISABLED: All legacy pitchbend handling code - replaced by unified system
    /*
    if (channel == PITCHBEND_SELECT_CHANNEL) {
        // Fader 1 (Channel 16): Note Selection - always active
        logger.log(CAT_MIDI, LOG_DEBUG, "MIDI Pitchbend SELECT: ch=%d value=%d", channel, pitchValue);
        
        // Track fader 1 activity to prevent fader 2 updates during selectnote fader use
        lastSelectnoteFaderTime = millis();
        
        // Initialize on first pitchbend message
        if (!pitchbendSelectInitialized) {
            lastPitchbendSelectValue = pitchValue;
            pitchbendSelectInitialized = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend SELECT initialized to %d", pitchValue);
            return;
        }
        
        // Only process if we're in SELECT mode
        if (currentEditMode != EDIT_MODE_SELECT) {
            return;
        }
        
        // Check if we should ignore incoming pitchbend (feedback prevention from selectnote updates)
        uint32_t now = millis();
        if (lastSelectnoteSentTime > 0 && (now - lastSelectnoteSentTime) < PITCHBEND_IGNORE_PERIOD) {
            uint32_t remaining = PITCHBEND_IGNORE_PERIOD - (now - lastSelectnoteSentTime);
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring incoming selectnote pitchbend (feedback prevention): %lu ms remaining", remaining);
            return;
        }
        
        // Check if we're in grace period after recent editing activity
        if (lastEditingActivityTime > 0 && (now - lastEditingActivityTime) < NOTE_SELECTION_GRACE_PERIOD) {
            uint32_t remaining = NOTE_SELECTION_GRACE_PERIOD - (now - lastEditingActivityTime);
            logger.log(CAT_MIDI, LOG_DEBUG, "Note selection disabled - editing grace period: %lu ms remaining", remaining);
            return;
        }
        
        Track& track = trackManager.getSelectedTrack();
        auto& midiEvents = track.getMidiEvents();
        uint32_t loopLength = track.getLoopLength();
        if (loopLength == 0) return;
        
        // Use the same navigation logic as before
        uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
        auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
        std::vector<uint32_t> allPositions;
        
        for (uint32_t step = 0; step < numSteps; step++) {
            uint32_t stepTick = step * Config::TICKS_PER_16TH_STEP;
            
            int nearbyNoteIdx = -1;
            for (int i = 0; i < (int)notes.size(); i++) {
                if (abs((int32_t)notes[i].startTick - (int32_t)stepTick) <= 24) {
                    nearbyNoteIdx = i;
                    break;
                }
            }
            
            if (nearbyNoteIdx >= 0) {
                allPositions.push_back(notes[nearbyNoteIdx].startTick);
            } else {
                allPositions.push_back(stepTick);
            }
        }
        
        // Remove duplicates
        for (int i = allPositions.size() - 1; i > 0; i--) {
            if (allPositions[i] == allPositions[i-1]) {
                allPositions.erase(allPositions.begin() + i);
            }
        }
        
        if (!allPositions.empty()) {
            int posIndex = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, allPositions.size() - 1);
            uint32_t targetTick = allPositions[posIndex];
            
            int noteIdx = -1;
            for (int i = 0; i < (int)notes.size(); i++) {
                if (notes[i].startTick == targetTick) {
                    noteIdx = i;
                    break;
                }
            }
            
            if (targetTick != editManager.getBracketTick()) {
                editManager.setBracketTick(targetTick);
                
                if (noteIdx >= 0) {
                    editManager.setSelectedNoteIdx(noteIdx);
                    logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend SELECT: selected note %d at tick %lu", noteIdx, targetTick);
                    
                    // Set initial reference step based on note position
                    referenceStep = targetTick / Config::TICKS_PER_16TH_STEP;
                    
                    // Start grace period
                    noteSelectionTime = millis();
                    startEditingEnabled = false;
                } else {
                    editManager.resetSelection();
                    logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend SELECT: selected empty step at tick %lu", targetTick);
                }
            }
        }
        
        lastPitchbendSelectValue = pitchValue;
        
    } else if (channel == PITCHBEND_START_CHANNEL) {
        // Fader 2 (Channel 15): Coarse 16th Step Movement - only active after grace period
        logger.log(CAT_MIDI, LOG_DEBUG, "MIDI Pitchbend COARSE: ch=%d value=%d", channel, pitchValue);
        
        // Initialize on first pitchbend message
        if (!pitchbendStartInitialized) {
            lastPitchbendStartValue = pitchValue;
            pitchbendStartInitialized = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend COARSE initialized to %d", pitchValue);
            return;
        }
        
        // Only process if start editing is enabled
        if (!startEditingEnabled) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Start editing disabled (grace period active)");
            return;
        }
        
        // Check if we should ignore incoming pitchbend (feedback prevention)
        uint32_t now = millis();
        if (lastPitchbendSentTime > 0 && (now - lastPitchbendSentTime) < PITCHBEND_IGNORE_PERIOD) {
            uint32_t remaining = PITCHBEND_IGNORE_PERIOD - (now - lastPitchbendSentTime);
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring incoming pitchbend (feedback prevention): %lu ms remaining", remaining);
            return;
        }
        
        // Only process if we have a selected note
        if (editManager.getSelectedNoteIdx() < 0) {
            logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse editing");
            return;
        }
        
        Track& track = trackManager.getSelectedTrack();
        uint32_t loopLength = track.getLoopLength();
        if (loopLength == 0) return;
        
        auto& midiEvents = track.getMidiEvents();
        auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
        int selectedIdx = editManager.getSelectedNoteIdx();
        
        if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
            uint32_t currentNoteStartTick = notes[selectedIdx].startTick;
            
            // Calculate how many 16th steps are in the loop
            uint32_t totalSixteenthSteps = loopLength / Config::TICKS_PER_16TH_STEP;
            
            // Calculate the offset within the current 16th step
            uint32_t currentSixteenthStep = currentNoteStartTick / Config::TICKS_PER_16TH_STEP;
            uint32_t offsetWithinSixteenth = currentNoteStartTick % Config::TICKS_PER_16TH_STEP;
            
            // Map pitchbend to 16th step across entire loop
            uint32_t targetSixteenthStep = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, totalSixteenthSteps - 1);
            
            // Calculate target tick: new 16th step + preserved offset
            uint32_t targetTick = (targetSixteenthStep * Config::TICKS_PER_16TH_STEP) + offsetWithinSixteenth;
            
            // Constrain to valid range within the loop
            if (targetTick >= loopLength) {
                targetTick = loopLength - 1;
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend COARSE: currentStep=%lu offset=%lu targetStep=%lu targetTick=%lu (pitchbend=%d)", 
                       currentSixteenthStep, offsetWithinSixteenth, targetSixteenthStep, targetTick, pitchValue);
            
            // Store the target step as reference for fine adjustments
            referenceStep = targetSixteenthStep;
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
            
            moveNoteToPosition(track, notes[selectedIdx], targetTick);
        }
        
        lastPitchbendStartValue = pitchValue;
    }
    */
}

void MidiButtonManager::handleMidiCC2Fine(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    // Route to unified fader system
    if (channel == FINE_CC_CHANNEL && ccNumber == FINE_CC_NUMBER) {
        handleFaderInput(FADER_FINE, 0, value);
        return;
    }
    
    // DISABLED: Legacy CC2 fine handling code - replaced by unified system
    /*
    // CC2 Fine Control: 127 discrete steps for fine positioning
    logger.log(CAT_MIDI, LOG_DEBUG, "MIDI CC2 FINE: ch=%d cc=%d value=%d", channel, ccNumber, value);
    
    // Initialize on first CC message
    if (!fineCCInitialized) {
        lastFineCCValue = value;
        fineCCInitialized = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "CC2 FINE initialized to %d", value);
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine editing disabled (grace period active)");
        return;
    }
    
    // Check if we should ignore incoming CC (feedback prevention)
    uint32_t now = millis();
    if (lastPitchbendSentTime > 0 && (now - lastPitchbendSentTime) < PITCHBEND_IGNORE_PERIOD) {
        uint32_t remaining = PITCHBEND_IGNORE_PERIOD - (now - lastPitchbendSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring incoming CC2 (feedback prevention): %lu ms remaining", remaining);
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine editing");
        return;
    }
    
    Track& track = trackManager.getSelectedTrack();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto& midiEvents = track.getMidiEvents();
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // Use the reference 16th step set by coarse movement (not current note position)
        uint32_t sixteenthStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
        
        // CC2 gives us 127 steps (0-127) for precise control
        // Map CC value directly to offset from 16th step start: 64 = 0 offset, 0 = -64 ticks, 127 = +63 ticks
        // This gives us 127 ticks of range starting from the 16th step start
        int32_t offset = (int32_t)value - 64;  // -64 to +63
        
        // Calculate target tick: reference 16th step start + CC offset
        int32_t targetTickSigned = (int32_t)sixteenthStepStartTick + offset;
        
        // Handle negative values by wrapping to end of loop
        uint32_t targetTick;
        if (targetTickSigned < 0) {
            targetTick = loopLength + targetTickSigned;
        } else {
            targetTick = (uint32_t)targetTickSigned;
        }
        
        // Constrain to valid range within the loop
        if (targetTick >= loopLength) {
            targetTick = targetTick % loopLength;
        }
        
        logger.log(CAT_MIDI, LOG_DEBUG, "CC2 FINE: referenceStep=%lu stepStart=%lu offset=%ld targetTick=%lu (cc=%d)", 
                   referenceStep, sixteenthStepStartTick, offset, targetTick, value);
        
        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        
        moveNoteToPosition(track, notes[selectedIdx], targetTick);
    }
    
    lastFineCCValue = value;
    */
}

void MidiButtonManager::handleMidiCC3NoteValue(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    // Route to unified fader system
    if (channel == NOTE_VALUE_CC_CHANNEL && ccNumber == NOTE_VALUE_CC_NUMBER) {
        handleFaderInput(FADER_NOTE_VALUE, 0, value);
        return;
    }
}

void MidiButtonManager::moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, uint32_t targetTick) {
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    
    // Calculate the difference for debugging
    int32_t tickDifference = (int32_t)targetTick - (int32_t)currentNote.startTick;
    logger.log(CAT_MIDI, LOG_DEBUG, "Note movement: from=%lu to=%lu difference=%ld", 
               currentNote.startTick, targetTick, tickDifference);
    
    // Find and update both note start and end positions to maintain duration
    bool noteStartUpdated = false;
    bool noteEndUpdated = false;
    uint32_t newEndTick = targetTick + (currentNote.endTick - currentNote.startTick);
    
    // Constrain the new end tick to stay within the loop
    if (newEndTick >= loopLength) {
        newEndTick = newEndTick % loopLength;
    }
    
    // Update note-on event (start)
    for (auto& event : midiEvents) {
        if (event.type == midi::NoteOn && 
            event.data.noteData.note == currentNote.note &&
            event.tick == currentNote.startTick &&
            event.data.noteData.velocity > 0) {
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Moving note start: pitch=%d from tick=%lu to tick=%lu", 
                       currentNote.note, event.tick, targetTick);
            
            event.tick = targetTick;
            noteStartUpdated = true;
            break;
        }
    }
    
    // Update note-off event (end) 
    for (auto& event : midiEvents) {
        if (((event.type == midi::NoteOff) || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
            event.data.noteData.note == currentNote.note &&
            event.tick == currentNote.endTick) {
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Moving note end: pitch=%d from tick=%lu to tick=%lu", 
                       currentNote.note, event.tick, newEndTick);
            
            event.tick = newEndTick;
            noteEndUpdated = true;
            break;
        }
    }
    
    if (noteStartUpdated && noteEndUpdated) {
        // Update the bracket to follow the note
        editManager.setBracketTick(targetTick);
        uint32_t noteDuration = currentNote.endTick - currentNote.startTick;
        logger.log(CAT_MIDI, LOG_DEBUG, "Note moved successfully: start=%lu end=%lu duration=%lu ticks", 
                   targetTick, newEndTick, noteDuration);
        
        // CRITICAL: Update the selectedNoteIdx to point to the moved note in the new reconstructed list
        auto updatedNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
        int newSelectedIdx = -1;
        
        // Find the moved note in the updated notes list
        for (int i = 0; i < (int)updatedNotes.size(); i++) {
            if (updatedNotes[i].note == currentNote.note && 
                updatedNotes[i].startTick == targetTick &&
                updatedNotes[i].endTick == newEndTick) {
                newSelectedIdx = i;
                break;
            }
        }
        
        if (newSelectedIdx >= 0) {
            int oldSelectedIdx = editManager.getSelectedNoteIdx();
            editManager.setSelectedNoteIdx(newSelectedIdx);
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note at new position)", 
                       oldSelectedIdx, newSelectedIdx);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in reconstructed list");
        }
        
        // DISABLED: Legacy scheduling system - now handled by unified system
        // sendSelectnoteFaderUpdate(track);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Failed to update note: start=%s end=%s", 
                   noteStartUpdated ? "OK" : "FAILED", noteEndUpdated ? "OK" : "FAILED");
    }
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
            // First click: Enter select mode and stay there
            currentEditMode = EDIT_MODE_SELECT;
            uint32_t currentTick = clockManager.getCurrentTick();
            editManager.setState(editManager.getSelectNoteState(), track, currentTick);
            logger.info("MIDI Encoder: Entered SELECT mode (tick=%lu)", currentTick);
            break;
        }
            
        case EDIT_MODE_SELECT: {
            // Subsequent clicks: Handle note selection but stay in SELECT mode
            editManager.onButtonPress(track);
            
            // Record when a note was selected to start grace period
            noteSelectionTime = millis();
            startEditingEnabled = false;
            
            logger.info("MIDI Encoder: Note selected, grace period started (start editing disabled for %dms)", 
                       START_EDIT_GRACE_PERIOD);
            break;
        }
            
        default: {
            // Should never reach here with new workflow, but safety fallback
            currentEditMode = EDIT_MODE_SELECT;
            uint32_t currentTick = clockManager.getCurrentTick();
            editManager.setState(editManager.getSelectNoteState(), track, currentTick);
            logger.info("MIDI Encoder: Fallback to SELECT mode");
            break;
        }
    }
    
    // Always send SELECT mode program change (program 1) to keep fader 1 active
    sendEditModeProgram(currentEditMode);
    
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
    sendEditModeProgram(currentEditMode);  // Send program 1 for SELECT mode
    
    logger.info("MIDI Encoder: Returned to SELECT mode after note deletion");
}

void MidiButtonManager::sendEditModeProgram(EditModeState mode) {
    uint8_t program = static_cast<uint8_t>(mode);  // 0=NONE, 1=SELECT, 2=START, 3=LENGTH, 4=PITCH
    
    // Send pitchbend position BEFORE program change when entering SELECT mode
    if (mode == EDIT_MODE_SELECT) {
        EditSelectNoteState::sendTargetPitchbend(editManager, trackManager.getSelectedTrack());
    }
    
    // Send program change to both USB device and USB host on channel 16
    midiHandler.sendProgramChange(PROGRAM_CHANGE_CHANNEL, program);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=%d (mode=%s)", 
               PROGRAM_CHANGE_CHANNEL, program, 
               (mode == EDIT_MODE_NONE) ? "NONE" :
               (mode == EDIT_MODE_SELECT) ? "SELECT" :
               (mode == EDIT_MODE_START) ? "START" :
               (mode == EDIT_MODE_LENGTH) ? "LENGTH" :
               (mode == EDIT_MODE_PITCH) ? "PITCH" : "UNKNOWN");
}

void MidiButtonManager::sendStartNotePitchbend(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for start pitchbend");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Use the SAME tick value as EditSelectNoteState::sendTargetPitchbend for consistency
    // This ensures both fader 1 and fader 2 use the same reference position
    uint32_t bracketTick = editManager.getBracketTick();
    
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t noteStartTick = notes[selectedIdx].startTick;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fader 2 position sync: bracketTick=%lu, noteStartTick=%lu, diff=%ld", 
                   bracketTick, noteStartTick, (int32_t)bracketTick - (int32_t)noteStartTick);
        
        // For fader 2, use a simpler 16th-step based calculation that matches the hardware expectations
        uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
        
        // Find which 16th step the bracket tick is closest to
        float stepPosition = (float)bracketTick / (float)Config::TICKS_PER_16TH_STEP;
        uint32_t nearestStep = (uint32_t)(stepPosition + 0.5f);  // Round to nearest step
        if (nearestStep >= numSteps) nearestStep = numSteps - 1;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fader 2 calculation: bracketTick=%lu, stepPos=%.2f, nearestStep=%lu/%lu", 
                   bracketTick, stepPosition, nearestStep, numSteps);
        
        if (numSteps > 1) {
            // COARSE FADER (Channel 15): Map 16th step position to pitchbend range  
            const int16_t PITCHBEND_MIN = -8192;
            const int16_t PITCHBEND_MAX = 8191;
            
            // Map step position to pitchbend range
            float normalizedPos = (float)nearestStep / (float)(numSteps - 1);  // 0.0 to 1.0
            int16_t coarseMidiPitchbend = (int16_t)(PITCHBEND_MIN + normalizedPos * (PITCHBEND_MAX - PITCHBEND_MIN));
            coarseMidiPitchbend = constrain(coarseMidiPitchbend, PITCHBEND_MIN, PITCHBEND_MAX);
            
            logger.log(CAT_MIDI, LOG_DEBUG, "SENDING COARSE PITCHBEND: ch=%d bracketTick=%lu step=%lu/%lu pitchbend=%d", 
                       PITCHBEND_START_CHANNEL, bracketTick, nearestStep, numSteps, coarseMidiPitchbend);
            
            // Send coarse position to channel 15
            midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);
            
            // FINE CC2 (Channel 15): Position within 127 steps around 16th step center
            // IMPORTANT: Don't send CC values to fader 3 when it was recently the driver
            uint32_t now = millis();
            uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
            bool shouldSendFineCC = true;
            
            if (currentDriverFader == FADER_FINE && timeSinceDriverSet < 5000) {
                shouldSendFineCC = false;
                logger.log(CAT_MIDI, LOG_DEBUG, "Skipping legacy fine CC update - fader 3 was recently the driver (%lu ms ago)", 
                           timeSinceDriverSet);
            }
            
            if (shouldSendFineCC) {
                uint32_t currentSixteenthStep = noteStartTick / Config::TICKS_PER_16TH_STEP;
                uint32_t sixteenthStepStartTick = currentSixteenthStep * Config::TICKS_PER_16TH_STEP;
                int32_t halfSixteenth = Config::TICKS_PER_16TH_STEP / 2;  // 48 ticks
                int32_t offsetFromSixteenthCenter = (int32_t)noteStartTick - ((int32_t)sixteenthStepStartTick + halfSixteenth);
                
                // Map offset to CC2 value: 64 = center, range ±63
                uint8_t fineCCValue = (uint8_t)constrain(64 + offsetFromSixteenthCenter, 0, 127);
                
                logger.log(CAT_MIDI, LOG_DEBUG, "SENDING FINE CC2: ch=%d cc=%d stepStart=%lu offset=%ld ccValue=%d", 
                           FINE_CC_CHANNEL, FINE_CC_NUMBER, sixteenthStepStartTick, offsetFromSixteenthCenter, fineCCValue);
                
                // Send fine position as CC2 on channel 15
                midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);
            }
        }
        
        // Record when we sent this pitchbend to ignore incoming feedback
        lastPitchbendSentTime = millis();
        logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend and CC2 sent to faders - ignoring incoming for %dms", PITCHBEND_IGNORE_PERIOD);
    }
}

void MidiButtonManager::sendSelectnoteFaderUpdate(Track& track) {
    // Cancel any previous pending update and schedule a new one
    // This prevents multiple overlapping updates when fader 2 is moved continuously
    uint32_t now = millis();
    
    if (pendingSelectnoteUpdate) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Canceling previous selectnote update and scheduling new one");
    }
    
    selectnoteUpdateTime = now + SELECTNOTE_UPDATE_DELAY;
    pendingSelectnoteUpdate = true;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Scheduled selectnote fader update for %lu ms from now", SELECTNOTE_UPDATE_DELAY);
}

void MidiButtonManager::performSelectnoteFaderUpdate(Track& track) {
    // Send program change first to activate selectnote fader
    midiHandler.sendProgramChange(PITCHBEND_SELECT_CHANNEL, 1);  // Program 1 for SELECT mode
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=1 (SELECT mode for selectnote fader)", 
               PITCHBEND_SELECT_CHANNEL);
    
    // Send selectnote fader position update
    EditSelectNoteState::sendTargetPitchbend(editManager, track);
    
    // Check if we should update fader 2 position
    // Use extended protection period to prevent updates during active fader 2 use
    uint32_t now = millis();
    bool recentEditingActivity = (lastEditingActivityTime > 0 && (now - lastEditingActivityTime) < FADER2_PROTECTION_PERIOD);
    bool inPitchbendIgnorePeriod = (lastPitchbendSentTime > 0 && (now - lastPitchbendSentTime) < PITCHBEND_IGNORE_PERIOD);
    
    if (!recentEditingActivity && !inPitchbendIgnorePeriod) {
        // Safe to update fader 2 position - no recent activity and not in ignore period
        midiHandler.sendProgramChange(PITCHBEND_START_CHANNEL, 2);  // Program 2 for COARSE+FINE editing
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=2 (updating fader 2 position)", 
                   PITCHBEND_START_CHANNEL);
        
        // Send updated coarse and fine positions for fader 2 and 3
        sendStartNotePitchbend(track);
        logger.log(CAT_MIDI, LOG_DEBUG, "Updated fader 2/3 positions to match note position");
    } else {
        if (recentEditingActivity) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader 2 update - recent editing activity (%lu ms ago)", 
                       now - lastEditingActivityTime);
        }
        if (inPitchbendIgnorePeriod) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader 2 update - in pitchbend ignore period (%lu ms ago)", 
                       now - lastPitchbendSentTime);
        }
    }
    
    // Record when we sent this update to ignore incoming feedback
    lastSelectnoteSentTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Selectnote fader updated - ignoring incoming for %dms", PITCHBEND_IGNORE_PERIOD);
}

void MidiButtonManager::enableStartEditing() {
    uint32_t now = millis();
    if (now - noteSelectionTime >= START_EDIT_GRACE_PERIOD) {
        if (!startEditingEnabled) {
            startEditingEnabled = true;
            logger.info("Start editing enabled - grace period elapsed (%lu ms since selection)", now - noteSelectionTime);
            
            // Debug: Check if we still have a selected note when grace period expires
            int currentSelectedNote = editManager.getSelectedNoteIdx();
            logger.log(CAT_MIDI, LOG_DEBUG, "Grace period expired - selected note: %d", currentSelectedNote);
            
            // Check if fader 1 was recently used - if so, don't update fader 2 to prevent interference
            bool recentSelectnoteFaderActivity = (lastSelectnoteFaderTime > 0 && (now - lastSelectnoteFaderTime) < SELECTNOTE_PROTECTION_PERIOD);
            
            if (!recentSelectnoteFaderActivity) {
                // Check if there are already scheduled updates pending - if so, let them handle the sync
                bool hasScheduledUpdates = false;
                for (const auto& state : faderStates) {
                    if (state.pendingUpdate && (state.type == FADER_COARSE || state.type == FADER_FINE)) {
                        hasScheduledUpdates = true;
                        break;
                    }
                }
                
                if (!hasScheduledUpdates) {
                    // Use unified fader system to update position faders (2 and 3)
                    // This ensures proper ignore periods are set to prevent feedback loops
                    Track& track = trackManager.getSelectedTrack();
                    
                    // Send updates through unified system - this will set proper ignore periods
                    sendFaderUpdate(FADER_COARSE, track);
                    sendFaderUpdate(FADER_FINE, track);
                    
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated fader 2/3 positions via unified system with proper ignore periods");
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipping grace period updates - scheduled updates are pending");
                }
            } else {
                uint32_t remaining = SELECTNOTE_PROTECTION_PERIOD - (now - lastSelectnoteFaderTime);
                logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader 2/3 update - recent fader 1 activity (%lu ms ago, %lu ms remaining)", 
                           now - lastSelectnoteFaderTime, remaining);
                
                // Still send program change but not the position update
                midiHandler.sendProgramChange(PITCHBEND_START_CHANNEL, 2);
                logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=2 (mode only, no position update)", 
                           PITCHBEND_START_CHANNEL);
            }
        }
    }
    // REMOVED: Continuous grace period logging that was causing infinite loop
    // The grace period status should only be logged when it changes, not continuously
}

void MidiButtonManager::refreshEditingActivity() {
    lastEditingActivityTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Editing activity refreshed - note selection disabled for %dms", NOTE_SELECTION_GRACE_PERIOD);
}

// Unified Fader State Machine Implementation

void MidiButtonManager::initializeFaderStates() {
    faderStates.clear();
    faderStates.resize(4);
    
    // Initialize Fader 1: Note Selection (Channel 16, Pitchbend)
    faderStates[0] = {
        .type = FADER_SELECT,
        .channel = PITCHBEND_SELECT_CHANNEL,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    // Initialize Fader 2: Coarse Positioning (Channel 15, Pitchbend)
    faderStates[1] = {
        .type = FADER_COARSE,
        .channel = PITCHBEND_START_CHANNEL,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    // Initialize Fader 3: Fine Positioning (Channel 15, CC2)
    faderStates[2] = {
        .type = FADER_FINE,
        .channel = FINE_CC_CHANNEL,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    // Initialize Fader 4: Note Value Editing (Channel 15, CC3)
    faderStates[3] = {
        .type = FADER_NOTE_VALUE,
        .channel = NOTE_VALUE_CC_CHANNEL,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    logger.info("Fader state machine initialized with 4 faders");
}

FaderState& MidiButtonManager::getFaderState(FaderType faderType) {
    for (auto& state : faderStates) {
        if (state.type == faderType) {
            return state;
        }
    }
    // Should never happen, but return first as fallback
    return faderStates[0];
}

bool MidiButtonManager::shouldIgnoreFaderInput(FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, -1, -1); // Use overloaded version with unknown values
}

bool MidiButtonManager::shouldIgnoreFaderInput(FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    FaderState& state = getFaderState(faderType);
    uint32_t now = millis();
    
    // No feedback prevention if we haven't sent anything recently
    if (state.lastSentTime == 0 || (now - state.lastSentTime) >= FEEDBACK_IGNORE_PERIOD) {
        return false;
    }
    
    // If we don't have the incoming values, use the old blanket ignore logic as fallback
    if (pitchbendValue == -1 && ccValue == (uint8_t)-1) {
        uint32_t remaining = FEEDBACK_IGNORE_PERIOD - (now - state.lastSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (feedback prevention): %lu ms remaining", 
                   faderType, remaining);
        return true;
    }
    
    // EDGE CASE FIX: Immediate post-update grace period
    // For the first 200ms after sending an update, ignore ALL input to prevent
    // user reactions to automatic fader movements from triggering new updates
    const uint32_t POST_UPDATE_GRACE_PERIOD = 200; // 200ms strict ignore period
    if ((now - state.lastSentTime) < POST_UPDATE_GRACE_PERIOD) {
        uint32_t remaining = POST_UPDATE_GRACE_PERIOD - (now - state.lastSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (post-update grace period): %lu ms remaining", 
                   faderType, remaining);
        return true;
    }
    
    // Smart feedback detection: only ignore if the incoming value matches what we just sent
    const int16_t FEEDBACK_TOLERANCE_PITCHBEND = 100;  // Allow 100 units tolerance for pitchbend
    const uint8_t FEEDBACK_TOLERANCE_CC = 3;           // Allow 3 units tolerance for CC
    
    bool isProbablyFeedback = false;
    
    if (faderType == FADER_SELECT || faderType == FADER_COARSE) {
        // For pitchbend faders, check if incoming value is close to what we last sent
        int16_t diff = abs(pitchbendValue - state.lastSentPitchbend);
        if (diff <= FEEDBACK_TOLERANCE_PITCHBEND) {
            isProbablyFeedback = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d pitchbend %d (feedback: sent %d, diff=%d)", 
                       faderType, pitchbendValue, state.lastSentPitchbend, diff);
        }
    } else if (faderType == FADER_FINE || faderType == FADER_NOTE_VALUE) {
        // For CC faders, check if incoming value is close to what we last sent
        uint8_t diff = abs((int)ccValue - (int)state.lastSentCC);
        if (diff <= FEEDBACK_TOLERANCE_CC) {
            isProbablyFeedback = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d CC %d (feedback: sent %d, diff=%d)", 
                       faderType, ccValue, state.lastSentCC, diff);
        }
    }
    
    if (!isProbablyFeedback) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Accepting fader %d input (significant user movement, not feedback)", 
                   faderType);
    }
    
    return isProbablyFeedback;
}

void MidiButtonManager::handleFaderInput(FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    if (shouldIgnoreFaderInput(faderType, pitchbendValue, ccValue)) {
        return;
    }
    
    FaderState& state = getFaderState(faderType);
    uint32_t now = millis();
    
    // Initialize on first input
    if (!state.isInitialized) {
        state.lastPitchbendValue = pitchbendValue;
        state.lastCCValue = ccValue;
        state.isInitialized = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d initialized: pitchbend=%d cc=%d", 
                   faderType, pitchbendValue, ccValue);
        return;
    }
    
    // Apply deadband filtering to prevent jitter from causing continuous updates
    bool significantChange = false;
    const int16_t PITCHBEND_DEADBAND = 23;  // Ignore changes smaller than this
    const uint8_t CC_DEADBAND_FINE = 1;     // Fine fader needs precise control
    
    if (faderType == FADER_SELECT || faderType == FADER_COARSE) {
        // For pitchbend faders, check if change is significant
        int16_t pitchbendDiff = abs(pitchbendValue - state.lastPitchbendValue);
        if (pitchbendDiff >= PITCHBEND_DEADBAND) {
            significantChange = true;
        }
    } else if (faderType == FADER_FINE || faderType == FADER_NOTE_VALUE) {
        // For CC faders, use smaller deadband for precise control
        uint8_t ccDiff = abs((int)ccValue - (int)state.lastCCValue);
        if (ccDiff >= CC_DEADBAND_FINE) {
            significantChange = true;
        }
    }
    
    if (!significantChange) {
        // Change too small - ignore to prevent jitter
        return;
    }
    
    // Update state
    state.lastPitchbendValue = pitchbendValue;
    state.lastCCValue = ccValue;
    state.lastUpdateTime = now;
    
    // Set this fader as the current driver
    currentDriverFader = faderType;
    lastDriverFaderUpdateTime = now;
    lastDriverFaderTime = now;
    
    // Process the fader input based on type
    Track& track = trackManager.getSelectedTrack();
    
    switch (faderType) {
        case FADER_SELECT:
            handleSelectFaderInput(pitchbendValue, track);
            break;
        case FADER_COARSE:
            handleCoarseFaderInput(pitchbendValue, track);
            break;
        case FADER_FINE:
            handleFineFaderInput(ccValue, track);
            break;
        case FADER_NOTE_VALUE:
            handleNoteValueFaderInput(ccValue, track);
            break;
    }
    
    // Schedule updates for other faders after delay
    scheduleOtherFaderUpdates(faderType);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d input processed: driver fader set", faderType);
}

void MidiButtonManager::scheduleOtherFaderUpdates(FaderType driverFader) {
    // CRITICAL FIX: Don't schedule ANY updates when fader 1 (SELECT) is the driver
    // Fader 1 is on channel 16, completely separate from faders 2&3 on channel 15
    // There's no need for feedback prevention between separate channels
    // Scheduling updates when fader 1 is driver causes it to get "stuck" as driver
    if (driverFader == FADER_SELECT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Skipping all update scheduling - fader 1 (SELECT) is on separate channel, no feedback prevention needed");
        return;
    }
    
    uint32_t now = millis();
    uint32_t updateTime = now + FADER_UPDATE_DELAY;
    
    // Cancel pending updates only for faders that would conflict with the new driver
    // Faders 2, 3, and 4 share channel 15, so they conflict with each other
    // Fader 1 is on channel 16, so it never conflicts
    // IMPORTANT: Don't cancel updates for the driver itself - that causes feedback loops
    for (auto& state : faderStates) {
        bool wouldConflict = false;
        
        // Check if driver and state share channel 15 (faders 2, 3, 4)
        bool driverOnChannel15 = (driverFader == FADER_COARSE || driverFader == FADER_FINE || driverFader == FADER_NOTE_VALUE);
        bool stateOnChannel15 = (state.type == FADER_COARSE || state.type == FADER_FINE || state.type == FADER_NOTE_VALUE);
        
        if (driverOnChannel15 && stateOnChannel15 && driverFader != state.type) {
            wouldConflict = true; // Any channel 15 fader conflicts with other channel 15 faders
        }
        // Removed: Don't cancel updates for the driver itself
        
        if (wouldConflict) {
            state.pendingUpdate = false;
            state.updateScheduledTime = 0;
            state.scheduledByDriver = FADER_SELECT; // Reset to default
            logger.log(CAT_MIDI, LOG_DEBUG, "Cancelled pending update for fader %d (channel conflict with new driver %d)", 
                       state.type, driverFader);
        }
    }
    
    // Schedule updates for non-driver faders
    for (auto& state : faderStates) {
        if (state.type != driverFader) {
            state.pendingUpdate = true;
            state.updateScheduledTime = updateTime;
            state.scheduledByDriver = driverFader;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Scheduled fader %d update for %lu ms from now (scheduled by driver %d)", 
                       state.type, FADER_UPDATE_DELAY, driverFader);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping scheduling for fader %d - it's the driver", state.type);
        }
    }
}

void MidiButtonManager::updateFaderStates() {
    uint32_t now = millis();
    
    // Process scheduled fader updates
    for (auto& state : faderStates) {
        if (state.pendingUpdate) {
            if (now >= state.updateScheduledTime) {
                state.pendingUpdate = false;
                
                // Only update if this fader wasn't the driver when the update was scheduled
                // AND the current driver is still the same as when the update was scheduled
                // AND we don't have channel conflicts (faders 2, 3, and 4 share channel 15)
                bool currentDriverOnChannel15 = (currentDriverFader == FADER_COARSE || currentDriverFader == FADER_FINE || currentDriverFader == FADER_NOTE_VALUE);
                bool stateOnChannel15 = (state.type == FADER_COARSE || state.type == FADER_FINE || state.type == FADER_NOTE_VALUE);
                bool hasChannelConflict = currentDriverOnChannel15 && stateOnChannel15 && currentDriverFader != state.type;
                // Note: FADER_SELECT (fader 1) is on channel 16, so it never conflicts with faders 2, 3, or 4
                
                // Check if the driver fader is still being actively moved
                bool driverStillActive = false;
                if (state.scheduledByDriver == FADER_SELECT) {
                    // For select fader, check if there was recent activity
                    driverStillActive = (lastSelectnoteFaderTime > 0 && (now - lastSelectnoteFaderTime) < 500); // 500ms recent activity window
                } else if (state.scheduledByDriver == FADER_COARSE) {
                    // For coarse fader, check recent activity (you'd need to add lastCoarseFaderTime tracking)
                    driverStillActive = (lastDriverFaderTime > 0 && currentDriverFader == FADER_COARSE && (now - lastDriverFaderTime) < 500);
                } else if (state.scheduledByDriver == FADER_FINE) {
                    // For fine fader, check recent activity  
                    driverStillActive = (lastDriverFaderTime > 0 && currentDriverFader == FADER_FINE && (now - lastDriverFaderTime) < 500);
                } else if (state.scheduledByDriver == FADER_NOTE_VALUE) {
                    // For note value fader, check recent activity  
                    driverStillActive = (lastDriverFaderTime > 0 && currentDriverFader == FADER_NOTE_VALUE && (now - lastDriverFaderTime) < 500);
                }
                
                // CRITICAL FIX: Never execute scheduled updates when fader 1 (SELECT) was the driver
                // Fader 1 is on channel 16, completely separate from faders 2&3 on channel 15
                // Updating faders 2&3 when fader 1 is the driver causes unwanted motorized fader movement
                // and feedback that overrides the user's fader 1 input
                bool skipForSelectDriver = (state.scheduledByDriver == FADER_SELECT);
                
                if (state.type != state.scheduledByDriver && 
                    currentDriverFader == state.scheduledByDriver && 
                    !hasChannelConflict &&
                    !driverStillActive &&
                    !skipForSelectDriver) {
                    Track& track = trackManager.getSelectedTrack();
                    sendFaderUpdate(state.type, track);
                    
                    logger.log(CAT_MIDI, LOG_DEBUG, "Executed scheduled update for fader %d (scheduled by driver %d)", 
                               state.type, state.scheduledByDriver);
                } else if (state.type == state.scheduledByDriver) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipped update for fader %d - was the driver when scheduled", 
                               state.type);
                } else if (currentDriverFader != state.scheduledByDriver) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipped update for fader %d - driver changed from %d to %d", 
                               state.type, state.scheduledByDriver, currentDriverFader);
                } else if (hasChannelConflict) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipped update for fader %d - channel conflict with current driver %d", 
                               state.type, currentDriverFader);
                } else if (driverStillActive) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipped update for fader %d - driver %d still active (%lu ms ago)", 
                               state.type, state.scheduledByDriver, now - (state.scheduledByDriver == FADER_SELECT ? lastSelectnoteFaderTime : lastDriverFaderTime));
                } else if (skipForSelectDriver) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipped update for fader %d - fader 1 (SELECT) was the driver (prevents motorized feedback)", 
                               state.type);
                }
            }
        }
    }
}

void MidiButtonManager::sendFaderUpdate(FaderType faderType, Track& track) {
    // IMPORTANT: Don't update CC faders (faders 3 and 4) when they were recently the driver
    // These faders represent user input and should maintain their position for a reasonable time
    // The MIDI events are the single source of truth - don't send calculated positions back to these faders
    if ((faderType == FADER_FINE && currentDriverFader == FADER_FINE) ||
        (faderType == FADER_NOTE_VALUE && currentDriverFader == FADER_NOTE_VALUE)) {
        uint32_t now = millis();
        uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
        if (timeSinceDriverSet < 1000) { // 1 second protection period
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader %d update - fader %d was recently the driver (%lu ms ago)", 
                       faderType, faderType, timeSinceDriverSet);
            return;
        }
    }
    
    // Faders 3 and 4 (CC faders) never need program changes - they only use CC messages
    bool shouldSendProgramChange = (faderType != FADER_FINE && faderType != FADER_NOTE_VALUE);
    
    // Additionally, skip program change if this fader shares a channel with the current driver
    bool currentDriverOnChannel15 = (currentDriverFader == FADER_COARSE || currentDriverFader == FADER_FINE || currentDriverFader == FADER_NOTE_VALUE);
    bool faderOnChannel15 = (faderType == FADER_COARSE || faderType == FADER_FINE || faderType == FADER_NOTE_VALUE);
    
    if (shouldSendProgramChange && currentDriverOnChannel15 && faderOnChannel15 && currentDriverFader != faderType) {
        // Driver and fader both share channel 15 - skip program change
        shouldSendProgramChange = false;
        logger.log(CAT_MIDI, LOG_DEBUG, "Skipping program change for fader %d (shares channel 15 with driver %d)", 
                   faderType, currentDriverFader);
    }
    
    if (shouldSendProgramChange) {
        uint8_t program = (faderType == FADER_SELECT) ? 1 : 2;
        midiHandler.sendProgramChange(getFaderState(faderType).channel, program);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=%d (fader %d update)", 
                   getFaderState(faderType).channel, program, faderType);
    } else if (faderType == FADER_FINE || faderType == FADER_NOTE_VALUE) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Skipped program change for fader %d (CC fader) - only uses CC messages", faderType);
    }
    
    // Send position update
    sendFaderPosition(faderType, track);
    
    // Record when we sent this update and set ignore periods
    uint32_t now = millis();
    getFaderState(faderType).lastSentTime = now;
    
    // IMPORTANT: If updating any channel 15 fader, all channel 15 faders get updated together.
    // Set ignore periods for all to prevent feedback from any MIDI message causing unwanted processing.
    if (faderType == FADER_COARSE || faderType == FADER_FINE || faderType == FADER_NOTE_VALUE) {
        getFaderState(FADER_COARSE).lastSentTime = now;
        getFaderState(FADER_FINE).lastSentTime = now;
        getFaderState(FADER_NOTE_VALUE).lastSentTime = now;
        logger.log(CAT_MIDI, LOG_DEBUG, "Set ignore periods for all channel 15 faders (shared channel)");
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d updated - ignoring incoming for %dms", 
               faderType, FEEDBACK_IGNORE_PERIOD);
}

void MidiButtonManager::sendFaderPosition(FaderType faderType, Track& track) {
    switch (faderType) {
        case FADER_SELECT:
            EditSelectNoteState::sendTargetPitchbend(editManager, track);
            break;
        case FADER_COARSE:
            sendCoarseFaderPosition(track);
            break;
        case FADER_FINE:
            sendFineFaderPosition(track);
            break;
        case FADER_NOTE_VALUE:
            sendNoteValueFaderPosition(track);
            break;
    }
}

void MidiButtonManager::sendCoarseFaderPosition(Track& track) {
    // IMPORTANT: Don't send pitchbend to channel 15 when fader 3 was recently the driver
    // Fader 2 and 3 share channel 15, so pitchbend updates to fader 2 will also move fader 3
    // This violates the "MIDI events as single source of truth" principle for fader 3
    if (currentDriverFader == FADER_FINE) {
        uint32_t now = millis();
        uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
        if (timeSinceDriverSet < 1000) { // 1 second protection period
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping coarse fader pitchbend update - fader 3 was recently the driver (%lu ms ago)", 
                       timeSinceDriverSet);
            return;
        }
    }
    
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse position update");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    // Double-check that the selected note index is still valid after potential note modifications
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t noteStartTick = notes[selectedIdx].startTick;
        uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
        uint32_t currentSixteenthStep = noteStartTick / Config::TICKS_PER_16TH_STEP;
        
        // Debug: verify we're using the correct note
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse position: selectedIdx=%d, noteStartTick=%lu, step=%lu", 
                   selectedIdx, noteStartTick, currentSixteenthStep);
        
        if (numSteps > 1) {
            const int16_t PITCHBEND_MIN = -8192;
            const int16_t PITCHBEND_MAX = 8191;
            
            float normalizedPos = (float)currentSixteenthStep / (float)(numSteps - 1);
            int16_t coarseMidiPitchbend = (int16_t)(PITCHBEND_MIN + normalizedPos * (PITCHBEND_MAX - PITCHBEND_MIN));
            coarseMidiPitchbend = constrain(coarseMidiPitchbend, PITCHBEND_MIN, PITCHBEND_MAX);
            
            midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);
            
            // Send note 1 trigger on channel 15 to help motorized fader 2 update
            midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, 1, 127);
            midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, 1, 0);
            
            // Record the value we sent for smart feedback detection
            getFaderState(FADER_COARSE).lastSentPitchbend = coarseMidiPitchbend;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Sent coarse pitchbend=%d + note 1 trigger (note at step %lu)", 
                       coarseMidiPitchbend, currentSixteenthStep);
        }
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse position: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

void MidiButtonManager::sendFineFaderPosition(Track& track) {
    // IMPORTANT: Don't send CC values to fader 3 when it was recently the driver
    // Fader 3 represents user input and should maintain its position - MIDI events are the single source of truth
    if (currentDriverFader == FADER_FINE) {
        uint32_t now = millis();
        uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
        if (timeSinceDriverSet < 1000) { // 1 second protection period
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fine fader CC update - fader 3 was recently the driver (%lu ms ago)", 
                       timeSinceDriverSet);
            return;
        }
    }
    
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine position update");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    // Double-check that the selected note index is still valid after potential note modifications
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t noteStartTick = notes[selectedIdx].startTick;
        
        // Use the reference step (established by coarse/select faders) as the base for CC calculation
        // This ensures fader 3 represents the note's position relative to a stable reference
        uint32_t referenceStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
        int32_t offsetFromReferenceStep = (int32_t)noteStartTick - (int32_t)referenceStepStartTick;
        
        // Debug: verify we're using the reference step
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine position: selectedIdx=%d, noteStartTick=%lu, referenceStep=%lu, offset=%ld", 
                   selectedIdx, noteStartTick, referenceStep, offsetFromReferenceStep);
        
        // CC64 = 0 tick offset from reference step start, CC0 = -64 ticks, CC127 = +63 ticks
        uint8_t fineCCValue = (uint8_t)constrain(64 + offsetFromReferenceStep, 0, 127);
        midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);
        
        // Send note-on with velocity 127 followed by note-off to trigger fader update
        midiHandler.sendNoteOn(FINE_CC_CHANNEL, 0, 127);
        midiHandler.sendNoteOff(FINE_CC_CHANNEL, 0, 0);
        
        // Record the value we sent for smart feedback detection
        getFaderState(FADER_FINE).lastSentCC = fineCCValue;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent fine CC=%d + note trigger (note offset %ld from reference step %lu)", 
                   fineCCValue, offsetFromReferenceStep, referenceStep);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine position: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

void MidiButtonManager::sendNoteValueFaderPosition(Track& track) {
    // IMPORTANT: Don't send CC values to fader 4 when it was recently the driver
    // Fader 4 represents user input and should maintain its position - MIDI events are the single source of truth
    if (currentDriverFader == FADER_NOTE_VALUE) {
        uint32_t now = millis();
        uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
        if (timeSinceDriverSet < 1000) { // 1 second protection period
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping note value fader CC update - fader 4 was recently the driver (%lu ms ago)", 
                       timeSinceDriverSet);
            return;
        }
    }
    
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value update");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    // Double-check that the selected note index is still valid after potential note modifications
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint8_t noteValue = notes[selectedIdx].note;
        
        // Map MIDI note value (0-127) to CC value (0-127)
        // This is a direct 1:1 mapping
        uint8_t noteValueCCValue = noteValue;
        
        // Debug: verify we're using the correct note
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value: selectedIdx=%d, noteValue=%d", 
                   selectedIdx, noteValue);
        
        midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER, noteValueCCValue);
        
        // Send note 2 trigger on channel 15 to help motorized fader 4 update
        midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, 2, 127);
        midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, 2, 0);
        
        // Record the value we sent for smart feedback detection
        getFaderState(FADER_NOTE_VALUE).lastSentCC = noteValueCCValue;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent note value CC=%d + note 2 trigger (note value %d)", 
                   noteValueCCValue, noteValue);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

void MidiButtonManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    // Only process if we're in SELECT mode
    if (currentEditMode != EDIT_MODE_SELECT) {
        return;
    }
    
    uint32_t now = millis();
    
    // SMART SELECTION STABILITY: Only process significant movements that indicate user intent
    // This prevents motorized fader feedback from being interpreted as user input
    bool isSignificantMovement = false;
    
    if (lastSelectFaderTime == 0) {
        // First movement - always significant
        isSignificantMovement = true;
    } else {
        int16_t movementDelta = abs(pitchValue - lastUserSelectFaderValue);
        uint32_t timeSinceLastMovement = now - lastSelectFaderTime;
        
        // Movement is significant if:
        // 1. Large enough change (> threshold), OR
        // 2. Enough time has passed since last movement (user settled then moved again)
        if (movementDelta >= SELECT_MOVEMENT_THRESHOLD || timeSinceLastMovement >= SELECT_STABILITY_TIME) {
            isSignificantMovement = true;
        }
    }
    
    if (!isSignificantMovement) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: ignoring small movement (delta=%d, time=%lu ms)", 
                   abs(pitchValue - lastUserSelectFaderValue), now - lastSelectFaderTime);
        return;
    }
    
    // Update tracking for next comparison
    lastUserSelectFaderValue = pitchValue;
    lastSelectFaderTime = now;
    
    // Check if we're in grace period after recent editing activity
    if (lastEditingActivityTime > 0 && (now - lastEditingActivityTime) < NOTE_SELECTION_GRACE_PERIOD) {
        uint32_t remaining = NOTE_SELECTION_GRACE_PERIOD - (now - lastEditingActivityTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection disabled - editing grace period: %lu ms remaining", remaining);
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Use the same navigation logic for note selection
    uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    std::vector<uint32_t> allPositions;
    
    for (uint32_t step = 0; step < numSteps; step++) {
        uint32_t stepTick = step * Config::TICKS_PER_16TH_STEP;
        
        int nearbyNoteIdx = -1;
        for (int i = 0; i < (int)notes.size(); i++) {
            // A note belongs to this step if it's in the same step (integer division)
            uint32_t noteStep = notes[i].startTick / Config::TICKS_PER_16TH_STEP;
            if (noteStep == step) {
                nearbyNoteIdx = i;
                break;
            }
        }
        
        if (nearbyNoteIdx >= 0) {
            // There's a note in this step - use the note position to represent this step
            allPositions.push_back(notes[nearbyNoteIdx].startTick);
        } else {
            // No note in this step - use the empty step position
            allPositions.push_back(stepTick);
        }
    }
    
    // Remove duplicates
    for (int i = allPositions.size() - 1; i > 0; i--) {
        if (allPositions[i] == allPositions[i-1]) {
            allPositions.erase(allPositions.begin() + i);
        }
    }
    
    if (!allPositions.empty()) {
        int posIndex = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, allPositions.size() - 1);
        uint32_t targetTick = allPositions[posIndex];
        
        int noteIdx = -1;
        for (int i = 0; i < (int)notes.size(); i++) {
            if (notes[i].startTick == targetTick) {
                noteIdx = i;
                break;
            }
        }
        
        if (targetTick != editManager.getBracketTick()) {
            editManager.setBracketTick(targetTick);
            
            if (noteIdx >= 0) {
                editManager.setSelectedNoteIdx(noteIdx);
                logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected note %d at tick %lu", noteIdx, targetTick);
                
                // Set initial reference step based on note position
                referenceStep = targetTick / Config::TICKS_PER_16TH_STEP;
                
                // Schedule updates for other faders to reflect the new note position
                scheduleOtherFaderUpdates(FADER_SELECT);
                
                // Start grace period for start editing
                noteSelectionTime = millis();
                startEditingEnabled = false;
            } else {
                // STABILITY FIX: Only reset selection if we don't currently have a note selected
                // This prevents rapid fader movements from accidentally resetting a valid selection
                if (editManager.getSelectedNoteIdx() < 0) {
                    editManager.resetSelection();
                    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected empty step at tick %lu (no note found)", targetTick);
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: ignoring empty step at tick %lu (preserving current selection %d)", 
                               targetTick, editManager.getSelectedNoteIdx());
                }
            }
        }
    }
}

void MidiButtonManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Start editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse editing");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto& midiEvents = track.getMidiEvents();
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t currentNoteStartTick = notes[selectedIdx].startTick;
        
        // Calculate how many 16th steps are in the loop
        uint32_t totalSixteenthSteps = loopLength / Config::TICKS_PER_16TH_STEP;
        
        // Calculate the offset within the current 16th step
        uint32_t currentSixteenthStep = currentNoteStartTick / Config::TICKS_PER_16TH_STEP;
        uint32_t offsetWithinSixteenth = currentNoteStartTick % Config::TICKS_PER_16TH_STEP;
        
        // Map pitchbend to 16th step across entire loop
        uint32_t targetSixteenthStep = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, totalSixteenthSteps - 1);
        
        // Calculate target tick: new 16th step + preserved offset
        uint32_t targetTick = (targetSixteenthStep * Config::TICKS_PER_16TH_STEP) + offsetWithinSixteenth;
        
        // Constrain to valid range within the loop
        if (targetTick >= loopLength) {
            targetTick = loopLength - 1;
        }
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader: currentStep=%lu offset=%lu targetStep=%lu targetTick=%lu", 
                   currentSixteenthStep, offsetWithinSixteenth, targetSixteenthStep, targetTick);
        
        // Store the target step as reference for fine adjustments
        referenceStep = targetSixteenthStep;
        
        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        
        moveNoteToPosition(track, notes[selectedIdx], targetTick);
    }
}

void MidiButtonManager::handleFineFaderInput(uint8_t ccValue, Track& track) {
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine editing");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto& midiEvents = track.getMidiEvents();
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // Use the reference step established by coarse fader as the base
        // CC2 gives us ±64 ticks of fine control around the 16th step boundary
        uint32_t sixteenthStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
        
        // CC2 gives us 127 steps for precise control: CC=64 is center (no offset)
        int32_t offset = (int32_t)ccValue - 64;  // -64 to +63
        
        // Calculate target tick: 16th step boundary + CC offset
        int32_t targetTickSigned = (int32_t)sixteenthStepStartTick + offset;
        
        // Handle negative values by wrapping to end of loop
        uint32_t targetTick;
        if (targetTickSigned < 0) {
            targetTick = loopLength + targetTickSigned;
        } else {
            targetTick = (uint32_t)targetTickSigned;
        }
        
        // Constrain to valid range within the loop
        if (targetTick >= loopLength) {
            targetTick = targetTick % loopLength;
        }
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader: referenceStep=%lu offset=%ld targetTick=%lu (cc=%d)", 
                   referenceStep, offset, targetTick, ccValue);
        
        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        
        moveNoteToPosition(track, notes[selectedIdx], targetTick);
    }
}

void MidiButtonManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value editing");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto& midiEvents = track.getMidiEvents();
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint8_t currentNoteValue = notes[selectedIdx].note;
        uint8_t newNoteValue = ccValue;  // Direct 1:1 mapping from CC to MIDI note value
        
        // Constrain to valid MIDI note range (0-127)
        newNoteValue = constrain(newNoteValue, 0, 127);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: currentNote=%d newNote=%d (cc=%d)", 
                   currentNoteValue, newNoteValue, ccValue);
        
        // Find and update both note-on and note-off events
        bool noteOnUpdated = false;
        bool noteOffUpdated = false;
        
        // Update note-on event
        for (auto& event : midiEvents) {
            if (event.type == midi::NoteOn && 
                event.data.noteData.note == currentNoteValue &&
                event.tick == notes[selectedIdx].startTick &&
                event.data.noteData.velocity > 0) {
                
                logger.log(CAT_MIDI, LOG_DEBUG, "Updating note-on: pitch %d -> %d at tick %lu", 
                           currentNoteValue, newNoteValue, event.tick);
                
                event.data.noteData.note = newNoteValue;
                noteOnUpdated = true;
                break;
            }
        }
        
        // Update note-off event (or note-on with velocity 0)
        for (auto& event : midiEvents) {
            if (((event.type == midi::NoteOff) || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == currentNoteValue &&
                event.tick == notes[selectedIdx].endTick) {
                
                logger.log(CAT_MIDI, LOG_DEBUG, "Updating note-off: pitch %d -> %d at tick %lu", 
                           currentNoteValue, newNoteValue, event.tick);
                
                event.data.noteData.note = newNoteValue;
                noteOffUpdated = true;
                break;
            }
        }
        
        if (noteOnUpdated && noteOffUpdated) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed successfully: %d -> %d", 
                       currentNoteValue, newNoteValue);
            
            // Update the selected note index to point to the updated note
            auto updatedNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
            int newSelectedIdx = -1;
            
            // Find the updated note in the new notes list
            for (int i = 0; i < (int)updatedNotes.size(); i++) {
                if (updatedNotes[i].note == newNoteValue && 
                    updatedNotes[i].startTick == notes[selectedIdx].startTick &&
                    updatedNotes[i].endTick == notes[selectedIdx].endTick) {
                    newSelectedIdx = i;
                    break;
                }
            }
            
            if (newSelectedIdx >= 0) {
                editManager.setSelectedNoteIdx(newSelectedIdx);
                logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note with new value)", 
                           selectedIdx, newSelectedIdx);
            }
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Failed to update note value: noteOn=%s noteOff=%s", 
                       noteOnUpdated ? "OK" : "FAILED", noteOffUpdated ? "OK" : "FAILED");
        }
    }
}