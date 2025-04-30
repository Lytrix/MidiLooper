#include "Globals.h"
#include <MIDI.h>
#include <LiquidCrystal.h>
#include "ClockManager.h"
#include "DisplayManager.h"

LiquidCrystal lcd(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
DisplayManager displayManager;  // Global instance

DisplayManager::DisplayManager() {}

void DisplayManager::setup() {
  lcd.begin(16, 2);
  lcd.clear();
}

void DisplayManager::update() {
  showTrackStates();  // Top row

  const auto& track = trackManager.getSelectedTrack();
  const auto& notes = track.getNoteEvents();

  displaySimpleNoteBar(notes, clockManager.getCurrentTick(), track.getLength(), track.getStartLoopTick(), lcd);

  //drawNotePageWithNoteEvents(notes, track.getLength(), clockManager.getCurrentTick(),track.getStartLoopTick());
  if (DEBUG_DISPLAY) {
    Serial.println("Calling displaySimpleNoteBar...");
    Serial.print("Note count: ");
    Serial.println(notes.size());
  }
}

void DisplayManager::showTrackStates() {
  lcd.setCursor(0, 0);

  for (uint8_t i = 0; i < trackManager.getTrackCount(); i++) {
    TrackState state = trackManager.getTrackState(i);
    char symbol = ' ';

    switch (state) {
      case TRACK_RECORDING:    symbol = 'R'; break;
      case TRACK_PLAYING:      symbol = 'P'; break;
      case TRACK_OVERDUBBING:  symbol = 'O'; break;
      case TRACK_STOPPED:      symbol = 'S'; break;
      default:                 symbol = '?'; break;
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
// Simple 1D note bar with playhead
// --------------------
void displaySimpleNoteBar(const std::vector<NoteEvent>& notes, uint32_t currentTick, uint32_t loopLengthTicks, uint32_t startLoopTick, LiquidCrystal& lcd) {
  char line[17] = "                ";  // 16 chars
  const uint8_t resolution = 16;



  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

  for (const auto& ne : notes) {
    uint32_t start = ne.startNoteTick % loopLengthTicks;
    uint32_t end = ne.endNoteTick % loopLengthTicks;
    if (end < start) end += loopLengthTicks;

    uint8_t startPos = map(start, 0, loopLengthTicks, 0, resolution);
    uint8_t endPos   = map(end,   0, loopLengthTicks, 0, resolution);

    startPos = constrain(startPos, 0, resolution - 1);
    endPos   = constrain(endPos, startPos + 1, resolution);

    for (uint8_t i = startPos; i < endPos && i < resolution; ++i) {
      line[i] = 0xFF;
    }
  }

  uint8_t playheadPos = map(tickInLoop, 0, loopLengthTicks, 0, resolution);
  if (line[playheadPos] != 0xFF) {
    line[playheadPos] = '|';
  }

  lcd.setCursor(0, 1);
  lcd.print(line);
}

// --------------------
// Alternate full-line bar renderer
// --------------------
void displayNoteBarAllInOneLine(const Track& track, LiquidCrystal& lcd) {
  char line[17];
  memset(line, ' ', 16);
  line[16] = '\0';

  const uint8_t resolution = 16;
  uint32_t loopLengthTicks = track.getLength();

  for (const auto& ne : track.getNoteEvents()) {
    uint8_t start = map(ne.startNoteTick % loopLengthTicks, 0, loopLengthTicks, 0, resolution);
    uint8_t end   = map(ne.endNoteTick   % loopLengthTicks, 0, loopLengthTicks, 0, resolution);

    if (end > resolution) end = resolution;
    if (start >= end) end = start + 1;

    for (uint8_t i = start; i < end && i < resolution; i++) {
      line[i] = 0xFF;
    }
  }

  lcd.setCursor(0, 1);
  lcd.print(line);
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

void drawNotePageWithNoteEvents(const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks, uint32_t currentTick, uint32_t startLoopTick) {
  clearCustomChars();

  for (const NoteEvent& note : notes) {
    int clamped = constrain(note.note, 28, 84);
    int y = map(clamped, 36, 84, 0, 7);

    int xStart = ((note.startNoteTick % loopLengthTicks) * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
    int xEnd   = ((note.endNoteTick % loopLengthTicks)  * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
    // if (xEnd < xStart) xEnd += loopLengthTicks;

    xStart = constrain(xStart, 0, DISPLAY_WIDTH_PIXELS - 1);
    xEnd   = constrain(xEnd, xStart + 1, DISPLAY_WIDTH_PIXELS - 1);

    for (int x = xStart; x <= xEnd; x++) {
      int charIndex = x / PIXELS_PER_CHAR;
      int bitIndex  = x % PIXELS_PER_CHAR;
      customChars[charIndex][y] |= (1 << (4 - bitIndex));
    }
  }

  // Add bar marker (at half)
  int barTick = loopLengthTicks / 2;
  int markerX = (barTick * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
  int mChar = markerX / PIXELS_PER_CHAR;
  int mBit  = markerX % PIXELS_PER_CHAR;
  for (int y = 0; y < 8; y++) {
    customChars[mChar][y] |= (1 << (4 - mBit));
  }

  // Add playhead at bottom
  int playheadX = ((currentTick -startLoopTick)) * DISPLAY_WIDTH_PIXELS / loopLengthTicks;
  int pChar = playheadX / PIXELS_PER_CHAR;
  int pBit  = playheadX % PIXELS_PER_CHAR;
  customChars[pChar][7] |= (1 << (4 - pBit));  // Playhead uses bottom pixel

  for (int i = 0; i < 8; i++) {
    lcd.createChar(i, customChars[i]);
  }

  lcd.setCursor(0, 1);
  for (int i = 0; i < 10; i++) {
    lcd.write(i % 8);  // Display characters 0–7 repeatedly
  }
}

