//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDIBUTTONMANAGER_H
#define MIDIBUTTONMANAGER_H

#include <Arduino.h>
#include <vector>

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

/**
 * @class MidiButtonManager
 * @brief Manages MIDI note-based button logic, replacing physical buttons with MIDI notes.
 *
 * Monitors USB Host MIDI channel 1 for specific notes:
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
    void processEncoderMovement(int delta);

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
    static constexpr uint8_t MIDI_CHANNEL = 1;         // Channel to monitor
    
    // MIDI note assignments
    static constexpr uint8_t NOTE_C2 = 36;  // Button A
    static constexpr uint8_t NOTE_C2_SHARP = 37;  // Button B  
    static constexpr uint8_t NOTE_D2 = 38;  // Encoder Button
    
    // MIDI constants for encoder CC
    static constexpr uint8_t ENCODER_CC_CHANNEL = 1;
    static constexpr uint8_t ENCODER_CC_NUMBER = 16;
    static constexpr uint8_t ENCODER_UP_VALUE = 1;    // CC value for encoder up (was down)
    static constexpr uint8_t ENCODER_DOWN_VALUE = 65;  // CC value for encoder down (was up)

    // Encoder state
    int midiEncoderPosition = 0;
    uint32_t lastEncoderTime = 0;
    bool pitchEditActive = false;
    bool wasEncoderButtonHeld = false;
    uint32_t encoderButtonHoldStart = 0;
    static constexpr uint32_t ENCODER_HOLD_DELAY = 250; // ms
    
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

    MidiButtonId getNoteButtonId(uint8_t note);
    bool isValidNote(uint8_t note);
};

extern MidiButtonManager midiButtonManager;

#endif // MIDIBUTTONMANAGER_H 