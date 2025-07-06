//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_MANAGER_H
#define NOTE_EDIT_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "EditManager.h"
#include "Utils/MidiMapping.h"
#include "MidiButtonManagerV2.h"
#include "MidiFaderManagerV2.h"
#include "MidiFaderProcessor.h"
#include "Track.h"
#include "Logger.h"
#include "LoopEditManager.h"

/**
 * @class NoteEditManager
 * @brief Manages MIDI note-based button logic and fader control.
 *
 * This class serves as the main interface for MIDI control, delegating to specialized handlers:
 * - MidiButtonHandler for button press/release logic
 * - MidiFaderHandler for fader control
 */
class NoteEditManager {
public:
    NoteEditManager();
    
    // Specialized handlers
    MidiButtonManagerV2 buttonHandler;
    MidiFaderManagerV2 faderHandler;
    
    //void setup();
    void update();
    
    // MIDI input handlers
    void handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn);
    void handleMidiPitchbend(uint8_t channel, int16_t pitchValue);
    void handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value);
    
    // Fader handler methods (must be public for MidiFaderActions)
    void handleSelectFaderInput(int16_t pitchValue, Track& track);
    void handleCoarseFaderInput(int16_t pitchValue, Track& track);
    void handleFineFaderInput(uint8_t ccValue, Track& track);
    void handleNoteValueFaderInput(uint8_t ccValue, Track& track);

    // Loop editing is now handled by LoopEditManager
    LoopEditManager loopEditManager;

    // Edit mode methods (must be public for MidiButtonActions)
    void cycleEditMode(Track& track);
    void deleteSelectedNote(Track& track);
    void toggleLengthEditingMode();
    
    // Legacy methods - to be replaced by unified system
    void sendStartNotePitchbend(Track& track);  // Sends coarse pitchbend ch15 and fine CC2 ch15
    void sendSelectnoteFaderUpdate(Track& track);  // Schedules selectnote pitchbend ch16 update with delay
    void performSelectnoteFaderUpdate(Track& track);  // Actually sends the selectnote fader update
    void enableStartEditing();
    void moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick);
    void moveNoteToPositionWithOverlapHandling(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick, bool commitChanges);
    void moveNoteToPositionSimple(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick);
    void refreshEditingActivity();  // Mark editing activity to prevent note selection changes
    
    // Overlap handling helper functions
    
    // void applyTemporaryOverlapChanges(std::vector<MidiEvent>& midiEvents,
    //                                  const std::vector<std::pair<NoteUtils::DisplayNote, std::uint32_t>>& notesToShorten,
    //                                  const std::vector<NoteUtils::DisplayNote>& notesToDelete,
    //                                  EditManager& manager, std::uint32_t loopLength,
    //                                  NoteUtils::EventIndexMap& onIndex, NoteUtils::EventIndexMap& offIndex);
    // void restoreTemporaryNotes(std::vector<MidiEvent>& midiEvents,
    //                           const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
    //                           EditManager& manager, std::uint32_t loopLength,
    //                           NoteUtils::EventIndexMap& onIndex, NoteUtils::EventIndexMap& offIndex);
    
    // void extendShortenedNotes(std::vector<MidiEvent>& midiEvents,
    //                          const std::vector<std::pair<EditManager::MovingNoteIdentity::DeletedNote, std::uint32_t>>& notesToExtend,
    //                          EditManager& manager, std::uint32_t loopLength);



    // Main edit mode switching (for mode button functionality)
    enum MainEditMode {
        MAIN_MODE_LOOP_EDIT = 0,    // Loop edit mode: Program 0, Note 100 trigger
        MAIN_MODE_NOTE_EDIT = 1     // Note edit mode: Program 1, Note 0 trigger
    };
    
    // Getter for current main edit mode
    MainEditMode getCurrentMainEditMode() const { return currentMainEditMode; }
    
    // Main edit mode methods
    void sendMainEditModeChange(MainEditMode mode);
    void cycleMainEditMode(Track& track);
    void onTrackChanged(Track& newTrack);
    
    // Current main edit mode state
    MainEditMode currentMainEditMode = MAIN_MODE_NOTE_EDIT;  // Start in note edit mode



