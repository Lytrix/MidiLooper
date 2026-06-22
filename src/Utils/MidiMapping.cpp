// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MidiMapping.h"
#include "Logger.h"

namespace MidiMapping {

// Static member definitions
std::vector<ButtonConfig> Config::buttonConfigs;
std::vector<FaderConfig> Config::faderConfigs;
EncoderConfig Config::encoderConfig(
    MidiConfig::Encoder::DEFAULT_CHANNEL,
    MidiConfig::Encoder::DEFAULT_CC,
    MidiConfig::Encoder::UP_VALUE,
    MidiConfig::Encoder::DOWN_VALUE,
    "Default Encoder"
);

static bool isInitialized = false;

void Config::initialize() {
    if (isInitialized) return;
    
    // Clear existing configurations
    buttonConfigs.clear();
    faderConfigs.clear();
    
    // Initialize default button mappings
    buttonConfigs = {
        ButtonConfig(MidiConfig::Transport::NOTE_RECORD, MidiConfig::Channels::DEFAULT, "Record/Stop"),
        ButtonConfig(MidiConfig::Transport::NOTE_PLAY, MidiConfig::Channels::DEFAULT, "Play/Stop"),
        ButtonConfig(MidiConfig::Transport::NOTE_EDIT_MODE, MidiConfig::Channels::DEFAULT, "Edit Mode"),
        ButtonConfig(MidiConfig::Transport::NOTE_REDO, MidiConfig::Channels::DEFAULT, "Redo")
    };
    
    // Initialize default fader mappings
    faderConfigs = {
        FaderConfig(FaderType::FADER_SELECT, MidiConfig::Fader::SELECT_CHANNEL, 0, true, "Note Selection"),
        FaderConfig(FaderType::FADER_COARSE, MidiConfig::Fader::COARSE_CHANNEL, 0, true, "Coarse Position"),
        FaderConfig(FaderType::FADER_FINE, MidiConfig::Fader::FINE_CHANNEL, MidiConfig::Fader::FINE_CC, false, "Fine Position"),
        FaderConfig(FaderType::FADER_NOTE_VALUE, MidiConfig::Fader::NOTE_VALUE_CHANNEL, MidiConfig::Fader::NOTE_VALUE_CC, false, "Note Value")
    };
    
    // Add fallback mappings for fader 4 in case it's sending different CC numbers
    faderConfigs.emplace_back(FaderType::FADER_NOTE_VALUE, MidiConfig::Fader::COARSE_CHANNEL, 4, false, "Note Value (CC4)");
    faderConfigs.emplace_back(FaderType::FADER_NOTE_VALUE, MidiConfig::Fader::COARSE_CHANNEL, 5, false, "Note Value (CC5)");
    faderConfigs.emplace_back(FaderType::FADER_NOTE_VALUE, MidiConfig::Fader::COARSE_CHANNEL, 6, false, "Note Value (CC6)");
    
    isInitialized = true;
    logger.info("MidiMapping configuration initialized with default values");
}

void Config::addButtonMapping(uint8_t note, uint8_t channel, const std::string& description) {
    // Check for duplicate note mappings
    for (const auto& config : buttonConfigs) {
        if (config.note == note && config.channel == channel) {
            logger.warning("Duplicate MIDI button mapping detected: note %d on channel %d", note, channel);
            return;
        }
    }
    
    buttonConfigs.emplace_back(note, channel, description);
    logger.info("Added MIDI button mapping: %s (note %d, channel %d)", description.c_str(), note, channel);
}

void Config::addFaderMapping(uint8_t channel, uint8_t ccNumber, bool usePitchBend, const std::string& description) {
    // Check for duplicate fader mappings
    for (const auto& config : faderConfigs) {
        if (config.channel == channel && 
            ((usePitchBend && config.usePitchBend) || 
             (!usePitchBend && config.ccNumber == ccNumber))) {
            logger.warning("Duplicate MIDI fader mapping detected on channel %d", channel);
            return;
        }
    }
    
    FaderType type;
    if (channel == MidiConfig::Fader::SELECT_CHANNEL) {
        type = FaderType::FADER_SELECT;
    } else if (usePitchBend) {
        type = FaderType::FADER_COARSE;
    } else if (ccNumber == MidiConfig::Fader::FINE_CC) {
        type = FaderType::FADER_FINE;
    } else {
        type = FaderType::FADER_NOTE_VALUE;
    }
    
    faderConfigs.emplace_back(type, channel, ccNumber, usePitchBend, description);
    logger.info("Added MIDI fader mapping: %s (channel %d, %s)", 
                description.c_str(), channel, 
                usePitchBend ? "pitchbend" : ("CC" + std::to_string(ccNumber)).c_str());
}

void Config::setEncoderMapping(uint8_t channel, uint8_t ccNumber, uint8_t upValue, uint8_t downValue, const std::string& description) {
    encoderConfig = EncoderConfig(channel, ccNumber, upValue, downValue, description);
    logger.info("Updated MIDI encoder mapping: %s (channel %d, CC%d)", description.c_str(), channel, ccNumber);
}

} // namespace MidiMapping 