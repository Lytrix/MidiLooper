//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_FADER_ACTIONS_H
#define MIDI_FADER_ACTIONS_H

#include <Arduino.h>
#include <cstdint>
#include "Utils/MidiFaderConfig.h"
#include "Utils/MidiMapping.h"

// Forward declarations
class Track;
namespace NoteUtils { struct DisplayNote; }

/**
 * @class MidiFaderActions
 * @brief Executes actions triggered by MIDI fader movements.
 * 
 * This class handles the execution of various fader actions such as:
 * - Note selection
 * - Note movement (coarse and fine)
 * - Note value changes
 * 
 * This class is designed to work with the move note logic that remains
 * in MidiButtonManager, delegating complex note operations to it.
 */
class MidiFaderActions {
public:
    MidiFaderActions();
    
    /**
     * @brief Execute a fader action with the given values
     * @param action The action type to execute
     * @param faderType The fader type that triggered the action
     * @param pitchbendValue The pitchbend value (if applicable)
     * @param ccValue The CC value (if applicable)
     * @param parameter Optional parameter for the action
     */
    void executeAction(MidiFaderConfig::ActionType action, 
                      MidiMapping::FaderType faderType,
                      int16_t pitchbendValue, 
                      uint8_t ccValue, 
                      uint8_t parameter = 0);

private:
    // Individual action handlers
    void handleSelectNote(int16_t pitchbendValue);
    void handleMoveNoteCoarse(int16_t pitchbendValue);
    void handleMoveNoteFine(uint8_t ccValue);
    void handleChangeNoteValue(uint8_t ccValue);
    
    // Helper methods for delegating to NoteEditManager
    void handleSelectFaderInput(int16_t pitchbendValue, Track& track);
    void handleCoarseFaderInput(int16_t pitchbendValue, Track& track);
    void handleFineFaderInput(uint8_t ccValue, Track& track);
    void handleNoteValueFaderInput(uint8_t ccValue, Track& track);
};

#endif // MIDI_FADER_ACTIONS_H 