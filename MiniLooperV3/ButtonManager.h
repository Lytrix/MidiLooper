#ifndef BUTTONMANAGER_H
#define BUTTONMANAGER_H

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

    // Returns true only when that press is the 2nd tap within window.
    bool isDoubleTap(uint8_t idx);

private:
    std::vector<Bounce> buttons;
     // Must be sized to #buttons
    std::vector<uint32_t> pressTimes;
    std::vector<uint32_t> lastTapTime;
    std::vector<bool> pendingShortPress;
    std::vector<uint32_t> shortPressExpireTime;

    static constexpr uint16_t DOUBLE_TAP_WINDOW = 250;  // ms
    static const uint16_t LONG_PRESS_TIME = 500; // ms

    void handleButton(uint8_t index, ButtonAction action);
};
extern ButtonManager buttonManager;

#endif // BUTTONMANAGER_H