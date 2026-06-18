//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_BUTTON_CONFIG_H
#define MIDI_BUTTON_CONFIG_H

#include <Arduino.h>
#include <cstdint>
#include "MidiConfig.h"
#include <vector>
#include <functional>

// Forward declarations
class Track;

namespace MidiButtonConfig {

// Button action types
enum class ActionType {
    NONE,
    TOGGLE_RECORD,
    TOGGLE_PLAY,
    MOVE_CURRENT_TICK,
    SELECT_TRACK,
    UNDO,
    REDO,
    UNDO_CLEAR_TRACK,      // Specific undo for track clearing
    REDO_CLEAR_TRACK,      // Specific redo for track clearing
    ENTER_EDIT_MODE,
    EXIT_EDIT_MODE,
    CYCLE_EDIT_MODE,
    DELETE_NOTE,
    CREATE_NOTE_AT_BRACKET,
    DELETE_OR_CREATE_NOTE,  // NOTELEN double: delete selection or create at empty bracket
    COPY_NOTE,
    PASTE_NOTE,
    QUANTIZE,
    CLEAR_TRACK,
    TOGGLE_RECORD_FOR_SLOT,   // Parameter = slot index 0-7
    CLEAR_TRACK_FOR_SLOT,     // Parameter = slot index
    OVERDUB_FOR_SLOT,         // Parameter = slot index
    UNDO_FOR_SLOT,            // Parameter = slot index
    REDO_FOR_SLOT,            // Parameter = slot index
    MUTE_TRACK,
    SOLO_TRACK,
    SET_LOOP_START,
    SET_LOOP_END,
    TOGGLE_LENGTH_EDIT_MODE,
    TOGGLE_TRANSPORT,
    RESET_TO_LOOP_START,
    CUSTOM_ACTION
};

// Press types for different button behaviors
enum class PressType {
    SHORT_PRESS,    // Quick tap
    DOUBLE_PRESS,   // Double tap
    TRIPLE_PRESS,   // Triple tap
    LONG_PRESS      // Hold
};

// Custom action function type
using CustomActionFunc = std::function<void(Track& track, uint32_t currentTick)>;

// Button configuration structure
struct ButtonConfig {
    uint8_t note;                    // MIDI note number
    uint8_t channel;                 // MIDI channel
    ActionType shortPressAction;     // Action for short press
    ActionType longPressAction;      // Action for long press
    ActionType doublePressAction;    // Action for double press
    ActionType triplePressAction;    // Action for triple press
    CustomActionFunc customAction;   // Custom action function
    const char* description;         // Human readable description
    int32_t parameter;               // Optional parameter (e.g., track number, tick offset)
    
    // Timing configuration (externalized from processor)
    uint32_t doubleTapWindow;        // Window for double tap detection (ms)
    uint32_t tripleTapWindow;        // Window for triple tap detection (ms)
    uint32_t longPressTime;          // Time threshold for long press (ms)
    uint32_t debounceMs;             // Ignore NoteOn within this many ms after last release (0 = disabled)
    bool isMomentary;                // True for momentary buttons (trigger on both press and release)
    
    ButtonConfig(uint8_t n, uint8_t ch, const char* desc) 
        : note(n), channel(ch), 
          shortPressAction(ActionType::NONE),
          longPressAction(ActionType::NONE),
          doublePressAction(ActionType::NONE),
          triplePressAction(ActionType::NONE),
          customAction(nullptr),
          description(desc),
          parameter(0),
          doubleTapWindow(300),
          tripleTapWindow(400),
          longPressTime(600),
          debounceMs(0),
          isMomentary(false) {}
    
    // Builder pattern methods for easy configuration
    ButtonConfig& onShortPress(ActionType action) { shortPressAction = action; return *this; }
    ButtonConfig& onLongPress(ActionType action) { longPressAction = action; return *this; }
    ButtonConfig& onDoublePress(ActionType action) { doublePressAction = action; return *this; }
    ButtonConfig& onTriplePress(ActionType action) { triplePressAction = action; return *this; }
    ButtonConfig& withParameter(int32_t param) { parameter = param; return *this; }
    ButtonConfig& withCustomAction(CustomActionFunc func) { customAction = func; return *this; }
    ButtonConfig& withTiming(uint32_t doubleTap, uint32_t tripleTap, uint32_t longPress) {
        doubleTapWindow = doubleTap; tripleTapWindow = tripleTap; longPressTime = longPress; return *this;
    }
    ButtonConfig& withDebounce(uint32_t ms) { debounceMs = ms; return *this; }
    ButtonConfig& asMomentary(bool momentary = true) { isMomentary = momentary; return *this; }
};

// Configuration class
class Config {
public:
    static void initialize();
    static void addButton(const ButtonConfig& config);
    static const std::vector<ButtonConfig>& getButtonConfigs();
    static const ButtonConfig* findButtonConfig(uint8_t note, uint8_t channel);
    static void clearConfigs();
    
    // Get timing configuration (returns first button's timing as default)
    static void getTimingConfig(uint32_t& doubleTapWindow, uint32_t& tripleTapWindow, uint32_t& longPressTime);
    
    // Convenience methods for common configurations
    static void addRecordButton(uint8_t note, uint8_t channel = 1);
    static void addPlayButton(uint8_t note, uint8_t channel = 1);
    static void addTrackSelectButton(uint8_t note, uint8_t trackNumber, uint8_t channel = 1);
    static void addTickMoveButton(uint8_t note, int32_t tickOffset, uint8_t channel = 1);
    static void addEditModeButton(uint8_t note, uint8_t channel = 1);
    static void addUndoRedoButton(uint8_t note, uint8_t channel = 1);
    
    // Load DROID button configuration (single preset, matches droid/midilooper_v1.ini)
    static void loadConfiguration();
    
private:
    static std::vector<ButtonConfig> buttonConfigs;
    static bool initialized;
};

// Default Channels (alias MidiConfig)
namespace Channels {
    constexpr uint8_t MAIN_BUTTONS = MidiConfig::Channels::DEFAULT;
    constexpr uint8_t TRACK_SELECT = MidiConfig::Channels::TRACK_SELECT;
    constexpr uint8_t EDIT_FUNCTIONS = MidiConfig::Channels::LED_FEEDBACK;
    constexpr uint8_t TRANSPORT = MidiConfig::Channels::TRANSPORT;
}

} // namespace MidiButtonConfig

#endif // MIDI_BUTTON_CONFIG_H 