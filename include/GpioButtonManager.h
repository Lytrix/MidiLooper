//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef GPIOBUTTONMANAGER_H
#define GPIOBUTTONMANAGER_H

#include <Arduino.h>
#include <Bounce2.h>
#include <vector>

enum ButtonAction {
    BUTTON_NONE,
    BUTTON_SHORT_PRESS,
    BUTTON_DOUBLE_PRESS,
    BUTTON_TRIPLE_PRESS,
    BUTTON_LONG_PRESS
};

enum ButtonId {
    BUTTON_A = 0,
    BUTTON_B = 1,
    BUTTON_C = 2,
    BUTTON_D = 3,
    BUTTON_ENCODER = 4
};

/**
 * @class GpioButtonManager
 * @brief Teensy GPIO buttons (36–39) and encoder button (31); routes to MidiButtonActions.
 */
class GpioButtonManager {
public:
    GpioButtonManager();

    void setup(const std::vector<uint8_t>& pins);
    void update();
    void handleButton(ButtonId button, ButtonAction action);

    static const uint16_t DEFAULT_DEBOUNCE_INTERVAL = 10;

private:
    std::vector<Bounce> buttons;
    std::vector<uint32_t> lastTapTime;
    std::vector<uint32_t> pressTimes;
    std::vector<bool> pendingShortPress;
    std::vector<uint32_t> shortPressExpireTime;
    std::vector<uint32_t> secondTapTime;
    std::vector<bool> pendingDoublePress;
    std::vector<uint32_t> doublePressExpireTime;

    static constexpr uint16_t DOUBLE_TAP_WINDOW = 300;
    static const uint16_t LONG_PRESS_TIME = 600;

    int encoderPosition = 0;
};

extern GpioButtonManager gpioButtonManager;

#endif  // GPIOBUTTONMANAGER_H
