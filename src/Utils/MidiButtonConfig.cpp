//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MidiButtonConfig.h"
#include "Logger.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "EditManager.h"
#include "TrackUndo.h"
#include <string>

namespace MidiButtonConfig {

// Static member definitions
std::vector<ButtonConfig> Config::buttonConfigs;
bool Config::initialized = false;

void Config::initialize() {
    if (initialized) return;
    
    clearConfigs();
    loadBasicConfiguration(); // Start with basic config
    initialized = true;
    
    logger.info("MidiButtonConfig initialized with %d button configurations", buttonConfigs.size());
}

void Config::addButton(const ButtonConfig& config) {
    // Check for duplicate mappings
    for (const auto& existing : buttonConfigs) {
        if (existing.note == config.note && existing.channel == config.channel) {
            logger.warning("Duplicate MIDI button mapping: note %d channel %d", config.note, config.channel);
            return;
        }
    }
    
    buttonConfigs.push_back(config);
    logger.debug("Added button config: %s (note %d, channel %d)", config.description, config.note, config.channel);
}

const std::vector<ButtonConfig>& Config::getButtonConfigs() {
    return buttonConfigs;
}

const ButtonConfig* Config::findButtonConfig(uint8_t note, uint8_t channel) {
    // Convert 0-based channel to 1-based for configuration lookup
    uint8_t configChannel = channel + 1;
    
    for (const auto& config : buttonConfigs) {
        if (config.note == note && config.channel == configChannel) {
            return &config;
        }
    }
    return nullptr;
}

void Config::clearConfigs() {
    buttonConfigs.clear();
}

void Config::getTimingConfig(uint32_t& doubleTapWindow, uint32_t& tripleTapWindow, uint32_t& longPressTime) {
    if (buttonConfigs.empty()) {
        // Default values if no configs loaded
        doubleTapWindow = 300;
        tripleTapWindow = 400;
        longPressTime = 600;
    } else {
        // Use timing from first button config as default
        const auto& firstConfig = buttonConfigs[0];
        doubleTapWindow = firstConfig.doubleTapWindow;
        tripleTapWindow = firstConfig.tripleTapWindow;
        longPressTime = firstConfig.longPressTime;
    }
}

// Convenience methods for common configurations
void Config::addRecordButton(uint8_t note, uint8_t channel) {
    ButtonConfig config(note, channel, "Record Toggle");
    config.onShortPress(ActionType::TOGGLE_RECORD);
    addButton(config);
}

void Config::addPlayButton(uint8_t note, uint8_t channel) {
    ButtonConfig config(note, channel, "Play Toggle");
    config.onShortPress(ActionType::TOGGLE_PLAY);
    addButton(config);
}

void Config::addTrackSelectButton(uint8_t note, uint8_t trackNumber, uint8_t channel) {
    ButtonConfig config(note, channel, ("Track " + std::to_string(trackNumber)).c_str());
    config.onShortPress(ActionType::SELECT_TRACK)
          .withParameter(trackNumber);
    addButton(config);
}

void Config::addTickMoveButton(uint8_t note, int32_t tickOffset, uint8_t channel) {
    const char* desc = tickOffset > 0 ? "Move Forward" : "Move Backward";
    ButtonConfig config(note, channel, desc);
    config.onShortPress(ActionType::MOVE_CURRENT_TICK)
          .withParameter(tickOffset);
    addButton(config);
}

void Config::addEditModeButton(uint8_t note, uint8_t channel) {
    ButtonConfig config(note, channel, "Edit Mode");
    config.onShortPress(ActionType::ENTER_EDIT_MODE)
          .onLongPress(ActionType::CYCLE_EDIT_MODE)
          .onDoublePress(ActionType::EXIT_EDIT_MODE);
    addButton(config);
}

void Config::addUndoRedoButton(uint8_t note, uint8_t channel) {
    ButtonConfig config(note, channel, "Undo/Redo");
    config.onShortPress(ActionType::UNDO)
          .onLongPress(ActionType::REDO);
    addButton(config);
}

// Preset configurations
void Config::loadBasicConfiguration() {
    clearConfigs();
    
    // Core transport controls
    addButton(ButtonConfig(36, Channels::MAIN_BUTTONS, "Record")   // C2
              .onShortPress(ActionType::TOGGLE_RECORD));
    
    addButton(ButtonConfig(37, Channels::MAIN_BUTTONS, "Play")     // C#2
              .onShortPress(ActionType::TOGGLE_PLAY));
    
    addButton(ButtonConfig(38, Channels::MAIN_BUTTONS, "Edit Mode")   // D2
              .onShortPress(ActionType::ENTER_EDIT_MODE)
              .onLongPress(ActionType::CYCLE_EDIT_MODE)
              .onDoublePress(ActionType::EXIT_EDIT_MODE));
    
    addButton(ButtonConfig(39, Channels::MAIN_BUTTONS, "Undo/Redo")   // D#2
              .onShortPress(ActionType::UNDO)
              .onLongPress(ActionType::REDO));
    
    logger.info("Loaded basic button configuration (4 buttons)");
}

void Config::loadExtendedConfiguration() {
    clearConfigs();
    
    // Load basic first
    loadBasicConfiguration();
    
    // Add track selection buttons (8 tracks, C3-B3)
    for (int i = 0; i < 8; i++) {
        addButton(ButtonConfig(48 + i, Channels::TRACK_SELECT, ("Track " + std::to_string(i + 1)).c_str())
                  .onShortPress(ActionType::SELECT_TRACK)
                  .onLongPress(ActionType::MUTE_TRACK)
                  .onDoublePress(ActionType::SOLO_TRACK)
                  .withParameter(i));
    }
    
    // Add navigation buttons
    addButton(ButtonConfig(40, Channels::MAIN_BUTTONS, "Move Back")   // E2
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-96)); // Move back 1 beat
    
