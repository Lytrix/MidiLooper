// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_MAPPING_H
#define MIDI_MAPPING_H

#include <cstdint>
#include <vector>
#include <string>

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
    FADER_COARSE = 2,     // Fader 2: Coarse positioning (channel 15, pitchbend)  
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

// Default MIDI configuration
namespace Defaults {
    // MIDI channels
    constexpr uint8_t BUTTON_CHANNEL = 1;
    constexpr uint8_t FADER_CHANNEL = 15;
    constexpr uint8_t SELECT_CHANNEL = 16;
    constexpr uint8_t ENCODER_CHANNEL = 1;  // Same as button channel
    
    // MIDI notes for buttons
    constexpr uint8_t NOTE_C2 = 36;  // Button A
    constexpr uint8_t NOTE_C2_SHARP = 37;  // Button B
    constexpr uint8_t NOTE_D2 = 38;  // Button C
    constexpr uint8_t NOTE_D2_SHARP = 39;  // Button D
    
    // MIDI CC numbers
    constexpr uint8_t CC_FINE = 2;
    constexpr uint8_t CC_NOTE_VALUE = 3;
    constexpr uint8_t ENCODER_CC = 4;
    
    // Encoder values
    constexpr uint8_t ENCODER_UP = 127;
    constexpr uint8_t ENCODER_DOWN = 0;
    
    // Pitchbend values
    constexpr int16_t PITCHBEND_MIN = -8192;
    constexpr int16_t PITCHBEND_CENTER = 0;
    constexpr int16_t PITCHBEND_MAX = 8191;
}

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