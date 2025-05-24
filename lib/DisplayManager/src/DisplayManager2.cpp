// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"
#include "SSD1322_Config.h"
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>
#include "TrackUndo.h"

DisplayManager2 displayManager2;

DisplayManager2::DisplayManager2() : _display() {
    // Don't initialize SPI here - it will be done in setup()
}

// Helper function to clear the display buffer
void DisplayManager2::clearDisplayBuffer() {
    Serial.println("DisplayManager2: Clearing display buffer");
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);
    Serial.println("DisplayManager2: Display buffer cleared");
    Serial.println("DisplayManager2: Displaying buffer");   
   _display.api.display();
    Serial.println("DisplayManager2: Display buffer displayed");
}

// Helper: Map TrackState to status letter
static char trackStateToLetter(TrackState state, bool muted) {
    if (muted) return 'M';
    switch (state) {
        case TRACK_EMPTY:       return '-';
        case TRACK_RECORDING:   return 'R';
        case TRACK_PLAYING:     return 'P';
        case TRACK_OVERDUBBING: return 'O';
        case TRACK_STOPPED:     return 'S';
        case TRACK_ARMED:       return 'A';
        case TRACK_STOPPED_RECORDING: return 'r';
        default:                return '?';
    }
}

// Helper: Convert ticks to Bars:Beats:16th:Ticks string, with option to limit ticks to 2 decimals
static void ticksToBarsBeats16thTicks2Dec(uint32_t ticks, char* out, size_t outSize, bool leadingZeros = false) {
    uint32_t bar = ticks / Config::TICKS_PER_BAR + 1;
    uint32_t ticksInBar = ticks % Config::TICKS_PER_BAR;
    uint32_t beat = ticksInBar / Config::TICKS_PER_QUARTER_NOTE + 1;
    uint32_t ticksInBeat = ticksInBar % Config::TICKS_PER_QUARTER_NOTE;
    uint32_t sixteenthTicks = Config::TICKS_PER_QUARTER_NOTE / 4;
    uint32_t sixteenth = ticksInBeat / sixteenthTicks + 1;
    uint32_t ticksIn16th = ticksInBeat % sixteenthTicks;
    // Limit ticks to 2 decimals (max 99)
    uint32_t ticks2dec = (ticksIn16th > 99) ? 99 : ticksIn16th;
    if (leadingZeros) {
        snprintf(out, outSize, "%02lu:%02lu:%02lu:%02lu", bar, beat, sixteenth, ticks2dec);
    } else {
        snprintf(out, outSize, "%lu:%lu:%lu:%lu", bar, beat, sixteenth, ticks2dec);
    }
}

void DisplayManager2::drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis) {
    // Update pulse phase for state of the selected track
    float dt = (now - _lastPulseUpdate) / 1000.0f;
    _pulsePhase += dt * PULSE_SPEED;
    if (_pulsePhase > 1.0f) _pulsePhase -= 1.0f;
    _lastPulseUpdate = now;
    
    // Font and layout
    _display.gfx.select_font(&Font5x7FixedMono);
    constexpr int x = 0; // left margin
    constexpr int char_height = 7; // Font5x7FixedMono is 7px high
    constexpr int trackCount = 8;
    constexpr int step = (DISPLAY_HEIGHT - char_height) / (trackCount - 1);


    for (uint8_t i = 0; i < trackCount; ++i) {
        char label[2] = {0};
        label[0] = trackStateToLetter(trackManager.getTrackState(i), !trackManager.isTrackAudible(i));
        int y = i * step + char_height;
        uint8_t brightness = 15;
        if (i == selectedTrack) {
            float phase = _pulsePhase;
            brightness = minPulse + (maxPulse - minPulse) * (0.5f + 0.5f * sinf(phase * 2 * 3.1415926f));
        } else {
            brightness = 8;
        }
        _display.gfx.draw_text(_display.api.getFrameBuffer(), label, x, y, brightness);
        // Draw track number next to state letter at 25% brightness
        char numStr[3];
        snprintf(numStr, sizeof(numStr), "%d", i + 1);
        uint8_t numBrightness = (i == selectedTrack) ? 15 : 4; // 100% if selected, else 25%
        _display.gfx.draw_text(_display.api.getFrameBuffer(), numStr, x + 10, y, numBrightness);
    }
}

