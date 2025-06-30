//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiFaderActions.h"
#include "Logger.h"
#include "TrackManager.h"
#include "NoteEditManager.h"

extern NoteEditManager noteEditManager;

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
    handleSelectFaderInput(pitchbendValue, track);
}

void MidiFaderActions::handleMoveNoteCoarse(int16_t pitchbendValue) {
    Track& track = trackManager.getSelectedTrack();
    handleCoarseFaderInput(pitchbendValue, track);
}

void MidiFaderActions::handleMoveNoteFine(uint8_t ccValue) {
    Track& track = trackManager.getSelectedTrack();
    handleFineFaderInput(ccValue, track);
}

void MidiFaderActions::handleChangeNoteValue(uint8_t ccValue) {
    Track& track = trackManager.getSelectedTrack();
    handleNoteValueFaderInput(ccValue, track);
}

void MidiFaderActions::handleSelectFaderInput(int16_t pitchbendValue, Track& track) {
    // Delegate to the existing NoteEditManager logic for note selection
    logger.log(CAT_MIDI, LOG_DEBUG, "Executing fader action: type=1 fader=1 pitchbend=%d cc=0", pitchbendValue);
    noteEditManager.handleSelectFaderInput(pitchbendValue, track);
}

void MidiFaderActions::handleCoarseFaderInput(int16_t pitchbendValue, Track& track) {
    // Delegate to the existing NoteEditManager logic for coarse movement
    logger.log(CAT_MIDI, LOG_DEBUG, "Executing fader action: type=2 fader=2 pitchbend=%d cc=0", pitchbendValue);
    noteEditManager.handleCoarseFaderInput(pitchbendValue, track);
}

void MidiFaderActions::handleFineFaderInput(uint8_t ccValue, Track& track) {
    // Delegate to the existing NoteEditManager logic for fine movement
    logger.log(CAT_MIDI, LOG_DEBUG, "Executing fader action: type=3 fader=3 pitchbend=0 cc=%d", ccValue);
    noteEditManager.handleFineFaderInput(ccValue, track);
}

void MidiFaderActions::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Delegate to the existing NoteEditManager logic for note value changes
    logger.log(CAT_MIDI, LOG_DEBUG, "Executing fader action: type=4 fader=4 pitchbend=0 cc=%d", ccValue);
    noteEditManager.handleNoteValueFaderInput(ccValue, track);
} 