//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "FaderManager.h"
#include "Track.h"
#include "TrackManager.h"
#include "EditManager.h"
#include "LooperState.h"
#include "StorageManager.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "Utils/MidiMapping.h"
#include "MidiHandler.h"
#include <algorithm>
#include <cmath>

FaderManager faderManager;

FaderManager::FaderManager() {
    initializeFaderStates();
}

void FaderManager::initializeFaderStates() {
    // Initialize all fader states to center/default values
    lastPitchbendSelectValue = PITCHBEND_CENTER;
    lastPitchbendStartValue = PITCHBEND_CENTER;
    lastFineCCValue = 64;
    lastNoteValueCCValue = 64;
    
    pitchbendSelectInitialized = false;
    pitchbendStartInitialized = false;
    fineCCInitialized = false;
    noteValueCCInitialized = false;
    
    // Clear scheduled updates
    scheduledUpdates.clear();
    pendingSelectnoteUpdate = false;
    selectnoteUpdateTime = 0;
}

void FaderManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    // Filter out small movements to prevent jitter
    if (abs(pitchValue - lastPitchbendSelectValue) < 100) {
        return;
    }
    
    // Initialize on first movement
    if (!pitchbendSelectInitialized) {
        pitchbendSelectInitialized = true;
        lastPitchbendSelectValue = pitchValue;
        return;
    }
    
    // Calculate target tick based on pitchbend value
    uint32_t targetTick = calculateTargetTick(pitchValue, track.getLoopLength());
    
    // Update edit manager bracket position
    editManager.setBracketTick(targetTick);
    editManager.selectClosestNote(track, targetTick);
    
    // Mark editing activity to prevent note selection changes
    refreshEditingActivity();
    
    lastPitchbendSelectValue = pitchValue;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: pitch=%d -> tick=%lu", pitchValue, targetTick);
}

void FaderManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
    // Filter out small movements
    if (abs(pitchValue - lastPitchbendStartValue) < 100) {
        return;
    }
    
    // Initialize on first movement
    if (!pitchbendStartInitialized) {
        pitchbendStartInitialized = true;
        lastPitchbendStartValue = pitchValue;
        return;
    }
    
    // Calculate target step (16th note resolution)
    uint8_t targetStep = calculateTargetStep(pitchValue, 16);
    uint32_t targetTick = targetStep * (track.getLoopLength() / 16);
    
    // Update edit manager bracket position
    editManager.setBracketTick(targetTick);
    editManager.selectClosestNote(track, targetTick);
    
    // Store reference step for fine control
    referenceStep = targetStep;
    
    // Mark editing activity
    refreshEditingActivity();
    
    lastPitchbendStartValue = pitchValue;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader: pitch=%d -> step=%d tick=%lu", pitchValue, targetStep, targetTick);
}

void FaderManager::handleFineFaderInput(uint8_t ccValue, Track& track) {
    // Filter out small movements
    if (abs(ccValue - lastFineCCValue) < 2) {
        return;
    }
    
    // Initialize on first movement
    if (!fineCCInitialized) {
        fineCCInitialized = true;
        lastFineCCValue = ccValue;
        return;
    }
    
    // Calculate fine offset within the current 16th step
    uint8_t fineOffset = calculateTargetOffset(ccValue, 24); // 24 ticks per 16th note
    uint32_t baseTick = referenceStep * (track.getLoopLength() / 16);
    uint32_t targetTick = baseTick + fineOffset;
    
    // Update edit manager bracket position
    editManager.setBracketTick(targetTick);
    editManager.selectClosestNote(track, targetTick);
    
    // Mark editing activity
    refreshEditingActivity();
    
    lastFineCCValue = ccValue;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader: cc=%d -> offset=%d tick=%lu", ccValue, fineOffset, targetTick);
}

void FaderManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Filter out small movements
    if (abs(ccValue - lastNoteValueCCValue) < 2) {
        return;
    }
    
    // Initialize on first movement
    if (!noteValueCCInitialized) {
        noteValueCCInitialized = true;
        lastNoteValueCCValue = ccValue;
        return;
    }
    
    // Calculate target note value
    uint8_t targetNote = calculateTargetNoteValue(ccValue);
    
    // Update selected note pitch if in pitch edit mode
    if (editManager.getCurrentState() == editManager.getPitchNoteState()) {
        // Note: EditPitchNoteState doesn't have setTargetPitch method
        // This functionality would need to be implemented in the state
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: cc=%d -> note=%d (pitch edit mode)", ccValue, targetNote);
    }
    
    lastNoteValueCCValue = ccValue;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: cc=%d -> note=%d", ccValue, targetNote);
}

