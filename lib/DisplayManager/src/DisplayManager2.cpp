// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"

#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>
    
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

// Helper: Convert ticks to Bars:Beats:16th:Ticks string
static void ticksToBarsBeats16thTicks(uint32_t ticks, char* out, size_t outSize, bool leadingZeros = false) {
    uint32_t bar = ticks / Config::TICKS_PER_BAR + 1;
    uint32_t ticksInBar = ticks % Config::TICKS_PER_BAR;
    uint32_t beat = ticksInBar / Config::TICKS_PER_QUARTER_NOTE + 1;
    uint32_t ticksInBeat = ticksInBar % Config::TICKS_PER_QUARTER_NOTE;
    uint32_t sixteenthTicks = Config::TICKS_PER_QUARTER_NOTE / 4;
    uint32_t sixteenth = ticksInBeat / sixteenthTicks + 1;
    uint32_t ticksIn16th = ticksInBeat % sixteenthTicks;
    if (leadingZeros) {
        snprintf(out, outSize, "%02lu:%02lu:%02lu:%02lu", bar, beat, sixteenth, ticksIn16th);
    } else {
        snprintf(out, outSize, "%lu:%lu:%lu:%lu", bar, beat, sixteenth, ticksIn16th);
    }
}

void DisplayManager2::drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis) {
    // Font and layout
    _display.gfx.select_font(&Font5x7FixedMono);
    constexpr int x = 2; // left margin
    constexpr int y0 = 14; // top margin
    constexpr int yStep = 14; // vertical spacing between tracks
    constexpr int minPulse = 4; // 25% of 15
    constexpr int maxPulse = 11; // 75% of 15

    uint8_t trackCount = trackManager.getTrackCount();
    for (uint8_t i = 0; i < trackCount; ++i) {
        char label[2] = {0};
        label[0] = trackStateToLetter(trackManager.getTrackState(i), !trackManager.isTrackAudible(i));
        int y = y0 + i * yStep;
        uint8_t brightness = 15;
        if (i == selectedTrack) {
            // Pulsate brightness
            float phase = _pulsePhase;
            brightness = minPulse + (maxPulse - minPulse) * (0.5f + 0.5f * sinf(phase * 2 * 3.1415926f));
        } else {
            brightness = 8;
        }
        _display.gfx.draw_text(_display.api.getFrameBuffer(), label, x, y, brightness);
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

void DisplayManager2::update() {
    // Get current global tick count for display timing
    uint32_t currentTick = clockManager.getCurrentTick();
    uint32_t now = millis();

    // Update pulse phase for selected track
    float dt = (now - _lastPulseUpdate) / 1000.0f;
    _pulsePhase += dt * PULSE_SPEED;
    if (_pulsePhase > 1.0f) _pulsePhase -= 1.0f;
    _lastPulseUpdate = now;

    // Clear frame buffer
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

    // Draw vertical track status on the left
    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);

    // Margin for piano roll
    constexpr int TRACK_MARGIN = 20;

    auto& track = trackManager.getSelectedTrack();
    uint32_t startLoop = track.getStartLoopTick();
    uint32_t lengthLoop = track.getLength();
    const auto& notes = track.getNoteEvents();

    // Draw piano roll when loop valid
    const NoteEvent* activeNote = nullptr;
    if (lengthLoop > 0) {
        // Compute position in loop based on currentTick and startLoop
        uint32_t loopPos = (currentTick >= startLoop)
            ? ((currentTick - startLoop) % lengthLoop)
            : 0;

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
        }
        int cx = TRACK_MARGIN + map(loopPos, 0, lengthLoop, 0, BUFFER_WIDTH - 1 - TRACK_MARGIN);
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, 0, 32, 15);
    }

    // Draw info area
    // 1. Current position (playhead) as musical time, with leading zeros
    char posStr[24];
    ticksToBarsBeats16thTicks(currentTick, posStr, sizeof(posStr), true); // true = leading zeros
    char loopLine[32];
    if (lengthLoop > 0 && Config::TICKS_PER_BAR > 0) {
        uint32_t bars = lengthLoop / Config::TICKS_PER_BAR;
        snprintf(loopLine, sizeof(loopLine), "LOOP: %lu", bars);
    } else {
        snprintf(loopLine, sizeof(loopLine), "LOOP: -");
    }
    // Calculate spacing after posStr to align LOOP: and NOTE:
    const char* alignSpaces = " "; // 1 space for alignment
    char posAndLoop[64];
    snprintf(posAndLoop, sizeof(posAndLoop), "%s%s%s", posStr, alignSpaces, loopLine);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), posAndLoop, TRACK_MARGIN + 10, 36, 15); // Top info line

    // 2. Note info line: SS:SS:SSS   NOTE: --- LEN: --- VEL: ---
    char noteLine[96];
    if (activeNote) {
        char startStr[24];
        ticksToBarsBeats16thTicks(activeNote->startNoteTick, startStr, sizeof(startStr), true);
        snprintf(noteLine, sizeof(noteLine), "%s%sNOTE:%3u LEN:%3lu VEL:%3u", startStr, alignSpaces, activeNote->note, activeNote->endNoteTick - activeNote->startNoteTick, activeNote->velocity);
    } else if (!notes.empty()) {
        const auto& last = notes.back();
        char startStr[24];
        ticksToBarsBeats16thTicks(last.startNoteTick, startStr, sizeof(startStr), true);
        snprintf(noteLine, sizeof(noteLine), "%s%sNOTE: %3u LEN: %3lu VEL: %3u", startStr, alignSpaces, last.note, last.endNoteTick - last.startNoteTick, last.velocity);
    } else {
        snprintf(noteLine, sizeof(noteLine), "--:--:--:--%sNOTE:--- LEN:--- VEL:---", alignSpaces);
    }
    _display.gfx.draw_text(_display.api.getFrameBuffer(), noteLine, TRACK_MARGIN + 10, 52, 15); // Info line below, minimal spacing
    
    // Send buffer to display
   _display.api.display();
}
