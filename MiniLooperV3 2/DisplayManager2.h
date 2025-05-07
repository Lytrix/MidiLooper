#pragma once
#include <U8g2lib.h>
#include <vector>
#include "Track.h"   // where NoteEvent is declared
#include "TrackManager.h"   // for trackManager
#include "ClockManager.h"   // for clockManager

// Pins: SCLK, DATA, CS, DC, RST
static const uint8_t SCLK_PIN = 12;
static const uint8_t DATA_PIN = 11;
static const uint8_t CS_PIN   = 40;
static const uint8_t DC_PIN   = 41;
static const uint8_t RST_PIN  = 39;

#define U8G2_16BIT

class DisplayManager2 {
public:
  DisplayManager2();
  void setup();
  // Combined render method: draws piano roll and info using track and clock
  void update();

private:
  U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI _u8g2;

  uint8_t _baseNote = 36;      // MIDI note mapping to top row
  uint32_t _prevDrawTick = 0;        // last tick when display was updated

  static constexpr uint8_t SCREEN_WIDTH  = 256;
  static constexpr uint8_t SCREEN_HEIGHT =  64;
  static constexpr uint8_t  MAX_NOTES      = 32;
  static constexpr uint32_t DRAW_INTERVAL  = 8;   // minimum ticks between updates


  uint8_t noteToRow(uint8_t note) {
    int16_t r = note - _baseNote;
    if (r < 0)          return 0;
    if (r >= MAX_NOTES) return MAX_NOTES - 1;
    return uint8_t(r);
  }
}; extern DisplayManager2 displayManager2;