void DisplayManager2::setup() {
    Serial.println("DisplayManager2: Setting up SSD1322 display...");

    // Initialize display
    _display.begin();

    // Set buffer size and clear display
    Serial.println("DisplayManager2: Setting buffer size");
    _display.gfx.set_buffer_size(BUFFER_WIDTH, BUFFER_HEIGHT);
    clearDisplayBuffer();

    // Now proceed with your drawing/demo code
    Serial.println("DisplayManager2: Drawing startup text...");
    Serial.println("Selecting font...");
    _display.gfx.select_font(&Font5x7Fixed);
    Serial.println("Font selected.");
    Serial.println("Drawing text...");
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "MidiLooper", 50, 20, 15);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "v0.2", 80, 40, 8);
    Serial.println("Text drawn.");
    _display.api.display();
    Serial.println("DisplayManager2: Text sent to display");
    delay(1000);
    clearDisplayBuffer();
}

void DisplayManager2::drawPianoRoll(uint32_t currentTick, Track& selectedTrack) {
    auto& track = selectedTrack;
    uint32_t startLoop = 0; // Always start at bar 1 visually
    uint32_t lengthLoop = track.getLength();
    const auto& notes = track.getNoteEvents();

    // Draw piano roll when loop valid

    setLastPlayedNote(nullptr); // Reset at the start
    uint32_t bestTick = 0;
    const NoteEvent* bestNote = nullptr;

    const int pianoRollY0 = 0;
    const int pianoRollY1 = 31;
    if (lengthLoop > 0) {
        // Compute position in loop based on currentTick and startLoop
        uint32_t loopPos = (currentTick >= startLoop)
            ? ((currentTick - startLoop) % lengthLoop)
            : 0;

        // Draw bar/beat/sixteenth grid lines
        const int barBrightness = 3;      // 50%
        const int beatBrightness = 2;     // 25%
        const int sixteenthBrightness = 1;// 10%
        const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
        const uint32_t ticksPerBeat = Config::TICKS_PER_QUARTER_NOTE;
        const uint32_t ticksPerSixteenth = Config::TICKS_PER_QUARTER_NOTE / 4;

        // Bar lines (solid, 10% brightness)
        for (uint32_t t = 0; t < lengthLoop; t += ticksPerBar) {
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
            _display.gfx.draw_vline(_display.api.getFrameBuffer(), x, pianoRollY0, pianoRollY1, barBrightness);
        }
        // Beat lines (dotted)
        bool showBeat = (lengthLoop <= 9 * ticksPerBar);
        if (showBeat) {
        for (uint32_t t = ticksPerBeat; t < lengthLoop; t += ticksPerBeat) {
            if (t % ticksPerBar == 0) continue; // skip bar lines
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
            for (int y = pianoRollY0; y <= pianoRollY1; y += 2) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, beatBrightness);
            }
        }
        }
        // Sixteenth lines (wider dotted)
        bool showSixteenth = (lengthLoop <= 5 * ticksPerBar);
        if (showSixteenth) {
        for (uint32_t t = ticksPerSixteenth; t < lengthLoop; t += ticksPerSixteenth) {
            if (t % ticksPerBar == 0 || t % ticksPerBeat == 0) continue; // skip bar/beat lines
            int x = TRACK_MARGIN + map(t, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
            for (int y = pianoRollY0; y <= pianoRollY1; y += 4) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), x, y, sixteenthBrightness);
            }
        }
        }

        // Draw each note as a horizontal bar
        for (const auto& e : notes) {
            // Snap loop start visually to 0
            uint32_t absStart = e.startNoteTick;
            uint32_t absEnd = e.endNoteTick;
            // Allow drawing notes before startLoopTick for correct modulo wrapping
            uint32_t s = (absStart >= startLoop) ? (absStart - startLoop) % lengthLoop : (lengthLoop - (startLoop - absStart) % lengthLoop) % lengthLoop;
            uint32_t eTick = (absEnd >= startLoop) ? (absEnd - startLoop) % lengthLoop : (lengthLoop - (startLoop - absEnd) % lengthLoop) % lengthLoop;
            
            // Compute min/max pitch for scaling
            int minPitch = 127;
            int maxPitch = 0;
            for (const auto& n : notes) {
                if (n.note < minPitch) minPitch = n.note;
                if (n.note > maxPitch) maxPitch = n.note;
            }
            int y = map(e.note, minPitch, maxPitch == minPitch ? minPitch + 1 : maxPitch, 31, 0);

            if (eTick < s) {
                int x0 = TRACK_MARGIN + map(s, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
                int x1 = BUFFER_WIDTH - 1;
                _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, 15);  // 15 is max brightness
                int x2 = TRACK_MARGIN;
                int x3 = TRACK_MARGIN + map(eTick, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
                _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x2, y, x3, y, 15);
            } else {
                int x0 = TRACK_MARGIN + map(s, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
                int x1 = TRACK_MARGIN + map(eTick, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
                if (x1 < x0) x1 = x0;
                _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, 15);
            }

            // Always update the most recent matching active note
            bool inNote = (eTick < s)
                ? (loopPos >= s || loopPos < eTick)
                : (loopPos >= s && loopPos < eTick);
            if (inNote) activeNote = &e;
           // Find the note with the largest start <= loopPos (wrapping)
           if (s <= loopPos && (bestNote == nullptr || s > bestTick)) {
              bestTick = s;
              bestNote = &e;
           }
        }
        if (bestNote) {
            setLastPlayedNote(bestNote);
        } else if (!notes.empty()) {
            setLastPlayedNote(&notes.back());
        }
        int cx = TRACK_MARGIN + map(loopPos, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, 0, 32, barBrightness);
    }
}

