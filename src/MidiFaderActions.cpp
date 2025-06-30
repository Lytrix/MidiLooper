//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiFaderActions.h"
#include "Logger.h"
#include "TrackManager.h"
#include "MidiButtonManager.h"

MidiFaderActions::MidiFaderActions() {
}

void MidiFaderActions::executeAction(MidiFaderConfig::ActionType action, 
                                   MidiMapping::FaderType faderType,
                                   int16_t pitchbendValue, 
                                   uint8_t ccValue, 
                                   uint8_t parameter) {
    logger.log(CAT_MIDI, LOG_DEBUG, "Executing fader action: type=%d fader=%d pitchbend=%d cc=%d", 
               (int)action, (int)faderType, pitchbendValue, ccValue);
    
    switch (action) {
        case MidiFaderConfig::ActionType::SELECT_NOTE:
            handleSelectNote(pitchbendValue);
            break;
            
        case MidiFaderConfig::ActionType::MOVE_NOTE_COARSE:
            handleMoveNoteCoarse(pitchbendValue);
            break;
            
        case MidiFaderConfig::ActionType::MOVE_NOTE_FINE:
            handleMoveNoteFine(ccValue);
            break;
            
        case MidiFaderConfig::ActionType::CHANGE_NOTE_VALUE:
            handleChangeNoteValue(ccValue);
            break;
            
        case MidiFaderConfig::ActionType::CUSTOM_ACTION:
            logger.log(CAT_MIDI, LOG_DEBUG, "Custom fader action not yet implemented");
            break;
            
        default:
            logger.log(CAT_MIDI, LOG_DEBUG, "Unknown fader action: %d", (int)action);
            break;
    }
}

void MidiFaderActions::handleSelectNote(int16_t pitchbendValue) {
    Track& track = trackManager.getSelectedTrack();
    delegateSelectFaderInput(pitchbendValue, track);
}

void MidiFaderActions::handleMoveNoteCoarse(int16_t pitchbendValue) {
    Track& track = trackManager.getSelectedTrack();
    delegateCoarseFaderInput(pitchbendValue, track);
}

void MidiFaderActions::handleMoveNoteFine(uint8_t ccValue) {
    Track& track = trackManager.getSelectedTrack();
    delegateFineFaderInput(ccValue, track);
}

void MidiFaderActions::handleChangeNoteValue(uint8_t ccValue) {
    Track& track = trackManager.getSelectedTrack();
    delegateNoteValueFaderInput(ccValue, track);
}

void MidiFaderActions::delegateSelectFaderInput(int16_t pitchbendValue, Track& track) {
    // Delegate to the existing MidiButtonManager logic for note selection
    // This calls the handleSelectFaderInput method from the original implementation
    midiButtonManager.handleSelectFaderInput(pitchbendValue, track);
}

void MidiFaderActions::delegateCoarseFaderInput(int16_t pitchbendValue, Track& track) {
    // Delegate to the existing MidiButtonManager logic for coarse movement
    // This calls the handleCoarseFaderInput method from the original implementation
    midiButtonManager.handleCoarseFaderInput(pitchbendValue, track);
}

void MidiFaderActions::delegateFineFaderInput(uint8_t ccValue, Track& track) {
    // Delegate to the existing MidiButtonManager logic for fine movement
    // This calls the handleFineFaderInput method from the original implementation
    midiButtonManager.handleFineFaderInput(ccValue, track);
}

void MidiFaderActions::delegateNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Delegate to the existing MidiButtonManager logic for note value changes
    // This calls the handleNoteValueFaderInput method from the original implementation
    midiButtonManager.handleNoteValueFaderInput(ccValue, track);
} 