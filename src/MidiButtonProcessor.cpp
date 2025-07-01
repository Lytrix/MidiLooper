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
            logger.log(CAT_BUTTON, LOG_DEBUG, "Button pressed: Ch%d Note%d at time %lu", channel, note, now);
            
            // Special case: Note 3 on Channel 16 - momentary length edit mode (ON = enable length mode)
            if (channel == 16 && note == 3) {
                logger.log(CAT_BUTTON, LOG_DEBUG, "Momentary length edit ON: Ch%d Note%d", channel, note);
                triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::SHORT_PRESS);
            }
        }
    } else {
        // Note Off - Button Release
        if (state.isPressed) {
            state.isPressed = false;
            
            // Special case: Note 3 on Channel 16 - momentary length edit mode (OFF = disable length mode)
            if (channel == 16 && note == 3) {
                logger.log(CAT_BUTTON, LOG_DEBUG, "Momentary length edit OFF: Ch%d Note%d", channel, note);
                triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::SHORT_PRESS);
                return; // Skip normal release handling for momentary button
            }
            
            // Handle millis() overflow properly
            uint32_t duration;
            if (now >= state.pressStartTime) {
                duration = now - state.pressStartTime;
            } else {
                // Handle overflow: now wrapped around, so duration is (MAX_UINT32 - pressStartTime + 1) + now
                duration = (0xFFFFFFFF - state.pressStartTime + 1) + now;
            }
            
            logger.log(CAT_BUTTON, LOG_DEBUG, "Button released: Ch%d Note%d, start=%lu, now=%lu, duration=%lu", 
                       channel, note, state.pressStartTime, now, duration);
            
            handleButtonRelease(channel, note, duration);
        }
    }
}

void MidiButtonProcessor::handleButtonRelease(uint8_t channel, uint8_t note, uint32_t pressDuration) {
    uint32_t now = millis();
    ButtonState& state = getButtonState(channel, note);
    
    logger.log(CAT_BUTTON, LOG_DEBUG, "handleButtonRelease: Ch%d Note%d, duration=%lu, longPressTime=%lu", 
               channel, note, pressDuration, longPressTime);
    
    if (pressDuration >= longPressTime) {
        // Long press - trigger immediately and cancel pending presses
        logger.log(CAT_BUTTON, LOG_DEBUG, "Long press detected: duration=%lu >= longPressTime=%lu", 
                   pressDuration, longPressTime);
        state.lastTapTime = 0;
        state.secondTapTime = 0;
        state.pendingShortPress = false;
        state.pendingDoublePress = false;
        state.pendingTriplePress = false;
        
        triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::LONG_PRESS);
    } else {
        // Short press - check for multiple taps
        logger.log(CAT_BUTTON, LOG_DEBUG, "Short press detected: duration=%lu < longPressTime=%lu", 
                   pressDuration, longPressTime);
        
        if (state.pendingDoublePress && (now - state.secondTapTime <= tripleTapWindow)) {
            // Third tap within window - triple press
            logger.log(CAT_BUTTON, LOG_DEBUG, "Triple press detected");
            state.lastTapTime = 0;
            state.secondTapTime = 0;
            state.pendingShortPress = false;
            state.pendingDoublePress = false;
            state.pendingTriplePress = false;
            
            triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::TRIPLE_PRESS);
        } else if (state.lastTapTime > 0 && (now - state.lastTapTime <= doubleTapWindow)) {
            // Second tap within window - set up for potential triple tap
            logger.log(CAT_BUTTON, LOG_DEBUG, "Second tap detected, waiting for triple");
            state.secondTapTime = now;
            state.pendingShortPress = false;
            state.pendingDoublePress = true;
            state.doublePressExpireTime = now + tripleTapWindow;
        } else {
            // First tap or outside double tap window - delay decision
            logger.log(CAT_BUTTON, LOG_DEBUG, "First tap or outside window, scheduling short press");
            state.lastTapTime = now;
            state.pendingShortPress = true;
            state.shortPressExpireTime = now + doubleTapWindow;
            logger.log(CAT_BUTTON, LOG_DEBUG, "Scheduled short press: expire at %lu (now=%lu + window=%lu)", 
                       state.shortPressExpireTime, now, doubleTapWindow);
        }
    }
}

void MidiButtonProcessor::processPendingPresses() {
    uint32_t now = millis();
    static uint32_t lastDebugTime = 0;
    
    // Print debug info every 100ms
    // if (now - lastDebugTime >= 100) {
    //     lastDebugTime = now;
    //     logger.log(CAT_BUTTON, LOG_DEBUG, "processPendingPresses: current time = %lu", now);
    // }
    
    for (size_t i = 0; i < buttonStates.size(); ++i) {
        ButtonState& state = buttonStates[i];
        
        // Calculate channel and note from index
        uint8_t channel = i / 128;
        uint8_t note = i % 128;
        
        // Process expired short presses
        if (state.pendingShortPress && now >= state.shortPressExpireTime) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Short press expired: Ch%d Note%d, now=%lu, expire=%lu", 
                       channel, note, now, state.shortPressExpireTime);
            state.pendingShortPress = false;
            triggerButtonPress(note, channel, MidiButtonConfig::PressType::SHORT_PRESS);
        } else if (state.pendingShortPress) {
            // Debug: show pending short presses that haven't expired yet
            static uint32_t lastPendingDebugTime = 0;
            if (now - lastPendingDebugTime >= 500) {  // Every 500ms
                lastPendingDebugTime = now;
                logger.log(CAT_BUTTON, LOG_DEBUG, "Pending short press: Ch%d Note%d, now=%lu, expire=%lu, remaining=%ld", 
                           channel, note, now, state.shortPressExpireTime, 
                           (int32_t)state.shortPressExpireTime - (int32_t)now);
            }
        }
        
        // Debug: show when we're checking the specific buttons we care about
        if ((channel == 16 && (note == 36 || note == 37 || note == 38)) && state.pendingShortPress) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Checking button Ch%d Note%d: pending=%d, now=%lu, expire=%lu, should_expire=%d", 
                       channel, note, state.pendingShortPress, now, state.shortPressExpireTime, 
                       (now >= state.shortPressExpireTime));
        }
        
        // Process expired double presses
        if (state.pendingDoublePress && now >= state.doublePressExpireTime) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Double press expired: Ch%d Note%d, now=%lu, expire=%lu", 
                       channel, note, now, state.doublePressExpireTime);
            state.pendingDoublePress = false;
            triggerButtonPress(note, channel, MidiButtonConfig::PressType::DOUBLE_PRESS);
        }
        
        // Process expired triple presses
        if (state.pendingTriplePress && now >= state.triplePressExpireTime) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Triple press expired: Ch%d Note%d, now=%lu, expire=%lu", 
                       channel, note, now, state.triplePressExpireTime);
            state.pendingTriplePress = false;
            triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::TRIPLE_PRESS);
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
    // Convert 1-based MIDI channel to 0-based indexing
    uint8_t channelIndex = channel - 1;
    return channelIndex * 128 + note;
}

MidiButtonProcessor::ButtonState& MidiButtonProcessor::getButtonState(uint8_t channel, uint8_t note) {
    size_t index = getButtonIndex(channel, note);
    return buttonStates[index];
}

const MidiButtonProcessor::ButtonState& MidiButtonProcessor::getButtonState(uint8_t channel, uint8_t note) const {
    size_t index = getButtonIndex(channel, note);
    return buttonStates[index];
} 