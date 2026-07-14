//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiButtonProcessor.h"
#include "Logger.h"
#include "Utils/DebugSessionCapture.h"

MidiButtonProcessor::MidiButtonProcessor() {
    // Initialize button states for all possible MIDI notes on all channels
    // Size: 16 channels * 128 notes = 2048 states (but we only use what we need)
    buttonStates.resize(16 * 128);
}

void MidiButtonProcessor::setup() {
    // Load timing configuration from MidiButtonConfig
    MidiButtonConfig::Config::getTimingConfig(doubleTapWindow, tripleTapWindow, longPressTime);
    
    // Clear all button states
    for (auto& state : buttonStates) {
        state = ButtonState();
    }
    logger.info("MidiButtonProcessor setup complete - timing: double=%lu, triple=%lu, long=%lu", 
                doubleTapWindow, tripleTapWindow, longPressTime);
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
        if (state.isPressed) {
            // Host re-sent note-on or note-off was lost — re-arm so release duration stays sane.
            logger.log(CAT_BUTTON, LOG_DEBUG,
                       "Button already pressed: Ch%d Note%d, re-arming start (was %lu, now %lu)",
                       channel, note, state.pressStartTime, now);
            state.pressStartTime = now;
            return;
        }

        // Debounce check: ignore NoteOn that arrives too soon after the last release
        // findButtonConfig expects 0-based channel (channel param here is 1-based MIDI)
        const auto* config = MidiButtonConfig::Config::findButtonConfig(note, channel - 1);
        if (config && config->debounceMs > 0 && state.lastReleaseTime > 0 &&
            (now - state.lastReleaseTime) < config->debounceMs) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Debounce: ignoring Ch%d Note%d (%lums since last release)",
                       channel, note, now - state.lastReleaseTime);
            return;
        }

        state.isPressed = true;
        state.pressStartTime = now;
        logger.log(CAT_BUTTON, LOG_DEBUG, "Button pressed: Ch%d Note%d at time %lu", channel, note, now);

        // Check if this is a momentary button (trigger on both press and release)
        if (config && config->isMomentary) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Momentary button press: Ch%d Note%d", channel, note);
            triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::SHORT_PRESS);
        }
    } else {
        // Note Off - Button Release
        if (state.isPressed) {
            state.isPressed = false;
            
            // Check if this is a momentary button (trigger on both press and release)
            const auto* config = MidiButtonConfig::Config::findButtonConfig(note, channel - 1);
            if (config && config->isMomentary) {
                logger.log(CAT_BUTTON, LOG_DEBUG, "Momentary button release: Ch%d Note%d", channel, note);
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

void MidiButtonProcessor::transitionToIdle(ButtonState& state) {
    state.lastTapTime = 0;
    state.secondTapTime = 0;
    state.tapState = TapState::Idle;
    state.tapStateExpireTime = 0;
}

void MidiButtonProcessor::onShortRelease(ButtonState& state, uint8_t channel, uint8_t note, uint32_t now,
                                         uint32_t effectiveDoubleTap, uint32_t effectiveTripleTap,
                                         bool awaitTripleTap) {
    const uint8_t channel0 = channel - 1;  // 0-based for callback

    if (state.tapState == TapState::PendingDouble && (now - state.secondTapTime <= effectiveTripleTap)) {
        // Third tap within window - triple press
        logger.log(CAT_BUTTON, LOG_DEBUG, "Triple press detected");
        transitionToIdle(state);
        triggerButtonPress(note, channel0, MidiButtonConfig::PressType::TRIPLE_PRESS);
    } else if (state.lastTapTime > 0 && (now - state.lastTapTime <= effectiveDoubleTap)) {
        if (!awaitTripleTap) {
            logger.log(CAT_BUTTON, LOG_DEBUG, "Double press detected (no triple action)");
            transitionToIdle(state);
            triggerButtonPress(note, channel0, MidiButtonConfig::PressType::DOUBLE_PRESS);
        } else {
            // Second tap within window - wait for potential triple tap
            logger.log(CAT_BUTTON, LOG_DEBUG, "Second tap detected, waiting for triple");
            state.secondTapTime = now;
            state.tapState = TapState::PendingDouble;
            state.tapStateExpireTime = now + effectiveTripleTap;
        }
    } else {
        // First tap or outside double tap window - delay decision
        logger.log(CAT_BUTTON, LOG_DEBUG, "First tap or outside window, scheduling short press");
        state.lastTapTime = now;
        state.tapState = TapState::PendingShort;
        state.tapStateExpireTime = now + effectiveDoubleTap;
        logger.log(CAT_BUTTON, LOG_DEBUG, "Scheduled short press: expire at %lu (now=%lu + window=%lu)",
                   state.tapStateExpireTime, now, effectiveDoubleTap);
    }
}

void MidiButtonProcessor::handleButtonRelease(uint8_t channel, uint8_t note, uint32_t pressDuration) {
    uint32_t now = millis();
    ButtonState& state = getButtonState(channel, note);
    state.lastReleaseTime = now;

    // findButtonConfig expects 0-based channel (channel param here is 1-based MIDI)
    const auto* cfg = MidiButtonConfig::Config::findButtonConfig(note, channel - 1);
    uint32_t effectiveLongPress = (cfg && cfg->longPressTime > 0)   ? cfg->longPressTime  : longPressTime;
    uint32_t effectiveDoubleTap = (cfg && cfg->doubleTapWindow > 0) ? cfg->doubleTapWindow : doubleTapWindow;
    uint32_t effectiveTripleTap = (cfg && cfg->tripleTapWindow > 0) ? cfg->tripleTapWindow : tripleTapWindow;
    const bool awaitTripleTap =
        cfg != nullptr && cfg->triplePressAction != MidiButtonConfig::ActionType::NONE;

    logger.log(CAT_BUTTON, LOG_DEBUG, "handleButtonRelease: Ch%d Note%d, duration=%lu, effectiveLongPress=%lu",
               channel, note, pressDuration, effectiveLongPress);

    if (pressDuration >= effectiveLongPress) {
        // Long press - trigger immediately and cancel pending presses
        logger.log(CAT_BUTTON, LOG_DEBUG, "Long press detected: duration=%lu >= effectiveLongPress=%lu",
                   pressDuration, effectiveLongPress);
        transitionToIdle(state);
        triggerButtonPress(note, channel - 1, MidiButtonConfig::PressType::LONG_PRESS);
    } else {
        // Short press - check for multiple taps
        logger.log(CAT_BUTTON, LOG_DEBUG, "Short press detected: duration=%lu < effectiveLongPress=%lu",
                   pressDuration, effectiveLongPress);
        onShortRelease(state, channel, note, now, effectiveDoubleTap, effectiveTripleTap,
                       awaitTripleTap);
    }
}

void MidiButtonProcessor::processPendingPresses() {
    uint32_t now = millis();

    for (size_t i = 0; i < buttonStates.size(); ++i) {
        ButtonState& state = buttonStates[i];

        // Channel from index is 0-based (MIDI ch 16 → 15); triggerButtonPress expects 0-based
        const uint8_t channel0 = i / 128;
        const uint8_t note = i % 128;

        if (state.tapState == TapState::Idle) continue;
        if (now < state.tapStateExpireTime) {
            // Debug: show pending states that haven't expired yet
            continue;
        }

        switch (state.tapState) {
            case TapState::PendingShort:
                logger.log(CAT_BUTTON, LOG_DEBUG, "Short press expired: Ch%d Note%d, now=%lu, expire=%lu",
                           channel0 + 1, note, now, state.tapStateExpireTime);
                transitionToIdle(state);
                triggerButtonPress(note, channel0, MidiButtonConfig::PressType::SHORT_PRESS);
                break;
            case TapState::PendingDouble:
                logger.log(CAT_BUTTON, LOG_DEBUG, "Double press expired: Ch%d Note%d, now=%lu, expire=%lu",
                           channel0 + 1, note, now, state.tapStateExpireTime);
                transitionToIdle(state);
                triggerButtonPress(note, channel0, MidiButtonConfig::PressType::DOUBLE_PRESS);
                break;
            case TapState::PendingTriple:
                logger.log(CAT_BUTTON, LOG_DEBUG, "Triple press expired: Ch%d Note%d, now=%lu, expire=%lu",
                           channel0 + 1, note, now, state.tapStateExpireTime);
                transitionToIdle(state);
                triggerButtonPress(note, channel0, MidiButtonConfig::PressType::TRIPLE_PRESS);
                break;
            default:
                break;
        }
    }
}

void MidiButtonProcessor::triggerButtonPress(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType) {
    SC_GESTURE(channel, note, static_cast<int>(pressType));
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