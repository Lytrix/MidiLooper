#include "Globals.h"
#include <MIDI.h>
#include <LiquidCrystal.h>
#include "ClockManager.h"
#include "TrackManager.h"
#include "DisplayManager.h"
#include "Logger.h"

LiquidCrystal lcd(LCD::RS, LCD::ENABLE, LCD::D4, LCD::D5, LCD::D6, LCD::D7);
DisplayManager displayManager;  // Global instance

DisplayManager::DisplayManager() {}

void DisplayManager::setup() {
  lcd.begin(16, 2);
  lcd.clear();
}

void DisplayManager::update() {
  // Top row
  showTrackStates();

  const auto& track = trackManager.getSelectedTrack();
  const auto& notes = track.getNoteEvents();

  // Bottom row
  drawNotePageWithNoteEvents(notes, track.getLength(), clockManager.getCurrentTick(), track.getStartLoopTick());
  if (debugLevel & DEBUG_DISPLAY) {
    logger.debug("Calling displaySimpleNoteBar...");
    logger.debug("Note count: %d", notes.size());
  }
}

void DisplayManager::showTrackStates() {
  lcd.setCursor(0, 0);

  for (uint8_t i = 0; i < trackManager.getTrackCount(); i++) {
    TrackState state = trackManager.getTrackState(i);
    char symbol = ' ';

    switch (state) {
      case TRACK_EMPTY: symbol = '-'; break;
      case TRACK_RECORDING: symbol = 'R'; break;
      case TRACK_PLAYING: symbol = 'P'; break;
      case TRACK_OVERDUBBING: symbol = 'O'; break;
      case TRACK_STOPPED: symbol = 'S'; break;
      default: symbol = '?'; break;
    }

    if (!trackManager.isTrackAudible(i)) {
      symbol = 'M';  // Muted
    }

    lcd.print(i + 1);
    lcd.print(":");
    lcd.print(symbol);
    lcd.print(" ");
  }
}

// --------------------
// Multi-line "pixel" note view using custom characters
// --------------------
#define PIXELS_PER_CHAR 5
#define DISPLAY_CHARS 16
#define DISPLAY_WIDTH_PIXELS (DISPLAY_CHARS * PIXELS_PER_CHAR)

static byte customChars[8][8];  // 8 custom characters (each 5x8 pixels)

void clearCustomChars() {
  memset(customChars, 0, sizeof(customChars));
}

void DisplayManager::drawNotePageWithNoteEvents(const std::vector<NoteEvent>& notes,
                                                uint32_t loopLengthTicks,
                                                uint32_t currentTick,
                                                uint32_t startLoopTick) {
  clearCustomChars();

  // === Bar/Beat Counter ===
  uint32_t ticksPerBeat = loopLengthTicks / 16;  // Assuming 4 bars, 4 beats per bar
 
  // --- 1) Compute current play‐head position in the loop ---
  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

  uint8_t beat = (tickInLoop / ticksPerBeat) % 4 + 1;   // 1-based beat
  uint8_t bar = (tickInLoop / (ticksPerBeat * 4)) + 1;  // 1-based bar

  char counterText[6];
  snprintf(counterText, sizeof(counterText), "%u:%u", bar, beat);

  // Place it in bottom right (last 3-5 characters of row 1)
  int counterCol = 16 - strlen(counterText);  // Adjust based on text width
  lcd.setCursor(counterCol, 1);
  lcd.print(counterText);

  clearCustomChars();

  if (notes.empty() || loopLengthTicks == 0) {
    // nothing to draw
    lcd.setCursor(0,1);
    lcd.print("                ");
    return;
  }

 
  // --- 2) Compute vertical scale ---
  int minNote = 127, maxNote = 0;
  for (auto &n : notes) {
    minNote = min(minNote, (int)n.note);
    maxNote = max(maxNote, (int)n.note);
  }
  if (minNote == maxNote) maxNote = minNote + 1;

  // --- 3) Draw each note (start→end) into the 8‐character window ---
  for (const auto &orig : notes) {
    // fold note into first loop
    uint32_t s0 = orig.startNoteTick % loopLengthTicks;
    uint32_t e0 = orig.endNoteTick   % loopLengthTicks;

    // map to Y row
    int clamped = constrain(orig.note, minNote, maxNote);
    int y = map(clamped, minNote, maxNote, /*top*/7, /*bot*/0);

    // helper to draw a span [a..b] in the window
    auto drawSpan = [&](uint32_t a, uint32_t b){
      for (uint32_t t = a; t <= b; ++t) {
        // bring t relative to play-head, wrapping
        uint32_t rel = (t + loopLengthTicks - tickInLoop) % loopLengthTicks;
        // shift into pixel space
        uint32_t xpix = (rel * (DISPLAY_WIDTH_PIXELS)) / loopLengthTicks;
        int ci = xpix / PIXELS_PER_CHAR;
        int bi = xpix % PIXELS_PER_CHAR;
        if (ci >= 0 && ci < 8) {
          customChars[ci][y] |= (1 << (4 - bi));
        }
      }
    };

    // if the note spans without wrap
    if (e0 >= s0) {
      drawSpan(s0, e0);
    } else {
      // wrapped note body: [s0..end] + [0..e0]
      drawSpan(s0, loopLengthTicks - 1);
      drawSpan(0, e0);
    }
  }

  // --- 4) Upload to LCD and write row 1 ---
  for (int i = 0; i < 8; ++i) {
    lcd.createChar(i, customChars[i]);
  }
  lcd.setCursor(0,1);
  for (int i = 0; i < 8; ++i) {
    lcd.write(i);
  }
}

