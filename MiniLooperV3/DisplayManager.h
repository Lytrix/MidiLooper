#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#pragma once

#include <Arduino.h>

class NoteEvent;

// --------------------
// DisplayManager handles all LCD UI
// --------------------
class DisplayManager {
public:
    DisplayManager();
    void setup();     // Call once in setup()
    void update();    // Call periodically to refresh display

private:
    void drawPianoRoll(const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks, uint32_t currentTick, uint32_t startLoopTick);
    void drawBarBeatCounter(uint32_t loopLengthTicks, uint32_t currentTick, uint32_t startLoopTick);
    void showTrackStates(uint8_t selectedTrackId);  // Top row: Track statuses

    bool blinkState = false;
    unsigned long lastBlinkTime = 0;
    static const unsigned long blinkInterval = 400;  // blink every 500ms
};

extern DisplayManager displayManager;

#endif  // DISPLAY_MANAGER_H