private:

    

    
    // MIDI constants for pitchbend navigation
    static constexpr uint8_t PITCHBEND_SELECT_CHANNEL = 16;  // Fader 1: Note selection
    static constexpr uint8_t PITCHBEND_START_CHANNEL = 15;   // Fader 2: Coarse start position (16th steps)
    
    // MIDI constants for program changes
    static constexpr uint8_t PROGRAM_CHANGE_CHANNEL = 16;
    
    // MIDI constants for fine control via CC
    static constexpr uint8_t FINE_CC_CHANNEL = 15;    // Channel 15 for fine CC control (same as coarse)
    static constexpr uint8_t FINE_CC_NUMBER = 2;      // CC2 for fine start position (tick level)
    
    // MIDI constants for note value control via CC
    static constexpr uint8_t NOTE_VALUE_CC_CHANNEL = 15;  // Channel 15 for note value CC control
    static constexpr uint8_t NOTE_VALUE_CC_NUMBER = 3;    // CC3 for note value editing
    static constexpr int16_t PITCHBEND_MIN = -8192;  // Standard MIDI pitchbend minimum
    static constexpr int16_t PITCHBEND_MAX = 8191;   // Standard MIDI pitchbend maximum
    static constexpr int16_t PITCHBEND_CENTER = 0;   // Center position


    
    // Grace period for start editing to prevent conflicts
    static constexpr uint32_t NOTE_SELECTION_GRACE_PERIOD = 750; // ms
    uint32_t noteSelectionTime = 0;
    bool startEditingEnabled = true;
    uint32_t lastEditingActivityTime = 0;
    
    // Smart selection and coarse fader stability - prevent feedback and jitter
    int16_t lastUserSelectFaderValue = PITCHBEND_CENTER;
    uint32_t lastSelectFaderTime = 0;
    static constexpr int16_t SELECT_MOVEMENT_THRESHOLD = 100; // Minimum pitchbend change to be considered intentional
    static constexpr uint32_t SELECT_STABILITY_TIME = 500; // ms between movements to be considered stable
    
    // Coarse fader movement stability - prevent jitter from rescheduling updates
    int16_t lastUserCoarseFaderValue = PITCHBEND_CENTER;
    uint32_t lastCoarseFaderTime = 0;
    static constexpr int16_t COARSE_MOVEMENT_THRESHOLD = 150; // Minimum pitchbend change to be considered intentional
    static constexpr uint32_t COARSE_STABILITY_TIME = 1000; // ms between movements to be considered stable
    
    // Fine CC control state
    uint8_t lastFineCCValue = 64;     // CC2 on channel 16 (center value)
    bool fineCCInitialized = false;
    uint32_t referenceStep = 0;       // 16th step position set by coarse movement
    


    


    
    // Feedback prevention for motorized faders
    uint32_t lastPitchbendSentTime = 0;
    uint32_t lastSelectnoteSentTime = 0;  // Track when we last sent selectnote fader updates
    static constexpr uint32_t PITCHBEND_IGNORE_PERIOD = 1500; // 1500ms to ignore incoming pitchbend after sending
    
    // Scheduled selectnote fader update
    bool pendingSelectnoteUpdate = false;
    uint32_t selectnoteUpdateTime = 0;
    static constexpr uint32_t SELECTNOTE_UPDATE_DELAY = 1600; // Wait 1600ms after coarse/fine updates
    
    // Additional protection against fader 2 updates during active use
    static constexpr uint32_t FADER2_PROTECTION_PERIOD = 2000; // Don't update fader 2 for 2 seconds after any fader 2 activity
    

    
    // Fader state management
    std::vector<MidiFaderProcessor::FaderState> faderStates;
    uint32_t lastDriverFaderUpdateTime = 0;
    MidiMapping::FaderType currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    uint32_t lastDriverFaderTime = 0;
    static constexpr uint32_t FADER_UPDATE_DELAY = 1500; // 1.5 seconds delay for other faders
    static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 1500; // 1.5s to ignore feedback
    

    
    // Length editing mode state
    bool lengthEditingMode = false;
    uint32_t lastLengthModeToggleTime = 0;
    static constexpr uint32_t LENGTH_MODE_DEBOUNCE_TIME = 100; // 100ms debounce protection
    

    
    // Unified fader methods
    void initializeFaderStates();
    void handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue = 0, uint8_t ccValue = 0);
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    void sendFaderUpdate(MidiMapping::FaderType faderType, Track& track);
    void sendFaderPosition(MidiMapping::FaderType faderType, Track& track);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    
    // Individual fader handler methods
    void sendCoarseFaderPosition(Track& track);
    void sendFineFaderPosition(Track& track);
    void sendNoteValueFaderPosition(Track& track);


    // Edit mode cycling - keeping the old system for now but not using it
    enum EditModeState {
        EDIT_MODE_NONE = 0,     // Not in edit mode
        EDIT_MODE_SELECT = 1,   // Select note or grid position
        EDIT_MODE_START = 2,    // Move start note position
        EDIT_MODE_LENGTH = 3,   // Change note length
        EDIT_MODE_PITCH = 4     // Change note pitch
    };
    EditModeState currentEditMode = EDIT_MODE_NONE;
    
    void enterNextEditMode(Track& track);

};

extern NoteEditManager noteEditManager;

#endif // NOTE_EDIT_MANAGER_H 