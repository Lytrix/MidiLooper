#ifndef BUTTONMANAGER_H
#define BUTTONMANAGER_H

#include <Bounce2.h>
#include <vector>

enum ButtonAction {
    BUTTON_NONE,
    BUTTON_SHORT_PRESS,
    BUTTON_LONG_PRESS
};

class ButtonManager {
public:
    ButtonManager();

    void setup(const std::vector<uint8_t>& pins);
    void update();

private:
    std::vector<Bounce> buttons;
    std::vector<uint32_t> pressTimes;

    static const uint16_t LONG_PRESS_TIME = 500; // ms

    void handleButton(uint8_t index, ButtonAction action);
};
extern ButtonManager buttonManager;

#endif // BUTTONMANAGER_H