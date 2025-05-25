#pragma once

#include <Arduino.h>
#include "SSD1322.h"
#include "Track.h"   // where NoteEvent is declared
#include "TrackManager.h"   // for trackManager
#include "ClockManager.h"   // for clockManager
#include "Globals.h"
#include "EditManager.h"

class DisplayManager2 {
public:
    DisplayManager2();
    void setup();
    void update();
    void clearDisplayBuffer();

    // Margin for piano roll, info area and note info
    static constexpr int TRACK_MARGIN = 20; 
    // Display buffer size
    static constexpr uint32_t DRAW_INTERVAL = 1000 / 30;  // 30 FPS
    static constexpr uint16_t BUFFER_WIDTH = 256;
    static constexpr uint16_t BUFFER_HEIGHT = 64;
    
    uint32_t lastPlayedTick = 0;

    const NoteEvent* getLastPlayedNote() const { return lastPlayedNote; }
    void setLastPlayedNote(const NoteEvent* note) { lastPlayedNote = note; }

private:
    // Pulse and brightness for selected track
    static constexpr int minPulse = 4;       // 25% of 16 steps
    static constexpr int maxPulse = 10;      // 75% of  16 steps
    static constexpr int minBrightness = 8;  // 50%  16 steps
    static constexpr int maxBrightness = 15; // 90%   16 steps
    
    // Edit bracket and note highlight
    static constexpr int BRACKET_COLOR = 8;
    static constexpr int HIGHLIGHT_COLOR = 10;  

    const NoteEvent* activeNote = nullptr;
    const NoteEvent* lastPlayedNote = nullptr;
    uint32_t _prevDrawTick = 0;
    SSD1322 _display;
    // Blinker/pulse state for selected track
    float _pulsePhase = 0.0f; // 0..1
    unsigned long _lastPulseUpdate = 0;


    // Edit Note bracket and highlight
    int tickToScreenX(uint32_t tick);
    int noteToScreenY(uint8_t note);

    static constexpr float PULSE_SPEED = 1.0f; // Pulses per second (slowed by 40%)
    // Track status rendering
    void drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis);
    // Piano roll rendering
    void drawPianoRoll(uint32_t currentTick, Track& selectedTrack);
    // Info area rendering
    void drawInfoArea(uint32_t currentTick, Track& selectedTrack);
    // Note info rendering
    void drawNoteInfo(uint32_t currentTick, Track& selectedTrack);
}; extern DisplayManager2 displayManager2;