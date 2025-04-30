#include "Globals.h"
#include <MIDI.h>
#include <LiquidCrystal.h>
#include "ClockManager.h"
#include "DisplayManager.h"

LiquidCrystal lcd(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

DisplayManager displayManager;  // create global instance

DisplayManager::DisplayManager() {}

void DisplayManager::setup() {
  lcd.begin(16, 2);
  lcd.clear();
}

void displaySimpleNoteBar(const std::vector<NoteEvent>& notes, uint32_t currentTick, uint32_t loopLengthTicks, uint32_t startLoopTick, LiquidCrystal& lcd) {
    char line[17] = "                "; // 16 chars
    const uint8_t resolution = 16;

    uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

    for (const auto& ne : notes) {
        uint32_t start = (ne.startNoteTick % loopLengthTicks);
        uint32_t end = (ne.endNoteTick % loopLengthTicks);

        if (end < start) end += loopLengthTicks;

        uint8_t startPos = map(start, 0, loopLengthTicks, 0, resolution);
        uint8_t endPos = map(end, 0, loopLengthTicks, 0, resolution);

        startPos = constrain(startPos, 0, resolution - 1);
        endPos = constrain(endPos, startPos + 1, resolution);

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


void DisplayManager::update() {
  //lcd.clear();
  showTrackStates();

  const auto& track = trackManager.getSelectedTrack();
  const auto& notes = track.getNoteEvents();  // <-- Make sure this is calling your accessor

  displaySimpleNoteBar(notes, clockManager.getCurrentTick(), track.getLength(), track.getStartLoopTick(), lcd);
  //displayNoteBarAllInOneLine(track, lcd);  // <- Use simple bar display
  
  if(DEBUG_DISPLAY) {
    Serial.println("Calling displaySimpleNoteBar...");
    Serial.print("Note count: ");
    Serial.println(track.getNoteEvents().size());
  }
  // not yet working 100%:
  //updateNoteDisplay(clockManager.getCurrentTick(), track.getNoteEvents(), track.getLength());
}

void DisplayManager::showTrackStates() {
  lcd.setCursor(0, 0);

  for (uint8_t i = 0; i < trackManager.getTrackCount(); i++) {
    TrackState trackState = trackManager.getTrackState(i);

    char symbol = ' ';
    switch (trackState) {
      case TRACK_RECORDING:    symbol = 'R'; break;
      case TRACK_PLAYING:      symbol = 'P'; break;
      case TRACK_OVERDUBBING:  symbol = 'O'; break;
      case TRACK_STOPPED:      symbol = 'S'; break;
      default:                 symbol = '?'; break;
    }

    if (!trackManager.isTrackAudible(i)) {
      symbol = 'M';  // muted
    }

    //lcd.print("T");
    lcd.print(i + 1);
    lcd.print(":");
    lcd.print(symbol);
    lcd.print(" ");
  }
}

#define PIXELS_PER_CHAR 5
#define DISPLAY_CHARS 16
#define DISPLAY_WIDTH_PIXELS (DISPLAY_CHARS * PIXELS_PER_CHAR)
#define PAGE_PIXELS 40  // 8 chars = 1 bar

byte customChars[8][8];  // 8 characters, each 8 vertical pixels high

void clearCustomChars() {
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++)
      customChars[i][j] = 0;
}

void drawNotePageWithNoteEvents(const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks, uint32_t currentTick) {
  clearCustomChars();

  for (const NoteEvent& note : notes) {
    // Clamp and map note pitch
    int clampedNote = constrain(note.note, 28, 84);
    int y = map(clampedNote, 36, 84, 0, 7);

    // Map tick positions to display width (80 pixels)
    int xStart = (note.startNoteTick * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
    int xEnd = (note.endNoteTick * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;

    xStart = constrain(xStart, 0, DISPLAY_WIDTH_PIXELS - 1);
    xEnd = constrain(xEnd, 0, DISPLAY_WIDTH_PIXELS - 1);

    for (int x = xStart; x <= xEnd; x++) {
      int charIndex = x / PIXELS_PER_CHAR;
      int bitIndex = x % PIXELS_PER_CHAR;
      customChars[charIndex][y] |= (1 << (4 - bitIndex));
    }
  }

  // Bar marker at midpoint
  int barTick = loopLengthTicks / 2;
  int markerX = (barTick * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
  int markerChar = markerX / PIXELS_PER_CHAR;
  int markerBit = markerX % PIXELS_PER_CHAR;
  for (int y = 0; y < 8; y++) {
    customChars[markerChar][y] |= (1 << (4 - markerBit));
  }

  // Playhead position
  uint32_t tickInLoop = currentTick % loopLengthTicks;
  int playheadX = (tickInLoop * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
  int playChar = playheadX / PIXELS_PER_CHAR;
  int playBit = playheadX % PIXELS_PER_CHAR;
  customChars[playChar][7] |= (1 << (4 - playBit));  // playhead as bottom pixel

  // Write all 8 custom characters to LCD
  for (int i = 0; i < 8; i++) {
    lcd.createChar(i, customChars[i]);
  }

  lcd.setCursor(0, 1);  // bottom row
  for (int i = 0; i < 16; i++) {
    lcd.write(i % 8);  // reuse 8 characters across 16 cells
  }
}

void displayNoteBarAllInOneLine(const Track& track, LiquidCrystal& lcd) {
  char line[17];
  memset(line, ' ', 16);
  line[16] = '\0';

  const uint8_t resolution = 16;
  uint32_t loopLengthTicks = track.getLength();

  for (const auto& ne : track.getNoteEvents()) {
    uint8_t startPos = map(ne.startNoteTick % loopLengthTicks, 0, loopLengthTicks, 0, resolution);
    uint8_t endPos = map(ne.endNoteTick % loopLengthTicks, 0, loopLengthTicks, 0, resolution);

    if (endPos > resolution) endPos = resolution;
    if (startPos >= endPos) endPos = startPos + 1;

    for (uint8_t i = startPos; i < endPos && i < resolution; i++) {
      line[i] = 0xFF;
    }
  }

  lcd.setCursor(0, 1);  // Show on second row to avoid overlap with track states
  lcd.print(line);
}

void updateNoteDisplay(uint32_t currentTick, const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks) {
  drawNotePageWithNoteEvents(notes, loopLengthTicks, currentTick);
}
