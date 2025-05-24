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

class ButtonManager {
public:
    ButtonManager();

    void setup(const std::vector<uint8_t>& pins);
    void update();

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
    static const uint16_t LONG_PRESS_TIME = 800; // ms

    void handleButton(uint8_t index, ButtonAction action);
};
extern ButtonManager buttonManager;

#endif // BUTTONMANAGER_H