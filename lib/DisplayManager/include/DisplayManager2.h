#pragma once

#include <Arduino.h>
#include "SSD1322.h"
#include "Track.h"   // where NoteEvent is declared
#include "TrackManager.h"   // for trackManager
#include "ClockManager.h"   // for clockManager
#include "Globals.h"


class DisplayManager2 {
public:
    DisplayManager2();
    void setup();
    void update();

private:
    static constexpr uint32_t DRAW_INTERVAL = 1000 / 30;  // 30 FPS
    static constexpr uint16_t BUFFER_WIDTH = 128;
    static constexpr uint16_t BUFFER_HEIGHT = 64;
    uint32_t _prevDrawTick = 0;
    SSD1322 _display;
    uint8_t _frameBuffer[BUFFER_WIDTH * BUFFER_HEIGHT / 2];  // 4-bit per pixel
}; extern DisplayManager2 displayManager2;