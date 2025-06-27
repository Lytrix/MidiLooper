//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDIBUTTONMANAGER_H
#define MIDIBUTTONMANAGER_H

#include <Arduino.h>
#include <vector>
#include "NoteUtils.h"

enum MidiButtonAction {
    MIDI_BUTTON_NONE,
    MIDI_BUTTON_SHORT_PRESS,
    MIDI_BUTTON_DOUBLE_PRESS,
    MIDI_BUTTON_LONG_PRESS
};

enum MidiButtonId {
    MIDI_BUTTON_A = 0,      // C2 (MIDI note 36)
    MIDI_BUTTON_B = 1,      // D2 (MIDI note 38) 
    MIDI_BUTTON_ENCODER = 2 // E2 (MIDI note 40)
};

// Unified Fader State Machine Types
enum FaderType {
    FADER_SELECT = 1,     // Fader 1: Note selection (channel 16, pitchbend)
    FADER_COARSE = 2,     // Fader 2: Coarse positioning (channel 15, pitchbend)  
    FADER_FINE = 3,       // Fader 3: Fine positioning (channel 15, CC2)
    FADER_NOTE_VALUE = 4  // Fader 4: Note value editing (channel 15, CC3)
};

struct FaderState {
    FaderType type;
    uint8_t channel;
    bool isInitialized;
    int16_t lastPitchbendValue;
    uint8_t lastCCValue;
    uint32_t lastUpdateTime;
    uint32_t lastSentTime;
    bool pendingUpdate;
    uint32_t updateScheduledTime;
    FaderType scheduledByDriver;  // Which fader was driver when this update was scheduled
    int16_t lastSentPitchbend;    // Last pitchbend value we sent to this fader
    uint8_t lastSentCC;           // Last CC value we sent to this fader
};

/**
 * @class MidiButtonManager
 * @brief Manages MIDI note-based button logic, replacing physical buttons with MIDI notes.
 *
 * Monitors MIDI channel 16 for specific notes:
 *   - C2 (MIDI note 36) = Button A (Record/Overdub)
 *   - D2 (MIDI note 38) = Button B (Play/Stop)  
 *   - E2 (MIDI note 40) = Encoder Button
 *
 * MIDI note behavior:
 *   - Short press = brief note on/off (< 600ms)
 *   - Long press = extended note on (>= 600ms)
 *   - Double press = two note on events within 300ms window
 *
 * The update() method must be called regularly to process timing and dispatch events.
 */
class MidiButtonManager {
public:
    MidiButtonManager();

    void setup();
    void update();
    void handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn);
    void handleButton(MidiButtonId button, MidiButtonAction action);

    // Encoder handling
    void handleMidiEncoder(uint8_t channel, uint8_t ccNumber, uint8_t value);
    void handleMidiPitchbend(uint8_t channel, int16_t pitchValue);
    void handleMidiCC2Fine(uint8_t channel, uint8_t ccNumber, uint8_t value);
    void handleMidiCC3NoteValue(uint8_t channel, uint8_t ccNumber, uint8_t value);
    void processEncoderMovement(int delta);
    
    // Public access to fader state for smart feedback detection
    FaderState& getFaderState(FaderType faderType);

private:
    struct MidiButtonState {
        bool isPressed;
        uint32_t pressStartTime;
        uint32_t lastTapTime;
        bool pendingShortPress;
        uint32_t shortPressExpireTime;
        uint8_t noteNumber;
    };

    std::vector<MidiButtonState> buttonStates;
    
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
    static constexpr int16_t PITCHBEND_MIN = 0;      // d1=0 d2=0 (full MIDI range minimum)
    static constexpr int16_t PITCHBEND_MAX = 16383;  // d1=127 d2=127 (full MIDI range maximum)
    static constexpr int16_t PITCHBEND_CENTER = 8192; // Center position

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
    static constexpr uint32_t SELECT_STABILITY_TIME = 500; // ms between movements to be considered stable
    
    // Grace period for note selection (prevent selection changes during editing)
    uint32_t lastEditingActivityTime = 0;
    static constexpr uint32_t NOTE_SELECTION_GRACE_PERIOD = 1500; // 1.5 seconds
    
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
    
    std::vector<FaderState> faderStates;
    uint32_t lastDriverFaderUpdateTime = 0;
    FaderType currentDriverFader = FADER_SELECT;
    uint32_t lastDriverFaderTime = 0;
    static constexpr uint32_t FADER_UPDATE_DELAY = 1500; // 1.5 seconds delay for other faders
    static constexpr uint32_t FEEDBACK_IGNORE_PERIOD = 1500; // 1.5s to ignore feedback
    
    // Unified fader methods
    void initializeFaderStates();
    void handleFaderInput(FaderType faderType, int16_t pitchbendValue = 0, uint8_t ccValue = 0);
    void updateFaderStates();
    void scheduleOtherFaderUpdates(FaderType driverFader);
    void sendFaderUpdate(FaderType faderType, Track& track);
    void sendFaderPosition(FaderType faderType, Track& track);
    bool shouldIgnoreFaderInput(FaderType faderType);
    bool shouldIgnoreFaderInput(FaderType faderType, int16_t pitchbendValue, uint8_t ccValue);
    
    // Individual fader handler methods
    void handleSelectFaderInput(int16_t pitchValue, Track& track);
    void handleCoarseFaderInput(int16_t pitchValue, Track& track);
    void handleFineFaderInput(uint8_t ccValue, Track& track);
    void handleNoteValueFaderInput(uint8_t ccValue, Track& track);
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
    void cycleEditMode(Track& track);
    void enterNextEditMode(Track& track);
    void deleteSelectedNote(Track& track);
    void sendEditModeProgram(EditModeState mode);
    
    // Legacy methods - to be replaced by unified system
    void sendStartNotePitchbend(Track& track);  // Sends coarse pitchbend ch15 and fine CC2 ch15
    void sendSelectnoteFaderUpdate(Track& track);  // Schedules selectnote pitchbend ch16 update with delay
    void performSelectnoteFaderUpdate(Track& track);  // Actually sends the selectnote fader update
    void enableStartEditing();
    void moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, uint32_t targetTick);
    void refreshEditingActivity();  // Mark editing activity to prevent note selection changes

    MidiButtonId getNoteButtonId(uint8_t note);
    bool isValidNote(uint8_t note);
};

extern MidiButtonManager midiButtonManager;

#endif // MIDIBUTTONMANAGER_H 