    addButton(ButtonConfig(41, Channels::MAIN_BUTTONS, "Move Forward")   // F2
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(96)); // Move forward 1 beat
    
    // Add edit functions
    addButton(ButtonConfig(42, Channels::EDIT_FUNCTIONS, "Delete Note")   // F#2
              .onShortPress(ActionType::DELETE_NOTE));
    
    addButton(ButtonConfig(43, Channels::EDIT_FUNCTIONS, "Copy/Paste")   // G2
              .onShortPress(ActionType::COPY_NOTE)
              .onLongPress(ActionType::PASTE_NOTE));
    
    // Add momentary length edit button (note 3, special per DROID)
    addButton(ButtonConfig(3, Channels::EDIT_FUNCTIONS, "Length Edit Mode")
              .onShortPress(ActionType::TOGGLE_LENGTH_EDIT_MODE)
              .asMomentary(true)
              .withTiming(300, 400, 600));
    
    logger.info("Loaded extended button configuration (16 buttons)");
}

void Config::loadFullConfiguration() {
    clearConfigs();
    
    // === PRESERVE EXISTING 3-BUTTON BEHAVIOR ===
    // These match MidiButtonManager exactly on Channel 16
    
    // Button A - Record/Overdub
    addButton(ButtonConfig(36, 16, "Record/Overdub")   // C2
              .onShortPress(ActionType::TOGGLE_RECORD)     // Record/Overdub/Stop
              .onDoublePress(ActionType::UNDO)             // Undo
              .onTriplePress(ActionType::REDO)             // Redo  
              .onLongPress(ActionType::CLEAR_TRACK));      // Clear Track
    
    // Button B - Track Switch
    addButton(ButtonConfig(37, 16, "Track Switch")     // C#2
              .onShortPress(ActionType::SELECT_TRACK)      // Switch to next track
              .onDoublePress(ActionType::UNDO_CLEAR_TRACK) // Undo clear track
              .onTriplePress(ActionType::REDO_CLEAR_TRACK) // Redo clear track
              .onLongPress(ActionType::MUTE_TRACK)         // Toggle mute
              .withParameter(255));                        // Special parameter for "next track" and "current track mute"
    
    // Encoder Button - Edit Mode
    addButton(ButtonConfig(38, 16, "Edit Mode")        // D2
              .onShortPress(ActionType::CYCLE_EDIT_MODE)   // Cycle edit modes
              .onDoublePress(ActionType::DELETE_NOTE)      // Delete selected note
              .onLongPress(ActionType::EXIT_EDIT_MODE));   // Exit edit mode
    
    // Length Editing Mode Toggle (note 3, special per DROID)
    addButton(ButtonConfig(3, 16, "Length Edit Mode")
              .onShortPress(ActionType::TOGGLE_LENGTH_EDIT_MODE));

    // Global Transport Start/Stop
    addButton(ButtonConfig(39, 16, "Global Transport")  // D#2
              .onShortPress(ActionType::TOGGLE_TRANSPORT)
              .onDoublePress(ActionType::RESET_TO_LOOP_START)
              .withDebounce(50)   // Lower than double-tap window so second tap can register
              .withTiming(300, 400, 3000));
    
    // === EXTEND WITH 37 MORE BUTTONS FOR 40 TOTAL ===
    
    // Transport Controls (Channel 1, D#2-B2) - 8 buttons
    addButton(ButtonConfig(39, 1, "Play/Stop")         // D#2
              .onShortPress(ActionType::TOGGLE_PLAY));
    
    addButton(ButtonConfig(40, 1, "Set Loop Start")    // E2
              .onShortPress(ActionType::SET_LOOP_START));
    
    addButton(ButtonConfig(41, 1, "Set Loop End")      // F2
              .onShortPress(ActionType::SET_LOOP_END));
    
    addButton(ButtonConfig(42, 1, "Quantize")          // F#2
              .onShortPress(ActionType::QUANTIZE));
    
    addButton(ButtonConfig(43, 1, "Copy Note")         // G2
              .onShortPress(ActionType::COPY_NOTE)
              .onLongPress(ActionType::PASTE_NOTE));
    
    addButton(ButtonConfig(44, 1, "Move Back Beat")    // G#2
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-96)); // Back 1 beat
    
    addButton(ButtonConfig(45, 1, "Move Forward Beat") // A2
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(96)); // Forward 1 beat
    
    addButton(ButtonConfig(46, 1, "Move Back 16th")    // A#2
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-24)); // Back 1/16 beat
    
    // Track Selection (Channel 2, C3-B4) - 16 buttons for 16 tracks
    for (int i = 0; i < 16; i++) {
        addButton(ButtonConfig(48 + i, 2, ("Track " + std::to_string(i + 1)).c_str())
                  .onShortPress(ActionType::SELECT_TRACK)
                  .onLongPress(ActionType::MUTE_TRACK)
                  .onDoublePress(ActionType::SOLO_TRACK)
                  .withParameter(i));
    }
    
    // Navigation with different step sizes (Channel 1, E4-D#5) - 12 buttons
    struct NavButton {
        uint8_t note;
        const char* name;
        int32_t tickOffset;
    };
    
    NavButton navButtons[] = {
        {64, "Back 32nd", -6},      // E4
        {65, "Forward 32nd", 6},    // F4
        {66, "Back 16th", -24},     // F#4
        {67, "Forward 16th", 24},   // G4
        {68, "Back Beat", -96},     // G#4
        {69, "Forward Beat", 96},   // A4
        {70, "Back Bar", -384},     // A#4
        {71, "Forward Bar", 384},   // B4
        {72, "Back 2 Bars", -768},  // C5
        {73, "Forward 2 Bars", 768},// C#5
        {74, "Back 4 Bars", -1536}, // D5
        {75, "Forward 4 Bars", 1536}// D#5
    };
    
    for (const auto& nav : navButtons) {
        addButton(ButtonConfig(nav.note, 1, nav.name)
                  .onShortPress(ActionType::MOVE_CURRENT_TICK)
                  .withParameter(nav.tickOffset));
    }
    
    logger.info("Loaded full button configuration (40 buttons) - preserves existing 3-button behavior");
}

} // namespace MidiButtonConfig 