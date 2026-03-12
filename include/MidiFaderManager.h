//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_FADER_MANAGER_H
#define MIDI_FADER_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include "MidiFaderProcessor.h"
#include "MidiFaderActions.h"
#include "Utils/MidiFaderConfig.h"
#include "Utils/MidiMapping.h"

/**
 * @class MidiFaderManager
 * @brief Simplified MIDI fader manager that coordinates between fader processing and actions.
 * 
 * This class separates concerns:
 * - MidiFaderProcessor handles fader state and movement detection
 * - MidiFaderActions handles action execution
 * - MidiFaderConfig manages fader configurations
 * - This class just coordinates between them
 */
class MidiFaderManager {
public:
    MidiFaderManager();
    
    void setup();
    void update();
    void handleMidiPitchbend(uint8_t channel, int16_t pitchValue);
    void handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value);
    
    // Configuration management
    void loadFaderConfiguration(const char* configName = "basic");
    void addCustomFader(MidiMapping::FaderType faderType, uint8_t channel, 
                       const char* description, MidiFaderConfig::ActionType action);
    
    // Query fader states (delegated to processor)
    MidiMapping::FaderType getCurrentDriverFader() const;
    const MidiFaderProcessor::FaderState& getFaderState(MidiMapping::FaderType faderType) const;
    MidiFaderProcessor::FaderState& getFaderStateMutable(MidiMapping::FaderType faderType);
    
    // Access to processor for external coordination
    MidiFaderProcessor& getProcessor() { return processor; }
    
    // Fader update control
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    void markFaderSent(MidiMapping::FaderType faderType);
    
    // Statistics and debugging
    void printFaderConfiguration() const;
    uint32_t getConfiguredFaderCount() const;
    
private:
    MidiFaderProcessor processor;
    MidiFaderActions actions;
    
    // Fader movement callback - called by processor when a fader movement is detected
    void onFaderMovement(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    
    // Validation
    bool isValidChannel(uint8_t channel) const;
    bool isValidPitchbend(int16_t pitchValue) const;
    bool isValidCC(uint8_t ccValue) const;
};

extern MidiFaderManager midiFaderManager;

#endif // MIDI_FADER_MANAGER_H
