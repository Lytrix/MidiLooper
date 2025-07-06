//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef FADER_MANAGER_H
#define FADER_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "Track.h"
#include "Logger.h"
#include "Utils/MidiMapping.h"

/**
 * @class FaderManager
 * @brief Manages all fader-related functionality including input processing, position updates, and scheduling.
 *
 * This class consolidates all fader control logic that was previously scattered across
 * NoteEditManager and other components. It handles:
 * - Fader input processing (pitchbend and CC)
 * - Fader position updates and scheduling
 * - Loop start and length editing
 * - Note selection and editing faders
 */
class FaderManager {
public:
    FaderManager();
    
    // Main fader input handlers
    void handleSelectFaderInput(int16_t pitchValue, Track& track);
    void handleCoarseFaderInput(int16_t pitchValue, Track& track);
    void handleFineFaderInput(uint8_t ccValue, Track& track);
    void handleNoteValueFaderInput(uint8_t ccValue, Track& track);
    

    
    // Fader update scheduling and processing
    void scheduleFaderUpdate(uint8_t faderType, uint32_t delayMs);
    void processScheduledUpdates();
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    
    // Fader position sending
    void sendFaderUpdate(MidiMapping::FaderType faderType, Track& track);
    void sendFaderPosition(MidiMapping::FaderType faderType, Track& track);
    void sendCoarseFaderPosition(Track& track);
    void sendFineFaderPosition(Track& track);
    void sendNoteValueFaderPosition(Track& track);
    void sendSelectnoteFaderUpdate(Track& track);
    void performSelectnoteFaderUpdate(Track& track);
    
    // Input filtering and state management
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    void initializeFaderStates();
    void updateFaderStates();
    
    // Helper methods
    uint16_t calculateTargetTick(int16_t pitchValue, uint16_t loopLength);
    uint8_t calculateTargetStep(int16_t pitchValue, uint8_t numSteps);
    uint8_t calculateTargetOffset(uint8_t ccValue, uint8_t numSteps);
    uint8_t calculateTargetNoteValue(uint8_t ccValue);
    
    // Legacy methods for compatibility
    void sendStartNotePitchbend(Track& track);
    void refreshEditingActivity();

private:
    // MIDI constants for pitchbend navigation
    static constexpr uint8_t PITCHBEND_SELECT_CHANNEL = 16;  // Fader 1: Note selection
    static constexpr uint8_t PITCHBEND_START_CHANNEL = 15;   // Fader 2: Coarse start position (16th steps)
    
    // MIDI constants for fine control via CC
    static constexpr uint8_t FINE_CC_CHANNEL = 15;    // Channel 15 for fine CC control (same as coarse)
    static constexpr uint8_t FINE_CC_NUMBER = 2;      // CC2 for fine start position (tick level)
    
    // MIDI constants for note value control via CC
    static constexpr uint8_t NOTE_VALUE_CC_CHANNEL = 15;  // Channel 15 for note value CC control
    static constexpr uint8_t NOTE_VALUE_CC_NUMBER = 3;    // CC3 for note value editing
    

    
    // Pitchbend constants
    static constexpr int16_t PITCHBEND_MIN = -8192;  // Standard MIDI pitchbend minimum
    static constexpr int16_t PITCHBEND_MAX = 8191;   // Standard MIDI pitchbend maximum
    static constexpr int16_t PITCHBEND_CENTER = 0;   // Center position
    
    // Grace periods
    static constexpr uint32_t NOTE_SELECTION_GRACE_PERIOD = 750; // ms
    
    // Fader state tracking
    int16_t lastPitchbendSelectValue = PITCHBEND_CENTER;   // Fader 1 (channel 16)
    int16_t lastPitchbendStartValue = PITCHBEND_CENTER;    // Fader 2 (channel 15)
    uint8_t lastFineCCValue = 64;     // CC2 on channel 15 (center value)
    uint8_t lastNoteValueCCValue = 64; // CC3 on channel 15 (center value)
    
    bool pitchbendSelectInitialized = false;
    bool pitchbendStartInitialized = false;
    bool fineCCInitialized = false;
    bool noteValueCCInitialized = false;
    
    // Grace period state
    uint32_t noteSelectionTime = 0;
    bool startEditingEnabled = true;
    uint32_t lastEditingActivityTime = 0;
    

    
    // Fader update scheduling
    struct ScheduledUpdate {
        uint8_t faderType;
        uint32_t executeTime;
        bool active;
    };
    std::vector<ScheduledUpdate> scheduledUpdates;
    
    // Pending selectnote fader update
    bool pendingSelectnoteUpdate = false;
    uint32_t selectnoteUpdateTime = 0;
    
    // Reference step for fine control
    uint32_t referenceStep = 0;       // 16th step position set by coarse movement
};

extern FaderManager faderManager;

#endif // FADER_MANAGER_H 