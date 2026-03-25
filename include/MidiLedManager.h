#pragma once

#include <Arduino.h>
#include <vector>
#include "Track.h"
#include "MidiHandler.h"
#include "Logger.h"
#include "Globals.h"
#include "MidiConfig.h"

class MidiLedManager {
public:
    MidiLedManager(MidiHandler& midiHandler);
    
    // Update LEDs based on current track and playback position
    void updateLeds(Track& track, uint32_t currentTick);
    
    // Force update all LEDs (useful for track changes)
    void forceUpdate(Track& track, uint32_t currentTick);
    
    // Clear all LEDs
    void clearAllLeds();
    
    // Track row LEDs (ch15 notes 60-67): 127=selected, 32=has data, 0=empty
    // Loop row (50-57): 127=selected slot, 32=slot has data, 0=empty (for selected track)
    void updateTrackSelectLeds(uint8_t selectedTrackIndex, const bool trackHasData[Config::NUM_TRACKS],
                               uint8_t focusSlotIndex, const uint8_t slotVelocities[Config::MAX_LOOPS_PER_TRACK]);
    
    // Update current tick indicator (which 16th step is playing)
    void updateCurrentTick(Track& track, uint32_t currentTick);
    
private:
    static constexpr uint8_t LED_CHANNEL = MidiConfig::Led::CHANNEL;
    static constexpr uint8_t LED_VELOCITY = 64;         // Velocity 64 for normal LEDs
    static constexpr uint8_t TICK_CHANNEL = MidiConfig::Led::CHANNEL;
    static constexpr uint8_t TICK_NOTE_OFFSET = MidiConfig::Led::TICK_OFFSET;
    static constexpr uint8_t TICK_VELOCITY = 127;       // Velocity for current tick indicator
    static constexpr uint8_t NUM_LEDS = 16;             // 16 LEDs for 16th notes
    // 8 bar LED feedback (ch15, notes 40-47 like 16th step note logic)
    static constexpr uint8_t BAR_LED_BASE_NOTE = MidiConfig::Led::BAR_BASE;
    static constexpr uint8_t NUM_BAR_LEDS = 8;
    static constexpr uint8_t VEL_BAR_USED = 32;         // Bar in loop, no notes
    static constexpr uint8_t VEL_BAR_HAS_NOTES = 64;   // Bar contains notes
    static constexpr uint8_t VEL_BAR_CURRENT = 127;    // Current bar highlight overlay
    
    MidiHandler& midiHandler;
    
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
    
    // Track select LED velocity tracking (notes 60-67)
    static constexpr uint8_t NUM_TRACK_LEDS = 8;
    uint8_t lastTrackSelectVelocity[NUM_TRACK_LEDS];
    // Loop select LED velocity tracking (notes 50-57) for selected track
    uint8_t lastLoopSelectVelocity[MidiConfig::Led::LOOP_SELECT_LED_COUNT];
    
    // Helper methods (all use loopStartTick so 16th/bar LEDs reflect user's loop window)
    uint32_t getCurrentBar(uint32_t currentTick, uint32_t loopLength, uint32_t startLoopTick, uint32_t loopStartTick);
    uint32_t getCurrentBarStartTick(uint32_t currentTick, uint32_t loopLength, uint32_t startLoopTick, uint32_t loopStartTick);
    bool hasNoteInSixteenthStep(Track& track, uint32_t stepStartStorage, uint32_t stepEndStorage);
    bool hasNoteInBar(Track& track, uint32_t barStartStorage, uint32_t barEndStorage, uint32_t loopLength);
    void sendLedUpdate(uint8_t ledIndex, bool state);
    void analyzeAndUpdateBar(Track& track, uint32_t barStartTickDisplay, uint32_t loopLength, uint32_t loopStartTick);
    void updateBarLeds(Track& track, uint32_t loopLength, uint32_t currentBar, uint32_t loopStartTick);
}; 