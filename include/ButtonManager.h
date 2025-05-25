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