// Draw info area
void DisplayManager2::drawInfoArea(uint32_t currentTick, Track& selectedTrack) {
    // 1. Current position (playhead) as musical time, with leading zeros and 2 decimals for ticks
    char posStr[24];
    char loopLine[32];
    // Get length of loop
    uint32_t lengthLoop = selectedTrack.getLength();
    
    ticksToBarsBeats16thTicks2Dec(currentTick, posStr, sizeof(posStr), true); // true = leading zeros
    if (lengthLoop > 0 && Config::TICKS_PER_BAR > 0) {
        uint32_t bars = lengthLoop / Config::TICKS_PER_BAR;
        snprintf(loopLine, sizeof(loopLine), "LOOP:%lu", bars);
    } else {
        snprintf(loopLine, sizeof(loopLine), "LOOP:-");
    }
    char posAndLoop[64];
    snprintf(posAndLoop, sizeof(posAndLoop), "%s%s%s", posStr, " ", loopLine); // 1 space for alignment
    // Lower brightness by 1/8 every 100 bars, min 1/8
    uint32_t bar = currentTick / Config::TICKS_PER_BAR + 1;
    uint8_t brightnessStep = maxBrightness / 8;
    uint8_t brightness = maxBrightness - ((bar / 100) * brightnessStep);
    if (brightness < brightnessStep) brightness = brightnessStep;
    // Draw posAndLoop with per-character font and brightness
    int x = DisplayManager2::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT - 12;
    const char* p = posAndLoop;
    while (*p) {
        char c[2] = {*p, 0};
        uint8_t charBrightness;
        if (*p == ':') {
            charBrightness = brightness / 2;
        } else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            charBrightness = brightness / 4;
        } else {
            charBrightness = brightness;
        }
        // Select font: digits, '-', and ':' use mono, rest use normal
        if (((*p >= '0' && *p <= '9') || *p == '-' || *p == ':')) {
            _display.gfx.select_font(&Font5x7FixedMono);
        } else {
            _display.gfx.select_font(&Font5x7Fixed);
        }
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, x, y, charBrightness);
        x += 6;
        ++p;
    }
    // Draw undo count right-aligned, max 99, styled per character and font
    uint8_t undoCount = TrackUndo::getUndoCount(selectedTrack);
    char undoStr[6];
    if (undoCount == 0) {
        snprintf(undoStr, sizeof(undoStr), "U:--");
    } else {
        if (undoCount > 99) undoCount = 99;
        snprintf(undoStr, sizeof(undoStr), "U:%02u", undoCount);
    }
    int undoLen = 0;
    for (const char* up = undoStr; *up; ++up) ++undoLen;
    int undoX = DISPLAY_WIDTH - (undoLen * 6) - 2;
    const char* up = undoStr;
    while (*up) {
        char c[2] = {*up, 0};
        uint8_t charBrightness;
        if (*up == ':') {
            charBrightness = brightness / 2;
        } else if ((*up >= 'A' && *up <= 'Z') || (*up >= 'a' && *up <= 'z')) {
            charBrightness = brightness / 4;
        } else {
            charBrightness = brightness;
        }
        // Select font: digits, '-', and ':' use mono, rest use normal
        if (((*up >= '0' && *up <= '9') || *up == '-' || *up == ':')) {
            _display.gfx.select_font(&Font5x7FixedMono);
        } else {
            _display.gfx.select_font(&Font5x7Fixed);
        }
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, undoX, y, charBrightness);
        undoX += 6;
        ++up;
    }
}