void FaderManager::handleLoopStartFaderInput(int16_t pitchValue, Track& track) {
    // Filter out small movements
    if (abs(pitchValue - lastPitchbendSelectValue) < 100) {
        return;
    }
    
    // Initialize on first movement
    if (!pitchbendSelectInitialized) {
        pitchbendSelectInitialized = true;
        lastPitchbendSelectValue = pitchValue;
        return;
    }
    
    // Calculate target tick for loop start
    uint32_t targetTick = calculateTargetTick(pitchValue, track.getLoopLength());
    
    // Update loop start point
    track.setLoopStartTick(targetTick);
    
    // Mark loop start editing activity
    refreshLoopStartEditingActivity();
    
    lastPitchbendSelectValue = pitchValue;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader: pitch=%d -> start=%lu", pitchValue, targetTick);
}

void FaderManager::handleLoopLengthInput(uint8_t ccValue, Track& track) {
    // Calculate target loop length in bars (1-8 bars)
    uint8_t targetBars = 1 + (ccValue * 7 / 127); // Map 0-127 to 1-8 bars
    uint32_t targetLength = targetBars * Config::TICKS_PER_BAR;
    
    // Update loop length
    track.setLoopLengthWithWrapping(targetLength);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Loop length: cc=%d -> bars=%d length=%lu", ccValue, targetBars, targetLength);
}

void FaderManager::refreshLoopStartEditingActivity() {
    loopStartEditingTime = millis();
    loopStartEditingEnabled = false;
}

void FaderManager::updateLoopEndpointAfterGracePeriod(Track& track) {
    uint32_t now = millis();
    
    // Re-enable loop start editing after grace period
    if (!loopStartEditingEnabled && (now - loopStartEditingTime) >= LOOP_START_GRACE_PERIOD) {
        loopStartEditingEnabled = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start editing grace period ended");
    }
}

void FaderManager::scheduleFaderUpdate(uint8_t faderType, uint32_t delayMs) {
    ScheduledUpdate update;
    update.faderType = faderType;
    update.executeTime = millis() + delayMs;
    update.active = true;
    scheduledUpdates.push_back(update);
}

void FaderManager::processScheduledUpdates() {
    uint32_t now = millis();
    
    for (auto& update : scheduledUpdates) {
        if (update.active && now >= update.executeTime) {
            Track& track = trackManager.getSelectedTrack();
            sendFaderUpdate(static_cast<MidiMapping::FaderType>(update.faderType), track);
            update.active = false;
        }
    }
    
    // Remove completed updates
    scheduledUpdates.erase(
        std::remove_if(scheduledUpdates.begin(), scheduledUpdates.end(),
                      [](const ScheduledUpdate& update) { return !update.active; }),
        scheduledUpdates.end()
    );
}

void FaderManager::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    // Schedule updates for other faders when one fader changes
    switch (driverFader) {
        case MidiMapping::FaderType::FADER_SELECT:
            scheduleFaderUpdate(static_cast<uint8_t>(MidiMapping::FaderType::FADER_COARSE), 50);
            scheduleFaderUpdate(static_cast<uint8_t>(MidiMapping::FaderType::FADER_FINE), 100);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
            scheduleFaderUpdate(static_cast<uint8_t>(MidiMapping::FaderType::FADER_SELECT), 50);
            scheduleFaderUpdate(static_cast<uint8_t>(MidiMapping::FaderType::FADER_FINE), 100);
            break;
        case MidiMapping::FaderType::FADER_FINE:
            scheduleFaderUpdate(static_cast<uint8_t>(MidiMapping::FaderType::FADER_SELECT), 50);
            scheduleFaderUpdate(static_cast<uint8_t>(MidiMapping::FaderType::FADER_COARSE), 100);
            break;
        default:
            break;
    }
}

void FaderManager::sendFaderUpdate(MidiMapping::FaderType faderType, Track& track) {
    sendFaderPosition(faderType, track);
}

void FaderManager::sendFaderPosition(MidiMapping::FaderType faderType, Track& track) {
    switch (faderType) {
        case MidiMapping::FaderType::FADER_SELECT:
            sendSelectnoteFaderUpdate(track);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
            sendCoarseFaderPosition(track);
            break;
        case MidiMapping::FaderType::FADER_FINE:
            sendFineFaderPosition(track);
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            sendNoteValueFaderPosition(track);
            break;
    }
}

void FaderManager::sendCoarseFaderPosition(Track& track) {
    uint32_t bracketTick = editManager.getBracketTick();
    uint32_t loopLength = track.getLoopLength();
    
    if (loopLength == 0) return;
    
    // Calculate 16th step position
    uint8_t step = (bracketTick * 16) / loopLength;
    
    // Convert to pitchbend value
    int16_t pitchValue = PITCHBEND_MIN + (step * (PITCHBEND_MAX - PITCHBEND_MIN) / 15);
    
    // Send pitchbend
    midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, pitchValue);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent coarse fader position: step=%d pitch=%d", step, pitchValue);
}

