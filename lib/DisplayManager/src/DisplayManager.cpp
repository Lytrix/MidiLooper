#include "DisplayManager.h"
#include "Globals.h"
#include <MIDI.h>
#include <LiquidCrystal.h>
#include "ClockManager.h"
#include "TrackManager.h"
#include "Logger.h"

LiquidCrystal lcd(LCD::RS, LCD::ENABLE, LCD::D4, LCD::D5, LCD::D6, LCD::D7);
DisplayManager displayManager;  // Global instance

DisplayManager::DisplayManager() {}

void DisplayManager::setup() {
  lcd.begin(16, 2);
  lcd.clear();
}

void DisplayManager::update() {
  // --- Used variables in draw methods ---
  const auto& track = trackManager.getSelectedTrack();
  const auto& notes = track.getNoteEvents();
  uint32_t currentTick = clockManager.getCurrentTick();
  uint32_t startLoopTick = track.getStartLoopTick();
  uint8_t getUndoCount = track.getUndoCount();

  uint32_t loopLengthTicks;
  // grow loop length while recording to be able to display in piano roll
  if (track.isRecording() && track.getLength() == 0) {
      // simulate provisional loop length for display
      loopLengthTicks = clockManager.getCurrentTick() - track.getStartLoopTick();
      if (loopLengthTicks == 0) loopLengthTicks = 1;  // prevent divide-by-zero
  } else {
      loopLengthTicks = track.getLength();
  }

  // Bottom row
  drawPianoRoll(notes, loopLengthTicks, currentTick, startLoopTick);
  drawUndoCounter(getUndoCount);
  drawBarBeatCounter(loopLengthTicks, currentTick, startLoopTick);

  if (debugLevel & DEBUG_DISPLAY) {
    logger.debug("Calling drawPianoRoll...");
    logger.debug("Note count: %d", notes.size());
  }
   // Top row
  drawTrackStates(trackManager.getSelectedTrackIndex());
}

// DisplayManager.cpp — Draw track states with blinking symbol on selected track number
void DisplayManager::drawTrackStates(uint8_t selectedTrack) {
  // --- 1) Update blink state timer ---
  unsigned long now = millis();
  if (now - lastBlinkTime >= blinkInterval) {
    blinkState = !blinkState;
    lastBlinkTime = now;
  }

  // --- 2) Draw row 0, blinking only the symbol of selected track ---
  lcd.setCursor(0, 0);
  uint8_t trackCount = trackManager.getTrackCount();

  for (uint8_t i = 0; i < trackCount; ++i) {
    // Print track number and colon
    lcd.print(i + 1);
    lcd.print(':');

    // Determine symbol (but blink it out when appropriate)
    char symbol;
    if (i == selectedTrack && !blinkState) {
      // During blink-off phase, print space instead of symbol
      symbol = ' ';
    } else {
      TrackState state = trackManager.getTrackState(i);
      switch (state) {
        case TRACK_EMPTY:       symbol = '-'; break;
        case TRACK_RECORDING:   symbol = 'R'; break;
        case TRACK_PLAYING:     symbol = 'P'; break;
        case TRACK_OVERDUBBING: symbol = 'O'; break;
        case TRACK_STOPPED:     symbol = 'S'; break;
        default:                symbol = '?'; break;
      }
      if (!trackManager.isTrackAudible(i)) {
        symbol = 'M';  // Muted overrides
      }
    }

    // Print symbol and trailing space
    lcd.print(symbol);
    lcd.print(' ');
  }
}


// DisplayManager.cpp — Multi-line "pixel" note helpers to use custom characters ---

#define PIXELS_PER_CHAR 5
#define DISPLAY_CHARS 16
#define DISPLAY_WIDTH_PIXELS (DISPLAY_CHARS * PIXELS_PER_CHAR)

static byte customChars[8][8];  // 8 custom characters (each 5x8 pixels)