void DisplayManager2::drawNoteInfo(uint32_t currentTick, Track& selectedTrack) {
    // 2. Note info line: SS:SS:SSS   NOTE: --- LEN: --- VEL: ---
    char noteLine[96];
    bool validNote = false;
    uint8_t noteVal = 0, velVal = 0;
    uint32_t lenVal = 0;
    char startStr[24] = {0};
    const auto& notes = selectedTrack.getNoteEvents();
    uint32_t lengthLoop = selectedTrack.getLength();
    const NoteEvent* lastPlayedNote = getLastPlayedNote();

    if (lastPlayedNote) {
        ticksToBarsBeats16thTicks2Dec(lastPlayedNote->startNoteTick % lengthLoop, startStr, sizeof(startStr), true);
        noteVal = lastPlayedNote->note;
        lenVal = lastPlayedNote->endNoteTick - lastPlayedNote->startNoteTick;
        velVal = lastPlayedNote->velocity;
        validNote = (noteVal <= 127 && velVal <= 127 && lenVal < 10000);
    } else if (!notes.empty()) {
        const auto& last = notes.back();
        ticksToBarsBeats16thTicks2Dec(last.startNoteTick % lengthLoop, startStr, sizeof(startStr), true);
        noteVal = last.note;
        lenVal = last.endNoteTick - last.startNoteTick;
        velVal = last.velocity;
        validNote = (noteVal <= 127 && velVal <= 127 && lenVal < 10000);
    }

    if (validNote) {
        snprintf(noteLine, sizeof(noteLine), "%s%sNOTE:%3u LEN:%3lu VEL:%3u", startStr, " ", noteVal, lenVal, velVal);
    } else {
        snprintf(noteLine, sizeof(noteLine), "--:--:--:--%sNOTE:--- LEN:--- VEL:---", " ");
    }
    // Draw noteLine with all ':' at 50% brightness, digits at full, letters at 75%
    int x = DisplayManager2::TRACK_MARGIN;
    int y = DISPLAY_HEIGHT;
    const char* p = noteLine;
    while (*p) {
        char c[2] = {*p, 0};
        uint8_t charBrightness;
        if (*p == ':') {
            charBrightness = maxBrightness / 2;
        } else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p == '-')) {
            charBrightness = maxBrightness / 4;
        } else {
            charBrightness = maxBrightness;
        }
        _display.gfx.draw_text(_display.api.getFrameBuffer(), c, x, y, charBrightness);
        x += 6;
        ++p;
    }
} 

void DisplayManager2::update() {
    // Get current global tick count for display timing
    uint32_t currentTick = clockManager.getCurrentTick();
    uint32_t now = millis();

    // Clear frame buffer
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

    // Draw vertical track status on the left
    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);
    
    // Draw piano roll
    drawPianoRoll(currentTick, trackManager.getSelectedTrack());

    // Draw info area
    drawInfoArea(currentTick, trackManager.getSelectedTrack());

    // Draw note info
    drawNoteInfo(currentTick, trackManager.getSelectedTrack());

    // Send buffer to display
   _display.api.display();
}