void FaderManager::sendFineFaderPosition(Track& track) {
    uint32_t bracketTick = editManager.getBracketTick();
    uint32_t loopLength = track.getLoopLength();
    
    if (loopLength == 0) return;
    
    // Calculate fine offset within current 16th step
    uint32_t stepSize = loopLength / 16;
    uint32_t stepStart = (bracketTick / stepSize) * stepSize;
    uint32_t offset = bracketTick - stepStart;
    
    // Convert to CC value
    uint8_t ccValue = (offset * 127) / stepSize;
    
    // Send CC
    midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, ccValue);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent fine fader position: offset=%lu cc=%d", offset, ccValue);
}

void FaderManager::sendNoteValueFaderPosition(Track& track) {
    // Get current note pitch if in pitch edit mode
    uint8_t noteValue = 60; // Default middle C
    
    if (editManager.getCurrentState() == editManager.getPitchNoteState()) {
        // Note: EditPitchNoteState doesn't have getCurrentPitch method
        // This functionality would need to be implemented in the state
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader position: pitch edit mode active");
    }
    
    // Convert to CC value
    uint8_t ccValue = (noteValue * 127) / 127;
    
    // Send CC
    midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER, ccValue);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent note value fader position: note=%d cc=%d", noteValue, ccValue);
}

void FaderManager::sendSelectnoteFaderUpdate(Track& track) {
    pendingSelectnoteUpdate = true;
    selectnoteUpdateTime = millis() + 50; // 50ms delay
}

void FaderManager::performSelectnoteFaderUpdate(Track& track) {
    uint32_t bracketTick = editManager.getBracketTick();
    uint32_t loopLength = track.getLoopLength();
    
    if (loopLength == 0) return;
    
    // Convert bracket tick to pitchbend value
    int16_t pitchValue = PITCHBEND_MIN + (bracketTick * (PITCHBEND_MAX - PITCHBEND_MIN) / loopLength);
    
    // Send pitchbend
    midiHandler.sendPitchBend(PITCHBEND_SELECT_CHANNEL, pitchValue);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent select fader position: tick=%lu pitch=%d", bracketTick, pitchValue);
}

bool FaderManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, 0, 0);
}

bool FaderManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    // Check if we're in a grace period
    if (!startEditingEnabled) {
        return true;
    }
    
    // Check if we're in loop edit mode and this is a note editing fader
    if (editManager.getCurrentMainEditMode() == EditManager::MAIN_MODE_LOOP_EDIT) {
        switch (faderType) {
            case MidiMapping::FaderType::FADER_SELECT:
            case MidiMapping::FaderType::FADER_COARSE:
            case MidiMapping::FaderType::FADER_FINE:
            case MidiMapping::FaderType::FADER_NOTE_VALUE:
                return true; // Ignore note editing faders in loop edit mode
        }
    }
    
    return false;
}

void FaderManager::updateFaderStates() {
    // Update fader states based on current edit mode and track state
    // This method can be called periodically to sync fader positions
}

uint16_t FaderManager::calculateTargetTick(int16_t pitchValue, uint16_t loopLength) {
    // Convert pitchbend value to tick position
    uint32_t normalizedValue = pitchValue - PITCHBEND_MIN;
    uint32_t range = PITCHBEND_MAX - PITCHBEND_MIN;
    
    return (normalizedValue * loopLength) / range;
}

uint8_t FaderManager::calculateTargetStep(int16_t pitchValue, uint8_t numSteps) {
    // Convert pitchbend value to step position
    uint32_t normalizedValue = pitchValue - PITCHBEND_MIN;
    uint32_t range = PITCHBEND_MAX - PITCHBEND_MIN;
    
    return (normalizedValue * numSteps) / range;
}

uint8_t FaderManager::calculateTargetOffset(uint8_t ccValue, uint8_t numSteps) {
    // Convert CC value to offset within a step
    return (ccValue * numSteps) / 127;
}

uint8_t FaderManager::calculateTargetNoteValue(uint8_t ccValue) {
    // Convert CC value to note value (0-127)
    return (ccValue * 127) / 127;
}

void FaderManager::sendStartNotePitchbend(Track& track) {
    // Legacy method for compatibility
    sendCoarseFaderPosition(track);
    sendFineFaderPosition(track);
}

void FaderManager::refreshEditingActivity() {
    lastEditingActivityTime = millis();
    noteSelectionTime = millis();
    startEditingEnabled = false;
} 