//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiButtonProcessor.h"
#include "Logger.h"

MidiButtonProcessor::MidiButtonProcessor() {
    // Initialize button states for all possible MIDI notes on all channels
    // Size: 16 channels * 128 notes = 2048 states (but we only use what we need)
    buttonStates.resize(16 * 128);
}

void MidiButtonProcessor::setup() {
    // Clear all button states
    for (auto& state : buttonStates) {
        state = ButtonState();
    }
    logger.info("MidiButtonProcessor setup complete");
}

void MidiButtonProcessor::update() {
    processPendingPresses();
}

void MidiButtonProcessor::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    uint32_t now = millis();
    ButtonState& state = getButtonState(channel, note);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Button Processor: Ch%d Note%d %s (vel %d)", 
               channel, note, isNoteOn ? "ON" : "OFF", velocity);
    
    if (isNoteOn && velocity > 0) {
        // Note On - Button Press
        if (!state.isPressed) {
            state.isPressed = true;
            state.pressStartTime = now;
            logger.log(CAT_BUTTON, LOG_DEBUG, "Button pressed: Ch%d Note%d", channel, note);
        }
    } else {
        // Note Off - Button Release
        if (state.isPressed) {
            state.isPressed = false;
            uint32_t duration = now - state.pressStartTime;
            handleButtonRelease(channel, note, duration);
            logger.log(CAT_BUTTON, LOG_DEBUG, "Button released: Ch%d Note%d, duration: %dms", 
                       channel, note, duration);
        }
    }
}

void MidiButtonProcessor::handleButtonRelease(uint8_t channel, uint8_t note, uint32_t pressDuration) {
    uint32_t now = millis();
    ButtonState& state = getButtonState(channel, note);
    
    if (pressDuration >= longPressTime) {
        // Long press - trigger immediately and cancel pending presses
        state.lastTapTime = 0;
        state.secondTapTime = 0;
        state.pendingShortPress = false;
        state.pendingDoublePress = false;
        state.pendingTriplePress = false;
        
        triggerButtonPress(note, channel, MidiButtonConfig::PressType::LONG_PRESS);
    } else {
        // Short press - check for multiple taps
        if (state.pendingDoublePress && (now - state.secondTapTime <= tripleTapWindow)) {
            // Third tap within window - triple press
            state.lastTapTime = 0;
            state.secondTapTime = 0;
            state.pendingShortPress = false;
            state.pendingDoublePress = false;
            state.pendingTriplePress = false;
            
            triggerButtonPress(note, channel, MidiButtonConfig::PressType::TRIPLE_PRESS);
        } else if (state.lastTapTime > 0 && (now - state.lastTapTime <= doubleTapWindow)) {
            // Second tap within window - set up for potential triple tap
            state.secondTapTime = now;
            state.pendingShortPress = false;
            state.pendingDoublePress = true;
            state.doublePressExpireTime = now + tripleTapWindow;
        } else {
            // First tap or outside double tap window - delay decision
            state.lastTapTime = now;
            state.pendingShortPress = true;
            state.shortPressExpireTime = now + doubleTapWindow;
        }
    }
}

void MidiButtonProcessor::processPendingPresses() {
    uint32_t now = millis();
    
    for (size_t i = 0; i < buttonStates.size(); ++i) {
        ButtonState& state = buttonStates[i];
        
        // Calculate channel and note from index
        uint8_t channel = i / 128;
        uint8_t note = i % 128;
        
        // Process expired short presses
        if (state.pendingShortPress && now >= state.shortPressExpireTime) {
            state.pendingShortPress = false;
            triggerButtonPress(note, channel, MidiButtonConfig::PressType::SHORT_PRESS);
        }
        
        // Process expired double presses
        if (state.pendingDoublePress && now >= state.doublePressExpireTime) {
            state.pendingDoublePress = false;
            triggerButtonPress(note, channel, MidiButtonConfig::PressType::DOUBLE_PRESS);
        }
        
        // Process expired triple presses
        if (state.pendingTriplePress && now >= state.triplePressExpireTime) {
            state.pendingTriplePress = false;
            triggerButtonPress(note, channel, MidiButtonConfig::PressType::TRIPLE_PRESS);
        }
    }
}

void MidiButtonProcessor::triggerButtonPress(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType) {
    logger.log(CAT_BUTTON, LOG_DEBUG, "Button press triggered: Ch%d Note%d Type%d", 
               channel, note, static_cast<int>(pressType));
    
    if (buttonPressCallback) {
        buttonPressCallback(note, channel, pressType);
    }
}

void MidiButtonProcessor::setButtonPressCallback(ButtonPressCallback callback) {
    buttonPressCallback = callback;
}

bool MidiButtonProcessor::isButtonPressed(uint8_t note, uint8_t channel) const {
    return getButtonState(channel, note).isPressed;
}

uint32_t MidiButtonProcessor::getButtonPressStartTime(uint8_t note, uint8_t channel) const {
    return getButtonState(channel, note).pressStartTime;
}

size_t MidiButtonProcessor::getButtonIndex(uint8_t channel, uint8_t note) const {
    return channel * 128 + note;
}

MidiButtonProcessor::ButtonState& MidiButtonProcessor::getButtonState(uint8_t channel, uint8_t note) {
    size_t index = getButtonIndex(channel, note);
    return buttonStates[index];
}

const MidiButtonProcessor::ButtonState& MidiButtonProcessor::getButtonState(uint8_t channel, uint8_t note) const {
    size_t index = getButtonIndex(channel, note);
    return buttonStates[index];
} 