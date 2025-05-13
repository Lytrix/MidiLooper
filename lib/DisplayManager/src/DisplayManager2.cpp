
// DisplayManager2.cpp
#include "DisplayManager2.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"

DisplayManager2 displayManager2;

DisplayManager2::DisplayManager2()
  : _u8g2(U8G2_R0, /*cs=*/CS_PIN, /*dc=*/DC_PIN, /*reset=*/RST_PIN)
{}

void DisplayManager2::setup() {
    _u8g2.begin();
    _u8g2.setFont(u8g2_font_6x13_tr);
    _u8g2.clearBuffer();
     auto& track = trackManager.getSelectedTrack();
    uint32_t lengthLoop = track.getLength();

    // Draw vertical bar lines for each bar in the loop
    if (lengthLoop > 0) {
        for (uint32_t bar = 0; bar <= lengthLoop; bar += track.getTicksPerBar()) {
            int bx = map(bar, 0, lengthLoop, 0, _u8g2.getDisplayWidth() - 1);
            _u8g2.drawVLine(bx, 0, 32);  // bar height of piano roll area
        }
    }
    _u8g2.sendBuffer();
}

void DisplayManager2::update() {
    constexpr uint32_t TICKS_PER_BAR = MidiConfig::PPQN * Config::QUARTERS_PER_BAR;
    uint32_t currentTick = clockManager.getCurrentTick();
    // Throttle updates
    if (currentTick - _prevDrawTick < DRAW_INTERVAL) return;
    _prevDrawTick = currentTick;

    _u8g2.clearBuffer();

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
            // Compute min/max pitch for scaling (once per update)
        int minPitch = 127;
        int maxPitch = 0;
        for (const auto& n : notes) {
            if (n.note < minPitch) minPitch = n.note;
            if (n.note > maxPitch) maxPitch = n.note;
        }
        int y = map(e.note, minPitch, maxPitch == minPitch ? minPitch + 1 : maxPitch, 31, 0);

            if (eTick < s) {
                int x0 = map(s, 0, lengthLoop, 0, _u8g2.getDisplayWidth() - 1);
                int x1 = _u8g2.getDisplayWidth() - 1;
                _u8g2.drawBox(x0, y, x1 - x0 + 1, 1);
                int x2 = 0;
                int x3 = map(eTick, 0, lengthLoop, 0, _u8g2.getDisplayWidth() - 1);
                _u8g2.drawBox(x2, y, x3 - x2 + 1, 1);
            } else {
                int x0 = map(s, 0, lengthLoop, 0, _u8g2.getDisplayWidth() - 1);
                int x1 = map(eTick, 0, lengthLoop, 0, _u8g2.getDisplayWidth() - 1);
                if (x1 < x0) x1 = x0;
                _u8g2.drawBox(x0, y, x1 - x0 + 1, 1);
            }

            // Always update the most recent matching active note
            bool inNote = (eTick < s)
                ? (loopPos >= s || loopPos < eTick)
                : (loopPos >= s && loopPos < eTick);
            if (inNote) activeNote = &e;
        }
        int cx = map(loopPos, 0, lengthLoop, 0, _u8g2.getDisplayWidth() - 1);
        _u8g2.drawVLine(cx, 0, 32);
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
    _u8g2.drawStr(0, 64 - 16, buf);
    
    // Display tick within loop and loop length
    uint32_t dispTick = (currentTick >= startLoop)
        ? (currentTick - startLoop) % lengthLoop
        : 0;
    snprintf(buf, sizeof(buf), "Tick:%lu Len:%lu", dispTick, lengthLoop);
    _u8g2.drawStr(0, 64 - 4, buf);

    _u8g2.sendBuffer();
}
