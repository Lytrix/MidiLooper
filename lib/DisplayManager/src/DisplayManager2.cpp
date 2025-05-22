// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"

// adafruit fonts - use the build path
#include <FreeMono12pt7b.h>
#include <FreeSansOblique9pt7b.h>

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
    _display.gfx.select_font(&FreeMono12pt7b);
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

    // Clear frame buffer
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

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
                int x0 = map(s, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                int x1 = BUFFER_WIDTH - 1;
                _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, 15);  // 15 is max brightness
                int x2 = 0;
                int x3 = map(eTick, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x2, y, x3, y, 15);
            } else {
                int x0 = map(s, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                int x1 = map(eTick, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
                if (x1 < x0) x1 = x0;
                _display.gfx.draw_rect_filled(_display.api.getFrameBuffer(), x0, y, x1, y, 15);
            }

            // Always update the most recent matching active note
            bool inNote = (eTick < s)
                ? (loopPos >= s || loopPos < eTick)
                : (loopPos >= s && loopPos < eTick);
            if (inNote) activeNote = &e;
        }
        int cx = map(loopPos, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
        _display.gfx.draw_vline(_display.api.getFrameBuffer(), cx, 0, 32, 15);
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
    _display.gfx.draw_text(_display.api.getFrameBuffer(), buf, 10, 48, 15);  // 15 is max brightness
    
    // Display tick within loop and loop length (removed for compile and speed)

    // Send buffer to display
    _display.api.display();
}
