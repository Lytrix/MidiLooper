//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_FADER_PROCESSOR_H
#define MIDI_FADER_PROCESSOR_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include <functional>
#include "Utils/MidiMapping.h"
#include "Utils/MidiFaderConfig.h"

/**
 * @class MidiFaderProcessor
 * @brief Processes MIDI fader input and manages fader states.
 * 
 * This class handles:
 * - Fader state tracking and deadband filtering
 * - Input validation and feedback prevention
 * - State changes and driver fader management
 * - Callbacks for fader movements
 */
class MidiFaderProcessor {
public:
    /**
     * @struct FaderState
     * @brief Internal state for a single fader
     */
    struct FaderState {
        MidiMapping::FaderType type;
        uint8_t channel;
        bool isInitialized;
        int16_t lastPitchbendValue;
        uint8_t lastCCValue;
        uint32_t lastUpdateTime;
        uint32_t lastSentTime;
        bool pendingUpdate;
        uint32_t updateScheduledTime;
        MidiMapping::FaderType scheduledByDriver;
        int16_t lastSentPitchbend;
        uint8_t lastSentCC;
    };

    /**
     * @typedef FaderMovementCallback
     * @brief Callback function type for fader movements
     * @param faderType The type of fader that moved
     * @param pitchbendValue The pitchbend value (if applicable)
     * @param ccValue The CC value (if applicable)
     */
    using FaderMovementCallback = std::function<void(MidiMapping::FaderType, int16_t, uint8_t)>;

    MidiFaderProcessor();
    
    void setup();
    void update();
    
    // MIDI input handlers
    void handlePitchbend(uint8_t channel, int16_t pitchValue);
    void handleCC(uint8_t channel, uint8_t ccNumber, uint8_t value);
    
    // State management
    void setDriverFader(MidiMapping::FaderType faderType);
    MidiMapping::FaderType getCurrentDriverFader() const { return currentDriverFader; }
    
    // Fader state access
    const FaderState& getFaderState(MidiMapping::FaderType faderType) const;
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue = -1, uint8_t ccValue = 255) const;
    
    // Callback management
    void setFaderMovementCallback(FaderMovementCallback callback) { movementCallback = callback; }
    
    // Update scheduling
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    void markFaderSent(MidiMapping::FaderType faderType);
    
    // Configuration
    void initializeFaderStates();
    
    FaderState& getFaderStateMutable(MidiMapping::FaderType faderType);
    
private:
    std::vector<FaderState> faderStates;
    MidiMapping::FaderType currentDriverFader;
    uint32_t lastDriverFaderTime;
    uint32_t lastDriverFaderUpdateTime;
    uint32_t lastSelectnoteFaderTime;
    
    FaderMovementCallback movementCallback;
    
    // Constants
    static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 100; // ms
    static constexpr int16_t PITCHBEND_DEADBAND = 23;
    static constexpr uint8_t CC_DEADBAND_FINE = 1;
    static constexpr int16_t PITCHBEND_CENTER = 0;
    
    // Helper methods
    bool hasSignificantChange(const FaderState& state, int16_t pitchbendValue, uint8_t ccValue) const;
    void processFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    void commitMovingNote();
};

#endif // MIDI_FADER_PROCESSOR_H 