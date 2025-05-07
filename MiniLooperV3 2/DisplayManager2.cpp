// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"

DisplayManager2 displayManager2;

#define U8g2_16BIT

// ctor: initialize software-SPI driver
DisplayManager2::DisplayManager2()
 : _u8g2(U8G2_R0, SCLK_PIN, DATA_PIN, CS_PIN, DC_PIN, RST_PIN)
{}

void DisplayManager2::setup() {
  _u8g2.begin();
  _u8g2.clearBuffer();
  _u8g2.sendBuffer();
}

void DisplayManager2::update() {
  const auto& track = trackManager.getSelectedTrack();
  const auto& notes = track.getNoteEvents();
  uint32_t currentTick   = clockManager.getCurrentTick();
  uint32_t startLoopTick = track.getStartLoopTick();

  // Throttle updates to reduce screen overhead
  if (currentTick - _prevDrawTick < DRAW_INTERVAL) {
    return;
  }
  _prevDrawTick = currentTick;

  // Do not display notes while recording
  if (track.isRecording()) {
    _u8g2.clearBuffer();
    _u8g2.sendBuffer();
    return;
  }

  _u8g2.clearBuffer();
  const NoteEvent* activeNote = nullptr;
  
  // Single loop: draw bars and capture active note
  for (const auto& e : notes) {
    uint32_t noteStart = e.startNoteTick;
    uint32_t noteEnd   = e.endNoteTick; //? e.endNoteTick : currentTick;

    // skip events before loop begin or fully off-screen
    if (noteEnd < startLoopTick) continue;

    // uint32_t visibleStart = (noteStart < startLoopTick) ? startLoopTick : noteStart;
    // uint32_t ageStart     = currentTick - visibleStart;
    // uint32_t ageEnd       = currentTick - noteEnd;
    // if (ageStart > SCREEN_WIDTH) continue;

    uint16_t x1 = SCREEN_WIDTH;// - uint16_t(ageStart % SCREEN_WIDTH);
    uint16_t x2 = SCREEN_WIDTH ;//- uint16_t(ageEnd   % SCREEN_WIDTH);
    uint8_t  row = (e.note < _baseNote) ? 0 : 
                    ((e.note >= _baseNote + MAX_NOTES) ? MAX_NOTES - 1 : e.note - _baseNote);

    _u8g2.drawBox(x2, row * 2, x1 - x2, 2);

    // capture first active note-on for info (start ≤ now < end)
    if (!activeNote && e.startNoteTick <= currentTick && currentTick < noteEnd) {
      activeNote = &e;
    }
  }

  // Draw info below roll
  static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  char buf[32];
  _u8g2.setFont(u8g2_font_6x13_tr);

  // Note & velocity
  if (activeNote) {
    uint8_t n   = activeNote->note % 12;
    uint8_t oct = activeNote->note / 12 - 1;
    snprintf(buf, sizeof(buf), "Note:%s%u Vel:%3u", names[n], oct, activeNote->velocity);
  } else {
    snprintf(buf, sizeof(buf), "Note:--- Vel:---");
  }
  _u8g2.drawStr(0, SCREEN_HEIGHT - 16, buf);

  // Tick & loop start
  snprintf(buf, sizeof(buf), "Tick:%lu Loop:%lu", currentTick, startLoopTick);
  _u8g2.drawStr(0, SCREEN_HEIGHT - 4, buf);

  _u8g2.sendBuffer();
}
