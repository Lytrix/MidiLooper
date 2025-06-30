//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MidiFaderConfig.h"
#include "Logger.h"

namespace MidiFaderConfig {

// Static member definitions
std::vector<FaderConfig> Config::faderConfigs;
bool Config::isInitialized = false;

void Config::initialize() {
    if (isInitialized) return;
    
    faderConfigs.clear();
    isInitialized = true;
    
    logger.info("MidiFaderConfig initialized");
}

void Config::loadBasicConfiguration() {
    initialize();
    faderConfigs.clear();
    
    // Fader 1: Note Selection (Channel 16, Pitchbend)
    FaderConfig fader1(MidiMapping::FaderType::FADER_SELECT, 16, "Note Selection");
    fader1.withAction(ActionType::SELECT_NOTE);
    faderConfigs.push_back(fader1);
    
    // Fader 2: Coarse Note Movement (Channel 15, Pitchbend)
    FaderConfig fader2(MidiMapping::FaderType::FADER_COARSE, 15, "Coarse Movement");
    fader2.withAction(ActionType::MOVE_NOTE_COARSE);
    faderConfigs.push_back(fader2);
    
    // Fader 3: Fine Note Movement (Channel 15, CC2)
    FaderConfig fader3(MidiMapping::FaderType::FADER_FINE, 15, 2, "Fine Movement");
    fader3.withAction(ActionType::MOVE_NOTE_FINE);
    faderConfigs.push_back(fader3);
    
    // Fader 4: Note Value Change (Channel 15, CC3)
    FaderConfig fader4(MidiMapping::FaderType::FADER_NOTE_VALUE, 15, 3, "Note Value");
    fader4.withAction(ActionType::CHANGE_NOTE_VALUE);
    faderConfigs.push_back(fader4);
    
    logger.info("Loaded basic fader configuration: %d faders", faderConfigs.size());
}

void Config::loadExtendedConfiguration() {
    loadBasicConfiguration();
    
    // Add fallback CC mappings for fader 4 (as in original system)
    FaderConfig fader4_cc4(MidiMapping::FaderType::FADER_NOTE_VALUE, 15, 4, "Note Value (CC4)");
    fader4_cc4.withAction(ActionType::CHANGE_NOTE_VALUE);
    faderConfigs.push_back(fader4_cc4);
    
    FaderConfig fader4_cc5(MidiMapping::FaderType::FADER_NOTE_VALUE, 15, 5, "Note Value (CC5)");
    fader4_cc5.withAction(ActionType::CHANGE_NOTE_VALUE);
    faderConfigs.push_back(fader4_cc5);
    
    FaderConfig fader4_cc6(MidiMapping::FaderType::FADER_NOTE_VALUE, 15, 6, "Note Value (CC6)");
    fader4_cc6.withAction(ActionType::CHANGE_NOTE_VALUE);
    faderConfigs.push_back(fader4_cc6);
    
    logger.info("Loaded extended fader configuration: %d faders", faderConfigs.size());
}

const FaderConfig* Config::findFaderConfig(MidiMapping::FaderType faderType) {
    for (const auto& config : faderConfigs) {
        if (config.type == faderType) {
            return &config;
        }
    }
    return nullptr;
}

const FaderConfig* Config::findFaderConfigByChannel(uint8_t channel, InputType inputType, uint8_t ccNumber) {
    for (const auto& config : faderConfigs) {
        if (config.channel == channel && config.inputType == inputType) {
            if (inputType == InputType::PITCHBEND) {
                return &config;
            } else if (inputType == InputType::CC_CONTROL && config.ccNumber == ccNumber) {
                return &config;
            }
        }
    }
    return nullptr;
}

void Config::addFader(const FaderConfig& config) {
    // Check for duplicate configurations
    for (const auto& existing : faderConfigs) {
        if (existing.type == config.type) {
            logger.warning("Duplicate fader configuration for type %d", (int)config.type);
            return;
        }
    }
    
    faderConfigs.push_back(config);
    logger.info("Added fader: %s (ch %d, type %d)", 
                config.description, config.channel, (int)config.type);
}

void Config::printConfiguration() {
    logger.info("Fader Configuration (%d faders):", faderConfigs.size());
    logger.info("Type  Ch  Input      CC   Action           Description");
    logger.info("----  --  ---------  ---  ---------------  --------------------------");
    
    for (const auto& config : faderConfigs) {
        const char* inputStr = (config.inputType == InputType::PITCHBEND) ? "Pitchbend" : "CC";
        const char* actionStr = "Unknown";
        
        switch (config.action) {
            case ActionType::SELECT_NOTE: actionStr = "Select Note"; break;
            case ActionType::MOVE_NOTE_COARSE: actionStr = "Move Coarse"; break;
            case ActionType::MOVE_NOTE_FINE: actionStr = "Move Fine"; break;
            case ActionType::CHANGE_NOTE_VALUE: actionStr = "Change Value"; break;
            case ActionType::CUSTOM_ACTION: actionStr = "Custom"; break;
            default: actionStr = "None"; break;
        }
        
        if (config.inputType == InputType::PITCHBEND) {
            logger.info("%-4d  %-2d  %-9s  ---  %-15s  %s",
                       (int)config.type, config.channel, inputStr, actionStr, config.description);
        } else {
            logger.info("%-4d  %-2d  %-9s  %-3d  %-15s  %s",
                       (int)config.type, config.channel, inputStr, config.ccNumber, actionStr, config.description);
        }
    }
}

} // namespace MidiFaderConfig 