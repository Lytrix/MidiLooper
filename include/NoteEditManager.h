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
    
    // Configuration
    void addButtonMapping(uint8_t note, uint8_t channel, const std::string& description);
    void addFaderMapping(uint8_t channel, uint8_t ccNumber, bool usePitchBend, const std::string& description);
    void setEncoderMapping(uint8_t channel, uint8_t ccNumber, uint8_t upValue, uint8_t downValue, const std::string& description);
    
    // Fader handler methods (must be public for MidiFaderActions)
    void handleSelectFaderInput(int16_t pitchValue, Track& track);
    void handleCoarseFaderInput(int16_t pitchValue, Track& track);
    void handleFineFaderInput(uint8_t ccValue, Track& track);
    void handleNoteValueFaderInput(uint8_t ccValue, Track& track);

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
    bool notesOverlap(std::uint32_t start1, std::uint32_t end1, std::uint32_t start2, std::uint32_t end2, std::uint32_t loopLength);
    std::uint32_t calculateNoteLength(std::uint32_t start, std::uint32_t end, std::uint32_t loopLength);
    MidiEvent* findCorrespondingNoteOff(std::vector<MidiEvent>& midiEvents, MidiEvent* noteOnEvent, uint8_t pitch, std::uint32_t startTick, std::uint32_t endTick);
    void findOverlapsForMovement(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                                uint8_t movingNotePitch, std::uint32_t currentStart, std::uint32_t newStart, std::uint32_t newEnd,
                                int delta, std::uint32_t loopLength,
                                std::vector<std::pair<NoteUtils::DisplayNote, std::uint32_t>>& notesToShorten,
                                std::vector<NoteUtils::DisplayNote>& notesToDelete);
    void applyTemporaryOverlapChanges(std::vector<MidiEvent>& midiEvents,
                                     const std::vector<std::pair<NoteUtils::DisplayNote, std::uint32_t>>& notesToShorten,
                                     const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                                     EditManager& manager, std::uint32_t loopLength,
                                     NoteUtils::EventIndexMap& onIndex, NoteUtils::EventIndexMap& offIndex);
    void restoreTemporaryNotes(std::vector<MidiEvent>& midiEvents,
                              const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                              EditManager& manager, std::uint32_t loopLength,
                              NoteUtils::EventIndexMap& onIndex, NoteUtils::EventIndexMap& offIndex);
    
    void extendShortenedNotes(std::vector<MidiEvent>& midiEvents,
                             const std::vector<std::pair<EditManager::MovingNoteIdentity::DeletedNote, std::uint32_t>>& notesToExtend,
                             EditManager& manager, std::uint32_t loopLength);

    // MidiButtonId getNoteButtonId(uint8_t note);
    bool isValidNote(uint8_t note);

