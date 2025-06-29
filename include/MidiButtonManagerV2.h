//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_BUTTON_MANAGER_V2_H
#define MIDI_BUTTON_MANAGER_V2_H

#include <Arduino.h>
#include <cstdint>
#include "MidiButtonProcessor.h"
#include "MidiButtonActions.h"
#include "Utils/MidiButtonConfig.h"

/**
 * @class MidiButtonManagerV2
 * @brief Simplified MIDI button manager that coordinates between button processing and actions.
 * 
 * This is a refactored version that separates concerns:
 * - MidiButtonProcessor handles button state and press detection
 * - MidiButtonActions handles action execution
 * - MidiButtonConfig manages button configurations
 * - This class just coordinates between them
 */
class MidiButtonManagerV2 {
public:
    MidiButtonManagerV2();
    
    void setup();
    void update();
    void handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn);
    
    // Configuration management
    void loadButtonConfiguration(const char* configName = "basic");
    void addCustomButton(uint8_t note, uint8_t channel, const char* description,
                        MidiButtonConfig::ActionType shortAction = MidiButtonConfig::ActionType::NONE,
                        MidiButtonConfig::ActionType longAction = MidiButtonConfig::ActionType::NONE);
    
    // Query button states (delegated to processor)
    bool isButtonPressed(uint8_t note, uint8_t channel) const;
    uint32_t getButtonPressStartTime(uint8_t note, uint8_t channel) const;
    
    // Statistics and debugging
    void printButtonConfiguration() const;
    uint32_t getConfiguredButtonCount() const;
    
private:
    MidiButtonProcessor processor;
    MidiButtonActions actions;
    
    // Button press callback - called by processor when a button press is detected
    void onButtonPress(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType);
    
    // Validation
    bool isValidChannel(uint8_t channel) const;
    bool isValidNote(uint8_t note) const;
};

extern MidiButtonManagerV2 midiButtonManagerV2;

#endif // MIDI_BUTTON_MANAGER_V2_H 