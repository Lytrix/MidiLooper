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
    void flashBarCounterHighlight(); // highlight an asterix if a noteon/Off pair is successfully recorded

private:
    // --- Building blocks ---
    void drawPianoRoll(const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks, uint32_t currentTick, uint32_t startLoopTick);
    void drawBarBeatCounter(uint32_t loopLengthTicks, uint32_t currentTick, uint32_t startLoopTick);
    void drawTrackStates(uint8_t selectedTrackId);  // Top row: Track statuses
    void drawUndoCounter(uint8_t getUndoCount);

    // --- Blinker variables ---
    bool highlightBarCounter = false;
    uint32_t highlightUntil = 0;
    bool blinkState = false;
    unsigned long lastBlinkTime = 0;
    static const unsigned long blinkInterval = 400;  // blink every 500ms
};

extern DisplayManager displayManager;

#endif  // DISPLAY_MANAGER_H
