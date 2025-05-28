#pragma once

#include <Arduino.h>
#include "SSD1322.h"
#include "Track.h"
#include "TrackManager.h"   // for trackManager
#include "ClockManager.h"   // for clockManager
#include "Globals.h"
#include "EditManager.h"
#include <vector>
#include <cstdint>
#include "MidiEvent.h"
#include "NoteUtils.h"

// Shared struct for UI note representation
using DisplayNote = NoteUtils::DisplayNote;

/**
 * @class DisplayManager
 * @brief Renders the graphical user interface for the MIDI looper.
 *
 * The DisplayManager draws all components of the looper UI onto the SSD1322 display:
 *  - Piano roll view with note bars and playhead cursor
 *  - Note selection bracket and highlight
 *  - Track status indicators and program/editor overlays
 *  - Info area displaying loop length, undo count, and transport position
 *  - Note detail area showing selected note parameters (pitch, length, velocity)
 *
 * It consumes MIDI event data, clock ticks, and state from EditManager and TrackManager,
 * and must have its update() method called regularly (e.g., at ~30 FPS) to refresh the display.
 */
class DisplayManager {
public:
    DisplayManager();
    void setup();
    void update();
    void clearDisplayBuffer();

    // Margin for piano roll, info area and note info
    static constexpr int TRACK_MARGIN = 22; 
    // Display buffer size
    static constexpr uint32_t DRAW_INTERVAL = 1000 / 30;  // 30 FPS
    
    uint32_t lastPlayedTick = 0;

    // If you want to track the last played note for display, use this:
    DisplayNote lastPlayedDisplayNote = {0, 0, 0, 0};
    // Or remove if not needed
    // const DisplayNote* getLastPlayedNote() const { return &lastPlayedDisplayNote; }
    // void setLastPlayedNote(const DisplayNote* note) { if (note) lastPlayedDisplayNote = *note; }

    // Helper functions for piano roll rendering
    void drawGridLines(uint32_t lengthLoop, int pianoRollY0, int pianoRollY1);
    void drawNoteBar(const DisplayNote& e, int y, uint32_t s, uint32_t eTick, uint32_t lengthLoop, int noteBrightness);
    void drawAllNotes(const std::vector<MidiEvent>& midiEvents, uint32_t startLoop, uint32_t lengthLoop, int minPitch, int maxPitch);
    void drawBracket(uint32_t bracketTick, uint32_t lengthLoop, int pianoRollY1);

private:
    // Pulse and brightness for selected track
    static constexpr int minPulse = 4;       // 25% of 16 steps
    static constexpr int maxPulse = 10;      // 75% of  16 steps
    static constexpr int minBrightness = 8;  // 50%  16 steps
    static constexpr int maxBrightness = 15; // 90%   16 steps
    
    // Edit bracket and note highlight
    static constexpr int BRACKET_COLOR = 8;
    static constexpr int HIGHLIGHT_COLOR = 10;  

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
    void drawInfoField(const char* label, const char* value, int x, int y, bool highlight, uint8_t defaultBrightness);
}; extern DisplayManager displayManager;