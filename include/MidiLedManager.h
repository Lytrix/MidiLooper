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
    void updateCurrentTick(uint32_t currentTick, uint32_t loopLength);
    
    // Configure update delays (in microseconds)
    void setUpdateDelay(uint16_t delayMicros);
    
private:
    static constexpr uint8_t LED_CHANNEL = 2;           // Channel 2 for LED control
    static constexpr uint8_t LED_VELOCITY = 127;         // Velocity 64 for normal LEDs
    static constexpr uint8_t TICK_CHANNEL = 3;          // Channel 3 for current tick indicator
    static constexpr uint8_t TICK_VELOCITY = 127;       // Velocity for current tick indicator
    static constexpr uint8_t NUM_LEDS = 16;             // 16 LEDs for 16th notes
    static constexpr uint16_t DEFAULT_UPDATE_DELAY = 500; // Default 0.5ms delay
    
    MidiHandler& midiHandler;
    uint16_t updateDelayMicros;                         // Configurable delay between updates
    
    // Track the last LED state to avoid redundant updates
    bool lastLedState[NUM_LEDS];
    uint32_t lastUpdateBar;
    bool hasInitialized;
    
    // Current tick indicator tracking
    int8_t currentTickStep;                             // Currently active 16th step (-1 = none)
    
    // Helper methods
    uint32_t getCurrentBar(uint32_t currentTick, uint32_t loopLength);
    uint32_t getCurrentBarStartTick(uint32_t currentTick, uint32_t loopLength);
    bool hasNoteInSixteenthStep(Track& track, uint32_t stepStartTick, uint32_t stepEndTick);
    void sendLedUpdate(uint8_t ledIndex, bool state);
    void analyzeAndUpdateBar(Track& track, uint32_t barStartTick, uint32_t loopLength);
}; 