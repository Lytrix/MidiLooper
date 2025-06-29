//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDIBUTTONACTIONS_H
#define MIDIBUTTONACTIONS_H

#include <Arduino.h>
#include <functional>
#include "Utils/MidiButtonConfig.h"

// Forward declarations
class Track;

/**
 * @class MidiButtonActions
 * @brief Handles execution of individual button actions
 * 
 * This class implements all the individual actions that buttons can trigger,
 * from basic transport controls to complex edit operations.
 */
class MidiButtonActions {
public:
    /**
     * @brief Copied note data for copy/paste operations
     */
    struct CopiedNoteData {
        bool hasData;
        uint8_t note;
        uint8_t velocity;
        uint32_t length;
        uint8_t channel;
    };

private:
    CopiedNoteData copiedNote;

public:
    MidiButtonActions();

    /**
     * @brief Execute an action with a parameter
     * @param actionType The type of action to execute
     * @param parameter Optional parameter for the action
     */
    void executeAction(MidiButtonConfig::ActionType actionType, uint32_t parameter = 0);

    // Core actions (matching current 3-button system)
    void handleToggleRecord();
    void handleSelectTrack(uint8_t trackNumber);
    void handleUndo();
    void handleRedo();
    void handleUndoClearTrack();
    void handleRedoClearTrack();
    void handleClearTrack();
    void handleMuteTrack(uint8_t trackNumber);
    void handleCycleEditMode();
    void handleExitEditMode();
    void handleDeleteNote();
    
    // Extended actions (stubbed for future)
    void handleTogglePlay();
    void handleMoveCurrentTick(int32_t tickOffset);

private:
    // Helper methods
    Track& getCurrentTrack();
    uint32_t getCurrentTick();
    bool isValidTrackNumber(uint8_t trackNumber);
};

// Global instance
extern MidiButtonActions midiButtonActions;

#endif // MIDIBUTTONACTIONS_H 