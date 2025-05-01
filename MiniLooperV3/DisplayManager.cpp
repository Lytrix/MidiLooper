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
  // Update scroll offset based on currentTick

  //displaySimpleNoteBar(notes, clockManager.getCurrentTick(), track.getLength(), track.getStartLoopTick(), lcd);
  uint32_t fixedCursorPosition = 4;  // Fixed cursor at the middle of the display
  drawNotePageWithNoteEvents(notes,  track.getLength(), clockManager.getCurrentTick(), track.getStartLoopTick());
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



void drawNotePageWithNoteEvents(const std::vector<NoteEvent>& notes,
                                uint32_t loopLengthTicks,
                                uint32_t currentTick,
                                uint32_t startLoopTick) {
  clearCustomChars();


// === Bar/Beat Counter ===
uint32_t ticksPerBeat = loopLengthTicks / 16;  // Assuming 4 bars, 4 beats per bar
uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

uint32_t beat = (tickInLoop / ticksPerBeat) % 4 + 1;  // 1-based beat
uint32_t bar  = (tickInLoop / (ticksPerBeat * 4)) + 1; // 1-based bar

char counterText[6];
snprintf(counterText, sizeof(counterText), "%u:%u", bar, beat);

// Place it in bottom right (last 3-5 characters of row 1)
int counterCol = 16 - strlen(counterText);  // Adjust based on text width
lcd.setCursor(counterCol, 1);
lcd.print(counterText);

  // Show original and duplicate (next-loop) notes
  for (const NoteEvent& originalNote : notes) {
    for (int loopOffset = 0; loopOffset <= 1; ++loopOffset) {
      NoteEvent note = originalNote;
      note.startNoteTick += loopOffset * loopLengthTicks;
      note.endNoteTick   += loopOffset * loopLengthTicks;

    // Step 1: Find min and max played notes
    int minNote = 127;
    int maxNote = 0;
    for (const NoteEvent& note : notes) {
      if (note.note < minNote) minNote = note.note;
      if (note.note > maxNote) maxNote = note.note;
    }

    // Optional: Clamp to a sensible MIDI range
    minNote = constrain(minNote, 0, 127);
    maxNote = constrain(maxNote, 0, 127);

    // Ensure a range of at least 1 to avoid divide-by-zero
    if (minNote == maxNote) maxNote = minNote + 1;

    // ... inside the drawing loop:
    int clamped = constrain(note.note, minNote, maxNote);
    int y = map(clamped, minNote, maxNote, 7, 0);  // Top = high notes


      int32_t noteStart = note.startNoteTick;
      int32_t noteEnd   = note.endNoteTick;

      int32_t length = noteEnd - noteStart;

      for (int32_t i = 0; i <= length; i++) {
        int32_t t = noteStart + i;
        int32_t relativeTick = t - (startLoopTick + tickInLoop);

        if (relativeTick < 0 || relativeTick >= (int32_t)loopLengthTicks)
          continue;

        int x = (relativeTick * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
        if (x < 0 || x >= DISPLAY_WIDTH_PIXELS) continue;

        int charIndex = x / PIXELS_PER_CHAR;
        int bitIndex  = x % PIXELS_PER_CHAR;

        if (charIndex >= 0 && charIndex < 8)
          customChars[charIndex][y] |= (1 << (4 - bitIndex));
      }
    }
  }

  // Upload to LCD
  for (int i = 0; i < 8; i++) {
    lcd.createChar(i, customChars[i]);
  }

  lcd.setCursor(0, 1);
  for (int i = 0; i < 8; i++) {
    lcd.write(i);
  }
}

