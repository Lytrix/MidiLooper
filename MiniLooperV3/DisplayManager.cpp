#include "Globals.h"
#include <MIDI.h>
#include <LiquidCrystal.h>
#include "ClockManager.h"
#include "TrackManager.h"
#include "DisplayManager.h"


LiquidCrystal lcd(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
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

