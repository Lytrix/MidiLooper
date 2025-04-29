#include "Globals.h"
#include <LiquidCrystal.h>
#include "ClockManager.h"
#include "DisplayManager.h"
#include <MIDI.h>

LiquidCrystal lcd(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

DisplayManager displayManager;  // create global instance

DisplayManager::DisplayManager()
  : selectedTrack(0) {}

void DisplayManager::setup() {
  lcd.begin(16, 2);
  lcd.clear();
}

void DisplayManager::setSelectedTrack(uint8_t trackIndex) {
  if (trackIndex < trackManager.getTrackCount()) {
    selectedTrack = trackIndex;
  }
}

void DisplayManager::update() {
  //lcd.clear();
  showTrackStates();
  showPianoRoll();
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
      symbol = 'M';  // muted
    }

    //lcd.print("T");
    lcd.print(i + 1);
    lcd.print(":");
    lcd.print(symbol);
    lcd.print(" ");
  }
}

void DisplayManager::showPianoRoll() {
  lcd.setCursor(0, 1);

  Track& track = trackManager.getTrack(selectedTrack);
  const auto& notes = track.getNoteEvents();

  uint32_t scrollOffset = 0;

  // Constants
  const uint32_t TICKS_PER_QUARTER = 24;  // adjust if needed
  const uint32_t BEATS_PER_BAR = 4;       // common time (4/4)
  const uint32_t TICKS_PER_BAR = TICKS_PER_QUARTER * BEATS_PER_BAR;  // 96 ticks per bar

  // Scroll by bar while recording or overdubbing
  if (track.isRecording() || track.isOverdubbing()) {
    uint32_t currentTick = clockManager.getCurrentTick();
    scrollOffset = (currentTick / TICKS_PER_BAR) * TICKS_PER_BAR;
  }

  for (uint8_t pos = 0; pos < 16; pos++) {
    uint32_t tick = scrollOffset + pos;
    bool foundNote = false;

    for (const auto& note : notes) {
      if (tick >= note.startTick && tick < note.endTick) {
        if (tick == note.startTick) {
          lcd.write(noteName(note.note));
        } else {
          lcd.write('#');
        }
        foundNote = true;
        break;
      }
    }

    if (!foundNote) {
      lcd.print('-');
    }
  }
}




// Converts MIDI note number to a letter (basic version)
char DisplayManager::noteName(uint8_t midiNote) {
  static const char names[] = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
  return names[(midiNote / 2) % 7];  // very simplified mapping
}