void clearCustomChars() {
  memset(customChars, 0, sizeof(customChars));
}

void DisplayManager::drawUndoCounter(uint8_t getUndoCount) {
  lcd.setCursor(9, 1);
  lcd.print("U:");
  lcd.print(getUndoCount);
}

void DisplayManager::flashBarCounterHighlight() {
  highlightBarCounter = true;
  highlightUntil = millis() + 150;  // Highlight for 150ms
}

// DisplayManager.cpp — Bar/Beat Counter "4:1"
void DisplayManager::drawBarBeatCounter(uint32_t loopLengthTicks,
                                        uint32_t currentTick,
                                        uint32_t startLoopTick) {
    uint32_t elapsedTicks = currentTick - startLoopTick;
    const auto& track = trackManager.getSelectedTrack();

    // Use PPQN when in record state, because actual loop length is still being defined
    uint32_t displayTicksPerBar = track.isRecording() ? MidiConfig::PPQN * 4 : loopLengthTicks;
    uint32_t displayTicksPerBeat = displayTicksPerBar / 4;

    uint8_t beat = (elapsedTicks / displayTicksPerBeat) % 4 + 1;
    uint8_t bar  = (elapsedTicks / displayTicksPerBar) + 1;

    char buf[6];
    snprintf(buf, sizeof(buf), "%u:%u", bar, beat);
    int col = 16 - strlen(buf);

    lcd.setCursor(col, 1);

    if (highlightBarCounter && millis() < highlightUntil) {
        lcd.print("*");
        lcd.print(buf + 1);
    } else {
        lcd.print(buf);
        highlightBarCounter = false;
    }
}


// DisplayManager.cpp — Draws the 8-character piano roll on row 1 using custom chars
void DisplayManager::drawPianoRoll(const std::vector<NoteEvent>& notes,
                                   uint32_t loopLengthTicks,
                                   uint32_t currentTick,
                                   uint32_t startLoopTick) {
    // clear custom char buffer
    clearCustomChars();

    if (notes.empty() || loopLengthTicks == 0) {
        // clear row
        lcd.setCursor(0, 1);
        lcd.print("        ");
        return;
    }

    // find min/max note for scaling
    int minNote = 127, maxNote = 0;
    for (auto &n : notes) {
        minNote = std::min(minNote, (int)n.note);
        maxNote = std::max(maxNote, (int)n.note);
    }
    if (minNote == maxNote) maxNote = minNote + 1;

    // play-head within loop
    uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

    // draw spans
    auto drawSpan = [&](uint32_t a, uint32_t b, int row) {
        for (uint32_t t = a; t <= b; ++t) {
            uint32_t rel = (t + loopLengthTicks - tickInLoop) % loopLengthTicks;
            uint32_t xpix = (rel * DISPLAY_WIDTH_PIXELS) / loopLengthTicks;
            int ci = xpix / PIXELS_PER_CHAR;
            int bi = xpix % PIXELS_PER_CHAR;
            if (ci >= 0 && ci < 8) {
                customChars[ci][row] |= (1 << (4 - bi));
            }
        }
    };

    // layout each note
    for (auto &evt : notes) {
        uint32_t s0 = evt.startNoteTick % loopLengthTicks;
        uint32_t e0 = evt.endNoteTick   % loopLengthTicks;
        int clamped = constrain(evt.note, minNote, maxNote);
        int row = map(clamped, minNote, maxNote, 7, 0);

        if (e0 >= s0) {
            drawSpan(s0, e0, row);
        } else {
            drawSpan(s0, loopLengthTicks - 1, row);
            drawSpan(0, e0, row);
        }
    }

    // upload custom chars and render
    for (int i = 0; i < 8; ++i) {
        lcd.createChar(i, customChars[i]);
    }
    lcd.setCursor(0, 1);
    for (int i = 0; i < 8; ++i) {
        lcd.write(i);
    }
}

