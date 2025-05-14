// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"

// adafruit fonts - use the build path
#include <FreeMono12pt7b.h>
#include <FreeSansOblique9pt7b.h>

// SPI Pins for Teensy 4.1
#define OLED_CLOCK 13
#define OLED_DATA 11  // MOSI
#define OLED_CS 10
#define OLED_DC 9
#define OLED_RESET 8

DisplayManager2 displayManager2;

DisplayManager2::DisplayManager2() : _display(OLED_CS, OLED_DC, BUFFER_HEIGHT, BUFFER_WIDTH, OLED_RESET) {
    // Don't initialize SPI here - it will be done in setup()
}

// Helper function to clear the display buffer
void DisplayManager2::clearDisplayBuffer() {
    _display.gfx.fill_buffer(_frameBuffer, 0);
    _display.gfx.send_buffer_to_OLED(_frameBuffer, 0, 0);
}

void DisplayManager2::setup() {
    Serial.println("DisplayManager2: Setting up SSD1322 display...");

    // Set pin modes for SPI control pins (as in example)
    pinMode(OLED_CS, OUTPUT);
    pinMode(OLED_DC, OUTPUT);

    // Initialize SPI
    SPI.begin();
    delay(500); // Longer delay as in example

    // Initialize display
    _display.api.SSD1322_API_init();

    // Set buffer size and clear display
    _display.gfx.set_buffer_size(BUFFER_WIDTH, BUFFER_HEIGHT);
    clearDisplayBuffer();

    // Now proceed with your drawing/demo code
    Serial.println("DisplayManager2: Drawing startup text...");
    Serial.println("Selecting font...");
    _display.gfx.select_font(&FreeMono12pt7b);
    Serial.println("Font selected.");
    Serial.println("Drawing text...");
    _display.gfx.draw_text(_frameBuffer, "MidiLooper", 100, 20, 15);
    _display.gfx.draw_text(_frameBuffer, "v0.2", 120, 40, 15);
    Serial.println("Text drawn.");
    _display.gfx.send_buffer_to_OLED(_frameBuffer, 0, 0);
    Serial.println("DisplayManager2: Text sent to display");
    delay(2000);
    clearDisplayBuffer();
}

void DisplayManager2::update() {
    constexpr uint32_t TICKS_PER_BAR = MidiConfig::PPQN * Config::QUARTERS_PER_BAR;
    uint32_t currentTick = clockManager.getCurrentTick();
    
    // Throttle updates
    if (currentTick - _prevDrawTick < DRAW_INTERVAL) return;
    _prevDrawTick = currentTick;

    // Clear frame buffer
    _display.gfx.fill_buffer(_frameBuffer, 0);

    auto& track = trackManager.getSelectedTrack();
    uint32_t startLoop = track.getStartLoopTick();
    uint32_t lengthLoop = track.getLength();
    const auto& notes = track.getNoteEvents();

    // Draw piano roll when loop valid
    const NoteEvent* activeNote = nullptr;
    if (lengthLoop > 0) {
        // Compute position in loop
        uint32_t loopPos = (currentTick >= startLoop)
            ? (currentTick - startLoop) % lengthLoop
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
                int x0 = map(s, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                int x1 = BUFFER_WIDTH - 1;
                _display.gfx.draw_rect_filled(_frameBuffer, x0, y, x1, y, 15);  // 15 is max brightness
                int x2 = 0;
                int x3 = map(eTick, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                _display.gfx.draw_rect_filled(_frameBuffer, x2, y, x3, y, 15);
            } else {
                int x0 = map(s, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                int x1 = map(eTick, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                if (x1 < x0) x1 = x0;
                _display.gfx.draw_rect_filled(_frameBuffer, x0, y, x1, y, 15);
            }

            // Always update the most recent matching active note
            bool inNote = (eTick < s)
                ? (loopPos >= s || loopPos < eTick)
                : (loopPos >= s && loopPos < eTick);
            if (inNote) activeNote = &e;
        }
        int cx = map(loopPos, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
        _display.gfx.draw_vline(_frameBuffer, cx, 0, 32, 15);
    }

    // Draw info text in bottom area
    char buf[32];
    if (activeNote) {
        snprintf(buf, sizeof(buf), "Note:%3u Vel:%3u Len:%lu", activeNote->note, activeNote->velocity, activeNote->endNoteTick - activeNote->startNoteTick);
    } else if (!notes.empty()) {
        const auto& last = notes.back();
        snprintf(buf, sizeof(buf), "Note:%3u Vel:%3u", last.note, last.velocity);
    } else {
        snprintf(buf, sizeof(buf), "Note:--- Vel:---");
    }
    _display.gfx.draw_text(_frameBuffer, buf, 10, 48, 15);  // 15 is max brightness
    
    // Display tick within loop and loop length
    uint32_t dispTick = (currentTick >= startLoop)
        ? (currentTick - startLoop) % lengthLoop
        : 0;
    snprintf(buf, sizeof(buf), "Tick:%lu Len:%lu", dispTick, lengthLoop);
    _display.gfx.draw_text(_frameBuffer, buf, 10, 60, 15);

    // Send buffer to display
    _display.gfx.send_buffer_to_OLED(_frameBuffer, 0, 0);
}
