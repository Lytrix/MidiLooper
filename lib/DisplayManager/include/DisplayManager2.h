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
    void clearDisplayBuffer();

private:
    static constexpr uint32_t DRAW_INTERVAL = 1000 / 30;  // 30 FPS
    static constexpr uint16_t BUFFER_WIDTH = 256;
    static constexpr uint16_t BUFFER_HEIGHT = 64;
    uint32_t _prevDrawTick = 0;
    SSD1322 _display;
    // Blinker/pulse state for selected track
    float _pulsePhase = 0.0f; // 0..1
    unsigned long _lastPulseUpdate = 0;
    static constexpr float PULSE_SPEED = 1.1f; // Pulses per second (slowed by 40%)
    // Track status rendering
    void drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis);
    //uint8_t _frameBuffer[BUFFER_WIDTH * BUFFER_HEIGHT / 2];  // 4-bit per pixel
}; extern DisplayManager2 displayManager2;