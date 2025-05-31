//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef BUTTONMANAGER_H
#define BUTTONMANAGER_H

#include <Arduino.h>
#include <Bounce2.h>
#include <vector>

enum ButtonAction {
    BUTTON_NONE,
    BUTTON_SHORT_PRESS,
    BUTTON_DOUBLE_PRESS,
    BUTTON_LONG_PRESS
};

enum ButtonId {
    BUTTON_A = 0,
    BUTTON_B = 1,
    BUTTON_ENCODER = 2
};

/**
 * @class ButtonManager
 * @brief Manages hardware button/debounce logic and detects press actions and encoder turns.
 *
 * Uses Bounce2 to debounce a configurable set of input pins. The update() method must
 * be called regularly (e.g., in loop()) to poll button states. It classifies button events
 * into ButtonAction types: none, short press, double press, or long press, and routes them
 * to handleButton() for application-specific handling. The encoder push is treated like a
 * button (ButtonId::BUTTON_ENCODER). Encoder rotation is tracked via internal position counters.
 *
 * Configuration:
 *   - setup(pins): initialize pins for debouncing.
 *   - DEFAULT_DEBOUNCE_INTERVAL: debounce time in ms.
 *
 * Timing constants:
 *   - DOUBLE_TAP_WINDOW: max interval for double-press detection (ms).
 *   - LONG_PRESS_TIME: threshold for long-press detection (ms).
 */
class ButtonManager {
public:
    ButtonManager();

    void setup(const std::vector<uint8_t>& pins);
    void update();
    void handleButton(ButtonId button, ButtonAction action);

    // Debounce interval in milliseconds accessable for all functions
    static const uint16_t DEFAULT_DEBOUNCE_INTERVAL = 10;

private:
    std::vector<Bounce> buttons;
    // Helpers for long and double tap logic
    std::vector<uint32_t> lastTapTime;
    // Helpers for double tap logic
    std::vector<uint32_t> pressTimes;
    std::vector<bool> pendingShortPress;
    std::vector<uint32_t> shortPressExpireTime;

    static constexpr uint16_t DOUBLE_TAP_WINDOW = 300;  // ms
    static const uint16_t LONG_PRESS_TIME = 600; // ms

    int encoderPosition = 0;
    int lastEncoderPosition = 0;
};
extern ButtonManager buttonManager;

#endif // BUTTONMANAGER_H