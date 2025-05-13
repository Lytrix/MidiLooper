#pragma once
#include <U8g2lib.h>
#include <vector>
#include "Track.h"   // where NoteEvent is declared
#include "TrackManager.h"   // for trackManager
#include "ClockManager.h"   // for clockManager

#define U8G2_16BIT

class DisplayManager2 {
public:
    DisplayManager2();
    void setup();
    void update();

private:
    // Control pins for SSD1322
    static constexpr uint8_t CS_PIN   = 40;
    static constexpr uint8_t DC_PIN   = 41;
    static constexpr uint8_t RST_PIN  = 39;

    // Minimum ticks between redraws to throttle screen updates
    static constexpr uint32_t DRAW_INTERVAL = 8;  // adjust as needed

    // U8g2 hardware SPI constructor (cs, dc, reset)
    U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI _u8g2;

    uint32_t _prevDrawTick = 0;
}; extern DisplayManager2 displayManager2;