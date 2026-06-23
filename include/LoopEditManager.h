//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef LOOP_EDIT_MANAGER_H
#define LOOP_EDIT_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "Track.h"
#include "MidiHandler.h"
#include "StorageManager.h"
#include "TrackUndo.h"
#include "Logger.h"
#include "Globals.h"
#include "MidiConfig.h"
#include "Utils/NoteUtils.h"

#include "EditSession.h"

/**
 * @class LoopEditManager
 * @brief Manages loop editing functionality including loop start and length editing.
 *
 * This class handles all loop editing operations:
 * - Loop start point editing via fader input
 * - Loop length editing via CC input
 * - Grace period management for loop start editing
 * - Undo/redo support for loop changes
 * - State saving after loop modifications
 */
class LoopEditManager {
public:
    LoopEditManager(MidiHandler& midiHandler);
    
    // Loop start point editing
    void handleLoopStartFaderInput(int16_t pitchValue, Track& track);
    void refreshLoopStartEditingActivity();
    void updateLoopEndpointAfterGracePeriod(Track& track);
    
    // Loop length editing
    void handleLoopLengthInput(uint8_t ccValue, Track& track);
    void sendCurrentLoopLengthCC(Track& track);
    
    // Track change handling
    void onTrackChanged(Track& newTrack);
    
    // Update method for grace period checking and debounced SD flush (call every frame).
    void update();
    
    bool isLoopEditMode() const;

private:
    MidiHandler& midiHandler;

    // Loop start editing grace period and state
    static constexpr uint32_t LOOP_START_GRACE_PERIOD = 1000; // ms
    uint32_t loopStartEditingTime = 0;
    bool loopStartEditingEnabled = true;
    uint32_t lastLoopStartEditingActivityTime = 0;
    
    // Helper methods
    uint32_t calculateLoopStartTick(int16_t pitchValue, Track& track);
    uint32_t calculateLoopLengthFromCC(uint8_t ccValue);
    uint8_t calculateCCFromLoopLength(uint32_t loopLength);
    
    // Movement filtering
    bool isSignificantMovement(uint32_t currentStart, uint32_t newStart);

    /// Coalesce SD writes during rapid loop start/length tweaks so the main loop
    /// (and USB host pumping) is not blocked on every fader step.
    void scheduleDebouncedLoopEditSave();

    static constexpr uint32_t LOOP_EDIT_SAVE_DEBOUNCE_MS = 400;
    uint32_t pendingLoopEditSaveAtMs = 0;
};

#endif // LOOP_EDIT_MANAGER_H 