private:
    // struct MidiButtonState {
    //     bool isPressed;
    //     uint32_t pressStartTime;
    //     uint32_t lastTapTime;
    //     bool pendingShortPress;
    //     uint32_t shortPressExpireTime;
    //     uint8_t noteNumber;
    //     // Triple press tracking
    //     uint32_t secondTapTime;
    //     bool pendingDoublePress;
    //     uint32_t doublePressExpireTime;
    // };

   // std::vector<MidiButtonState> buttonStates;
    
    static constexpr uint16_t DOUBLE_TAP_WINDOW = 300;  // ms
    static constexpr uint16_t LONG_PRESS_TIME = 600;   // ms
    static constexpr uint8_t MIDI_CHANNEL = 16;        // Channel to monitor
    
    // MIDI note assignments
    static constexpr uint8_t NOTE_C2 = 36;  // Button A
    static constexpr uint8_t NOTE_C2_SHARP = 37;  // Button B  
    static constexpr uint8_t NOTE_D2 = 38;  // Encoder Button
    
    // MIDI constants for encoder CC
    static constexpr uint8_t ENCODER_CC_CHANNEL = 16;
    static constexpr uint8_t ENCODER_CC_NUMBER = 16;
    static constexpr uint8_t ENCODER_UP_VALUE = 1;    // CC value for encoder up (was down)
    static constexpr uint8_t ENCODER_DOWN_VALUE = 65;  // CC value for encoder down (was up)
    
    // MIDI constants for program changes
    static constexpr uint8_t PROGRAM_CHANGE_CHANNEL = 16;
    
    // MIDI constants for pitchbend navigation
    static constexpr uint8_t PITCHBEND_SELECT_CHANNEL = 16;  // Fader 1: Note selection
    static constexpr uint8_t PITCHBEND_START_CHANNEL = 15;   // Fader 2: Coarse start position (16th steps)
    
    // MIDI constants for fine control via CC
    static constexpr uint8_t FINE_CC_CHANNEL = 15;    // Channel 15 for fine CC control (same as coarse)
    static constexpr uint8_t FINE_CC_NUMBER = 2;      // CC2 for fine start position (tick level)
    
    // MIDI constants for note value control via CC
    static constexpr uint8_t NOTE_VALUE_CC_CHANNEL = 15;  // Channel 15 for note value CC control
    static constexpr uint8_t NOTE_VALUE_CC_NUMBER = 3;    // CC3 for note value editing
    static constexpr int16_t PITCHBEND_MIN = -8192;  // Standard MIDI pitchbend minimum
    static constexpr int16_t PITCHBEND_MAX = 8191;   // Standard MIDI pitchbend maximum
    static constexpr int16_t PITCHBEND_CENTER = 0;   // Center position

    // Encoder state
    int midiEncoderPosition = 0;
    uint32_t lastEncoderTime = 0;
    bool pitchEditActive = false;
    bool wasEncoderButtonHeld = false;
    uint32_t encoderButtonHoldStart = 0;
    static constexpr uint32_t ENCODER_HOLD_DELAY = 250; // ms
    
    // Pitchbend navigation state
    int16_t lastPitchbendSelectValue = PITCHBEND_CENTER;   // Fader 1 (channel 16)
    int16_t lastPitchbendStartValue = PITCHBEND_CENTER;    // Fader 2 (channel 15)
    bool pitchbendSelectInitialized = false;
    bool pitchbendStartInitialized = false;
    
    // Fine CC control state
    uint8_t lastFineCCValue = 64;     // CC2 on channel 16 (center value)
    bool fineCCInitialized = false;
    uint32_t referenceStep = 0;       // 16th step position set by coarse movement
    
    // Grace period for start note editing (prevent fader overwhelm)
    uint32_t noteSelectionTime = 0;
    static constexpr uint32_t START_EDIT_GRACE_PERIOD = 1500; // 1 second
    bool startEditingEnabled = false;
    
    // Smart selection stability - prevent motorized fader feedback from changing selection
    int16_t lastUserSelectFaderValue = PITCHBEND_CENTER;
    uint32_t lastSelectFaderTime = 0;
    static constexpr int16_t SELECT_MOVEMENT_THRESHOLD = 200; // Minimum pitchbend change to be considered intentional
    static constexpr uint32_t SELECT_STABILITY_TIME = 1500; // ms between movements to be considered stable
    
    // Coarse fader movement stability - prevent jitter from rescheduling updates
    int16_t lastUserCoarseFaderValue = PITCHBEND_CENTER;
    uint32_t lastCoarseFaderTime = 0;
    static constexpr int16_t COARSE_MOVEMENT_THRESHOLD = 150; // Minimum pitchbend change to be considered intentional
    static constexpr uint32_t COARSE_STABILITY_TIME = 1000; // ms between movements to be considered stable
    
    // Grace period for note selection (prevent selection changes during editing)
    uint32_t lastEditingActivityTime = 0;
    static constexpr uint32_t NOTE_SELECTION_GRACE_PERIOD = 0; // Disabled - allow fader1 during edit mode
    
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
    
    // Track fader 1 activity to prevent fader 2 updates during selectnote fader use
    uint32_t lastSelectnoteFaderTime = 0;
    static constexpr uint32_t SELECTNOTE_PROTECTION_PERIOD = 2000; // Don't update fader 2 for 2 seconds after fader 1 activity
    
    // Length editing mode state
    bool lengthEditingMode = false;
    uint32_t lastLengthModeToggleTime = 0;
    static constexpr uint32_t LENGTH_MODE_DEBOUNCE_TIME = 100; // 100ms debounce protection
    
    std::vector<MidiFaderProcessor::FaderState> faderStates;
    uint32_t lastDriverFaderUpdateTime = 0;
    MidiMapping::FaderType currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    uint32_t lastDriverFaderTime = 0;
    static constexpr uint32_t FADER_UPDATE_DELAY = 1500; // 1.5 seconds delay for other faders
    static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 1500; // 1.5s to ignore feedback
    
    // Unified fader methods
    void initializeFaderStates();
    void handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue = 0, uint8_t ccValue = 0);
    void updateFaderStates();
    void scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader);
    void sendFaderUpdate(MidiMapping::FaderType faderType, Track& track);
    void sendFaderPosition(MidiMapping::FaderType faderType, Track& track);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType);
    bool shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    
    // Individual fader handler methods
    void sendCoarseFaderPosition(Track& track);
    void sendFineFaderPosition(Track& track);
    void sendNoteValueFaderPosition(Track& track);

    // Edit mode cycling
    enum EditModeState {
        EDIT_MODE_NONE = 0,     // Not in edit mode
        EDIT_MODE_SELECT = 1,   // Select note or grid position
        EDIT_MODE_START = 2,    // Move start note position
        EDIT_MODE_LENGTH = 3,   // Change note length
        EDIT_MODE_PITCH = 4     // Change note pitch
    };
    EditModeState currentEditMode = EDIT_MODE_NONE;
    void enterNextEditMode(Track& track);
    void sendEditModeProgram(EditModeState mode);

    // Fader input processing
    void processSelectFaderInput(int16_t pitchValue, Track& track);
    void processCoarseFaderInput(int16_t pitchValue, Track& track);
    void processFineFaderInput(uint8_t ccValue, Track& track);
    void processNoteValueFaderInput(uint8_t ccValue, Track& track);
    
    // Fader update scheduling
    void scheduleFaderUpdate(uint8_t faderType, uint32_t delayMs);
    void processScheduledUpdates();
    
    // Helper methods
    uint16_t calculateTargetTick(int16_t pitchValue, uint16_t loopLength);
    uint8_t calculateTargetStep(int16_t pitchValue, uint8_t numSteps);
    uint8_t calculateTargetOffset(uint8_t ccValue, uint8_t numSteps);
    uint8_t calculateTargetNoteValue(uint8_t ccValue);
};

extern NoteEditManager noteEditManager;

#endif // NOTE_EDIT_MANAGER_H 