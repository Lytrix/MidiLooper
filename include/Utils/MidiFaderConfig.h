//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_FADER_CONFIG_H
#define MIDI_FADER_CONFIG_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "Utils/MidiMapping.h"

/**
 * @namespace MidiFaderConfig
 * @brief Configuration system for MIDI faders, providing a simplified interface
 * similar to MidiButtonConfig but for fader assignments and actions.
 */
namespace MidiFaderConfig {

    /**
     * @enum ActionType
     * @brief Types of actions that can be triggered by fader movements
     */
    enum class ActionType {
        NONE = 0,
        SELECT_NOTE,           // Select different note (fader 1)
        MOVE_NOTE_COARSE,      // Move note position coarsely (fader 2)
        MOVE_NOTE_FINE,        // Move note position finely (fader 3)
        CHANGE_NOTE_VALUE,     // Change note pitch value (fader 4)
        CUSTOM_ACTION
    };

    /**
     * @enum InputType
     * @brief Types of MIDI input for faders
     */
    enum class InputType {
        PITCHBEND,
        CC_CONTROL
    };

    /**
     * @struct FaderConfig
     * @brief Configuration for a single fader
     */
    struct FaderConfig {
        MidiMapping::FaderType type;
        uint8_t channel;
        uint8_t ccNumber;        // Only used for CC-based faders
        InputType inputType;
        ActionType action;
        uint8_t parameter;       // Optional parameter for the action
        const char* description;
        // Processing parameters (externalized from processor)
        uint16_t feedbackIgnoreMs;   // How long to ignore feedback after sending (ms)
        int16_t pitchbendDeadband;   // Deadband for pitchbend inputs
        uint8_t ccDeadband;          // Deadband for CC inputs
        int16_t pitchbendCenter;     // Center initialization value for pitchbend
        uint8_t initialCC;           // Initial CC value
        uint8_t groupKey;            // Grouping key for feedback grouping (0 = use channel)
        
        // Constructor for pitchbend faders
        FaderConfig(MidiMapping::FaderType faderType, uint8_t ch, const char* desc) 
            : type(faderType), channel(ch), ccNumber(0), inputType(InputType::PITCHBEND), 
              action(ActionType::NONE), parameter(0), description(desc),
              feedbackIgnoreMs(100), pitchbendDeadband(23), ccDeadband(1),
              pitchbendCenter(0), initialCC(64), groupKey(0) {}
        
        // Constructor for CC faders
        FaderConfig(MidiMapping::FaderType faderType, uint8_t ch, uint8_t cc, const char* desc)
            : type(faderType), channel(ch), ccNumber(cc), inputType(InputType::CC_CONTROL),
              action(ActionType::NONE), parameter(0), description(desc),
              feedbackIgnoreMs(100), pitchbendDeadband(23), ccDeadband(1),
              pitchbendCenter(0), initialCC(64), groupKey(0) {}
        
        // Fluent interface for setting actions
        FaderConfig& withAction(ActionType actionType, uint8_t param = 0) {
            action = actionType;
            parameter = param;
            return *this;
        }
        // Fluent setters for processing parameters
        FaderConfig& withFeedbackIgnore(uint16_t ms) {
            feedbackIgnoreMs = ms; return *this;
        }
        FaderConfig& withDeadbands(int16_t pbDeadband, uint8_t ccDb) {
            pitchbendDeadband = pbDeadband; ccDeadband = ccDb; return *this;
        }
        FaderConfig& withCenters(int16_t pbCenter, uint8_t initialCc) {
            pitchbendCenter = pbCenter; initialCC = initialCc; return *this;
        }
        FaderConfig& withGroup(uint8_t key) {
            groupKey = key; return *this;
        }
    };

    /**
     * @class Config
     * @brief Manages fader configurations
     */
    class Config {
    public:
        static void initialize();
        static void loadBasicConfiguration();
        static void loadExtendedConfiguration();
        
        static const FaderConfig* findFaderConfig(MidiMapping::FaderType faderType);
        static const FaderConfig* findFaderConfigByChannel(uint8_t channel, InputType inputType, uint8_t ccNumber = 0);
        
        static void addFader(const FaderConfig& config);
        static const std::vector<FaderConfig>& getFaderConfigs() { return faderConfigs; }
        
        static void printConfiguration();
        
    private:
        static std::vector<FaderConfig> faderConfigs;
        static bool isInitialized;
    };

} // namespace MidiFaderConfig

#endif // MIDI_FADER_CONFIG_H 