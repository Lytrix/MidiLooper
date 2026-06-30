// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_MAPPING_H
#define MIDI_MAPPING_H

#include <cstdint>
#include <vector>
#include <string>
#include "MidiConfig.h"

namespace MidiMapping {

// Button action types
enum class ButtonAction {
    NONE,
    SHORT_PRESS,
    DOUBLE_PRESS,
    TRIPLE_PRESS,
    LONG_PRESS
};

// Fader types
enum class FaderType {
    FADER_SELECT = 1,     // Fader 1: Note selection (channel 16, pitchbend)
    FADER_COARSE = 2,     // Fader 2: Coarse positioning (channel 14, pitchbend)
    FADER_FINE = 3,       // Fader 3: Fine positioning (channel 15, CC2)
    FADER_NOTE_VALUE = 4  // Fader 4: Note value editing (channel 15, CC3)
};

// Button configuration
struct ButtonConfig {
    uint8_t note;
    uint8_t channel;
    std::string description;
    
    ButtonConfig(uint8_t n, uint8_t ch, const std::string& desc) 
        : note(n), channel(ch), description(desc) {}
};

// Fader configuration
struct FaderConfig {
    FaderType type;
    uint8_t channel;
    uint8_t ccNumber;
    bool usePitchBend;
    std::string description;
    
    FaderConfig(FaderType t, uint8_t ch, uint8_t cc, bool usePb, const std::string& desc)
        : type(t), channel(ch), ccNumber(cc), usePitchBend(usePb), description(desc) {}
};

// Encoder configuration
struct EncoderConfig {
    uint8_t channel;
    uint8_t ccNumber;
    uint8_t upValue;
    uint8_t downValue;
    std::string description;
    
    EncoderConfig(uint8_t ch, uint8_t cc, uint8_t up, uint8_t down, const std::string& desc)
        : channel(ch), ccNumber(cc), upValue(up), downValue(down), description(desc) {}
};

// Configuration class
class Config {
public:
    static void initialize();
    static const std::vector<ButtonConfig>& getButtonConfigs() { return buttonConfigs; }
    static const std::vector<FaderConfig>& getFaderConfigs() { return faderConfigs; }
    static const EncoderConfig& getEncoderConfig() { return encoderConfig; }
    
    // Add new button mapping
    static void addButtonMapping(uint8_t note, uint8_t channel, const std::string& description);
    
    // Add new fader mapping
    static void addFaderMapping(uint8_t channel, uint8_t ccNumber, bool usePitchBend, const std::string& description);
    
    // Update encoder mapping
    static void setEncoderMapping(uint8_t channel, uint8_t ccNumber, uint8_t upValue, uint8_t downValue, const std::string& description);

private:
    static std::vector<ButtonConfig> buttonConfigs;
    static std::vector<FaderConfig> faderConfigs;
    static EncoderConfig encoderConfig;
};

} // namespace MidiMapping

#endif // MIDI_MAPPING_H 