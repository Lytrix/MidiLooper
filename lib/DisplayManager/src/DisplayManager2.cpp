// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"

// SPI Pins - matching example configuration
#define OLED_CLOCK 13
#define OLED_DATA 11
#define OLED_CS 10
#define OLED_DC 9
#define OLED_RESET 8

DisplayManager2 displayManager2;

DisplayManager2::DisplayManager2() : _display(OLED_CS, OLED_DC, BUFFER_HEIGHT, BUFFER_WIDTH, OLED_RESET) {
    pinMode(OLED_CS, OUTPUT);
    pinMode(OLED_DC, OUTPUT);
    pinMode(OLED_RESET, OUTPUT);
    
    // Initialize SPI
    SPI.begin();
    delay(500);  // Same delay as in example
}

void DisplayManager2::setup() {
    // Initialize display
    _display.api.SSD1322_API_init();
    
    // Set buffer size
    _display.gfx.set_buffer_size(BUFFER_WIDTH, BUFFER_HEIGHT);
    
    // Clear the display (fill buffer with zeros and send to display)
    _display.gfx.fill_buffer(_frameBuffer, 0);
    _display.gfx.send_buffer_to_OLED(_frameBuffer, 0, 0);
    
    // Set contrast
    _display.api.SSD1322_API_set_contrast(255);
    
    auto& track = trackManager.getSelectedTrack();
    uint32_t lengthLoop = track.getLength();

    // Clear frame buffer
    memset(_frameBuffer, 0, sizeof(_frameBuffer));

    // Draw vertical bar lines for each bar in the loop
    if (lengthLoop > 0) {
        for (uint32_t bar = 0; bar <= lengthLoop; bar += track.getTicksPerBar()) {
            int bx = map(bar, 0, lengthLoop, 0, BUFFER_WIDTH - 1);
            _display.gfx.draw_vline(_frameBuffer, bx, 0, 32, 15);  // bar height of piano roll area
        }
    }
    _display.gfx.send_buffer_to_OLED(_frameBuffer, 0, 0);
}

void DisplayManager2::update() {
    constexpr uint32_t TICKS_PER_BAR = MidiConfig::PPQN * Config::QUARTERS_PER_BAR;
    uint32_t currentTick = clockManager.getCurrentTick();
    
    // Throttle updates
    if (currentTick - _prevDrawTick < DRAW_INTERVAL) return;
    _prevDrawTick = currentTick;

    // Clear frame buffer
    memset(_frameBuffer, 0, sizeof(_frameBuffer));

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
    _display.gfx.draw_text(_frameBuffer, buf, 0, 48, 15);  // 15 is max brightness
    
    // Display tick within loop and loop length
    uint32_t dispTick = (currentTick >= startLoop)
        ? (currentTick - startLoop) % lengthLoop
        : 0;
    snprintf(buf, sizeof(buf), "Tick:%lu Len:%lu", dispTick, lengthLoop);
    _display.gfx.draw_text(_frameBuffer, buf, 0, 60, 15);

    _display.gfx.send_buffer_to_OLED(_frameBuffer, 0, 0);
}
