#pragma once

#include <Arduino.h>
#include <vector>
#include "Track.h"
#include "MidiHandler.h"
#include "Logger.h"
#include "Globals.h"

class MidiLedManager {
public:
    MidiLedManager(MidiHandler& midiHandler);
    
    // Update LEDs based on current track and playback position
    void updateLeds(Track& track, uint32_t currentTick);
    
    // Force update all LEDs (useful for track changes)
    void forceUpdate(Track& track, uint32_t currentTick);
    
    // Clear all LEDs
    void clearAllLeds();
    
    // Update current tick indicator (which 16th step is playing)
    void updateCurrentTick(Track& track, uint32_t currentTick);
    
    // Configure update delays (in microseconds)
    void setUpdateDelay(uint16_t delayMicros);
    
private:
    static constexpr uint8_t LED_CHANNEL = 3;           // Channel 3 - working channel on DROID 1.7 (ch2/ch4 don't receive)
    static constexpr uint8_t LED_VELOCITY = 64;         // Velocity 64 for normal LEDs
    static constexpr uint8_t TICK_CHANNEL = 3;          // Same channel, uses notes 16-31 (TICK_NOTE_OFFSET)
    static constexpr uint8_t TICK_NOTE_OFFSET = 16;     // Tick uses notes 16-31 to avoid conflict with bar content (0-15)
    static constexpr uint8_t TICK_VELOCITY = 127;       // Velocity for current tick indicator
    static constexpr uint8_t NUM_LEDS = 16;             // 16 LEDs for 16th notes
    // 8 bar LED feedback (same channel 3, notes 40-47 like 16th step note logic)
    static constexpr uint8_t BAR_LED_BASE_NOTE = 40;    // Bar 0 = note 40, Bar 1 = 41, ... Bar 7 = 47
    static constexpr uint8_t NUM_BAR_LEDS = 8;
    static constexpr uint8_t VEL_BAR_USED = 32;         // Bar in loop, no notes
    static constexpr uint8_t VEL_BAR_HAS_NOTES = 64;   // Bar contains notes
    static constexpr uint8_t VEL_BAR_CURRENT = 127;    // Current bar highlight overlay
    static constexpr uint16_t DEFAULT_UPDATE_DELAY = 500; // Default 0.5ms delay
    
    MidiHandler& midiHandler;
    uint16_t updateDelayMicros;                         // Configurable delay between updates
    
    // Track the last LED state to avoid redundant updates
    bool lastLedState[NUM_LEDS];
    uint32_t lastUpdateBar;
    uint32_t lastLoopLength;   // Detect loop length changes for immediate refresh
    uint32_t lastLoopStartTick; // Detect loop start changes for immediate refresh
    bool hasInitialized;
    
    // Current tick indicator tracking
    int8_t currentTickStep;                             // Currently active 16th step (-1 = none)

    // Bar LED velocity tracking: only send NoteOn when velocity changes; NoteOff only when required
    static constexpr uint8_t BAR_VEL_NEVER_SENT = 0xFF;
    uint8_t lastBarVelocity[NUM_BAR_LEDS];
    
    // Helper methods (all use loopStartTick so 16th/bar LEDs reflect user's loop window)
    uint32_t getCurrentBar(uint32_t currentTick, uint32_t loopLength, uint32_t startLoopTick, uint32_t loopStartTick);
    uint32_t getCurrentBarStartTick(uint32_t currentTick, uint32_t loopLength, uint32_t startLoopTick, uint32_t loopStartTick);
    bool hasNoteInSixteenthStep(Track& track, uint32_t stepStartStorage, uint32_t stepEndStorage);
    bool hasNoteInBar(Track& track, uint32_t barStartStorage, uint32_t barEndStorage, uint32_t loopLength);
    void sendLedUpdate(uint8_t ledIndex, bool state);
    void analyzeAndUpdateBar(Track& track, uint32_t barStartTickDisplay, uint32_t loopLength, uint32_t loopStartTick);
    void updateBarLeds(Track& track, uint32_t loopLength, uint32_t currentBar, uint32_t loopStartTick);
}; 