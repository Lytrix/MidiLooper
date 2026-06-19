//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_MANAGER_H
#define NOTE_EDIT_MANAGER_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "EditManager.h"
#include "Utils/MidiMapping.h"
#include "MidiButtonManager.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"
#include "Track.h"
#include "Logger.h"
#include "LoopEditManager.h"
#include "MidiConfig.h"
#include "Utils/SelectNavigation.h"

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
    MidiButtonManager buttonHandler;
    MidiFaderManager faderHandler;
    
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

    /** One nav slot per note (multiple per 16th when notes share a step) or empty grid step. */
    static std::vector<SelectNavigation::SelectNavSlot> buildSelectNavigationSlots(
        const Track& track, uint32_t bracketTick, bool includeBracketIfMissing = true);
    /// NOTE_EDIT UI list: filtered when **NoteEditSession** active, else **getCachedNotes()** copy.
    static std::vector<NoteUtils::DisplayNote> selectableDisplayNotesForEditUi(const Track& track);

    // Loop editing is now handled by LoopEditManager
    LoopEditManager loopEditManager;

    // Edit mode methods (must be public for MidiButtonActions)
    void cycleEditMode(Track& track);
    void deleteSelectedNote(Track& track);
    void toggleLengthEditingMode();
    /** Force position-edit routing when opening or closing a note-edit session. */
    void resetLengthEditingModeOnSessionBoundary();
    /** Return fader 2/3 to position edit after fader-1 note select (leaves length mode). */
    void resetLengthEditingModeOnNoteSelect();
    
    // Legacy methods - to be replaced by unified system
    void sendStartNotePitchbend(Track& track);  // Sends coarse pitchbend ch15 and fine CC2 ch15
    void sendSelectnoteFaderUpdate(Track& track);  // Schedules selectnote pitchbend ch16 update with delay
    void performSelectnoteFaderUpdate(Track& track);  // Actually sends the selectnote fader update
    void enableStartEditing();
    void moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick);
    void moveNoteToPositionWithOverlapHandling(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick, bool commitChanges);
    void moveNoteToPositionSimple(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick);
    void changeNoteEndWithOverlapHandling(Track& track, const NoteUtils::DisplayNote& currentNote,
                                          std::uint32_t targetEndTick);
    void refreshEditingActivity();  // Mark editing activity to prevent note selection changes
    
    // Overlap handling helper functions
    
    // void applyTemporaryOverlapChanges(std::vector<MidiEvent>& midiEvents,
    //                                  const std::vector<std::pair<NoteUtils::DisplayNote, std::uint32_t>>& notesToShorten,
    //                                  const std::vector<NoteUtils::DisplayNote>& notesToDelete,
    //                                  EditManager& manager, std::uint32_t loopLength,
    //                                  NoteUtils::EventIndexMap& onIndex, NoteUtils::EventIndexMap& offIndex);
    // void restoreTemporaryNotes(...); — removed with MovingNoteIdentity (Phase 2d)



    // Main edit mode switching (for mode button functionality)
    enum MainEditMode {
        // Logical mode IDs (MIDI program mapping is handled in sendMainEditModeChange()).
        MAIN_MODE_LOOP_EDIT = 0,
        MAIN_MODE_NOTE_EDIT = 1
    };
    
    // Getter for current main edit mode
    MainEditMode getCurrentMainEditMode() const { return currentMainEditMode; }
    
    // Main edit mode methods
    void sendMainEditModeChange(MainEditMode mode);
    void cycleMainEditMode(Track& track);
    void onTrackChanged(Track& newTrack);
    
    // Current main edit mode state. Startup policy is set by orchestration (main/looper setup).
    MainEditMode currentMainEditMode = MAIN_MODE_NOTE_EDIT;



private:

    

    
    // MIDI constants (from MidiConfig)
    static constexpr uint8_t PITCHBEND_SELECT_CHANNEL = MidiConfig::Fader::SELECT_CHANNEL;
    static constexpr uint8_t PITCHBEND_START_CHANNEL = MidiConfig::Fader::COARSE_CHANNEL;
    static constexpr uint8_t PROGRAM_CHANGE_CHANNEL = MidiConfig::PROGRAM_CHANGE_CHANNEL;
    static constexpr uint8_t FINE_CC_CHANNEL = MidiConfig::Fader::FINE_CHANNEL;
    static constexpr uint8_t FINE_CC_NUMBER = MidiConfig::Fader::FINE_CC;
    static constexpr uint8_t NOTE_VALUE_CC_CHANNEL = MidiConfig::Fader::NOTE_VALUE_CHANNEL;
    static constexpr uint8_t NOTE_VALUE_CC_NUMBER = MidiConfig::Fader::NOTE_VALUE_CC;


    
    // Grace period for start editing to prevent conflicts
    static constexpr uint32_t NOTE_SELECTION_GRACE_PERIOD = 750; // ms
    uint32_t noteSelectionTime = 0;
    bool startEditingEnabled = true;
    uint32_t lastEditingActivityTime = 0;
    
    // Smart selection and coarse fader stability - prevent feedback and jitter
    int16_t lastUserSelectFaderValue = MidiConfig::Pitchbend::CENTER;
    uint32_t lastSelectFaderTime = 0;
    static constexpr int16_t SELECT_MOVEMENT_THRESHOLD = 100; // Minimum pitchbend change to be considered intentional
    static constexpr uint32_t SELECT_STABILITY_TIME = 500; // ms between movements to be considered stable
    
    // Coarse fader movement stability - prevent jitter from rescheduling updates
    int16_t lastUserCoarseFaderValue = MidiConfig::Pitchbend::CENTER;
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
    

    
    // Fader state management - now delegated to MidiFaderProcessor
    MidiFaderProcessor* faderProcessor = nullptr;
    uint32_t lastDriverFaderUpdateTime = 0;
    MidiMapping::FaderType currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    uint32_t lastDriverFaderTime = 0;
    static constexpr uint32_t FADER_UPDATE_DELAY = 1500; // 1.5 seconds delay for other faders
    static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 1500; // 1.5s to ignore feedback
    

    
    // Length editing mode state
    bool lengthEditingMode = false;
    uint32_t lastLengthModeToggleTime = 0;
    static constexpr uint32_t LENGTH_MODE_DEBOUNCE_TIME = 100; // 100ms debounce protection
    

    
public:
    // Unified fader methods
    void setFaderProcessor(MidiFaderProcessor* processor) { faderProcessor = processor; }
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