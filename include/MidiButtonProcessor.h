//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_BUTTON_PROCESSOR_H
#define MIDI_BUTTON_PROCESSOR_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include <functional>
#include "Utils/MidiButtonConfig.h"

class MidiButtonProcessor {
public:
    // Button press callback function type
    using ButtonPressCallback = std::function<void(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType)>;
    
    MidiButtonProcessor();
    
    void setup();
    void update();
    void handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn);
    
    // Set callback for button press events
    void setButtonPressCallback(ButtonPressCallback callback);
    
    // Query button states
    bool isButtonPressed(uint8_t note, uint8_t channel) const;
    uint32_t getButtonPressStartTime(uint8_t note, uint8_t channel) const;
    
    // Configuration
    void setDoubleTapWindow(uint32_t windowMs) { doubleTapWindow = windowMs; }
    void setLongPressTime(uint32_t timeMs) { longPressTime = timeMs; }
    void setTripleTapWindow(uint32_t windowMs) { tripleTapWindow = windowMs; }
    
private:
    struct ButtonState {
        bool isPressed;
        uint32_t pressStartTime;
        uint32_t lastTapTime;
        uint32_t secondTapTime;
        bool pendingShortPress;
        uint32_t shortPressExpireTime;
        bool pendingDoublePress;
        uint32_t doublePressExpireTime;
        bool pendingTriplePress;
        uint32_t triplePressExpireTime;
        
        ButtonState() : isPressed(false), pressStartTime(0), lastTapTime(0), secondTapTime(0),
                       pendingShortPress(false), shortPressExpireTime(0),
                       pendingDoublePress(false), doublePressExpireTime(0),
                       pendingTriplePress(false), triplePressExpireTime(0) {}
    };
    
    // Button state storage - indexed by (channel * 128 + note)
    std::vector<ButtonState> buttonStates;
    
    // Timing configuration
    uint32_t doubleTapWindow = 300;  // ms
    uint32_t tripleTapWindow = 400;  // ms 
    uint32_t longPressTime = 600;    // ms
    
    // Callback for button press events
    ButtonPressCallback buttonPressCallback;
    
    // Helper methods
    size_t getButtonIndex(uint8_t channel, uint8_t note) const;
    ButtonState& getButtonState(uint8_t channel, uint8_t note);
    const ButtonState& getButtonState(uint8_t channel, uint8_t note) const;
    
    void processPendingPresses();
    void handleButtonRelease(uint8_t channel, uint8_t note, uint32_t pressDuration);
    void triggerButtonPress(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType);
};

#endif // MIDI_BUTTON_PROCESSOR_H 