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
    addButton(ButtonConfig(Notes::C2, Channels::MAIN_BUTTONS, "Record")
              .onShortPress(ActionType::TOGGLE_RECORD));
    
    addButton(ButtonConfig(Notes::C2_SHARP, Channels::MAIN_BUTTONS, "Play")
              .onShortPress(ActionType::TOGGLE_PLAY));
    
    addButton(ButtonConfig(Notes::D2, Channels::MAIN_BUTTONS, "Edit Mode")
              .onShortPress(ActionType::ENTER_EDIT_MODE)
              .onLongPress(ActionType::CYCLE_EDIT_MODE)
              .onDoublePress(ActionType::EXIT_EDIT_MODE));
    
    addButton(ButtonConfig(Notes::D2_SHARP, Channels::MAIN_BUTTONS, "Undo/Redo")
              .onShortPress(ActionType::UNDO)
              .onLongPress(ActionType::REDO));
    
    logger.info("Loaded basic button configuration (4 buttons)");
}

void Config::loadExtendedConfiguration() {
    clearConfigs();
    
    // Load basic first
    loadBasicConfiguration();
    
    // Add track selection buttons (8 tracks)
    for (int i = 0; i < 8; i++) {
        addButton(ButtonConfig(Notes::C3 + i, Channels::TRACK_SELECT, ("Track " + std::to_string(i + 1)).c_str())
                  .onShortPress(ActionType::SELECT_TRACK)
                  .onLongPress(ActionType::MUTE_TRACK)
                  .onDoublePress(ActionType::SOLO_TRACK)
                  .withParameter(i));
    }
    
    // Add navigation buttons
    addButton(ButtonConfig(Notes::E2, Channels::MAIN_BUTTONS, "Move Back")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-96)); // Move back 1 beat
    
    addButton(ButtonConfig(Notes::F2, Channels::MAIN_BUTTONS, "Move Forward")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(96)); // Move forward 1 beat
    
    // Add edit functions
    addButton(ButtonConfig(Notes::F2_SHARP, Channels::EDIT_FUNCTIONS, "Delete Note")
              .onShortPress(ActionType::DELETE_NOTE));
    
    addButton(ButtonConfig(Notes::G2, Channels::EDIT_FUNCTIONS, "Copy/Paste")
              .onShortPress(ActionType::COPY_NOTE)
              .onLongPress(ActionType::PASTE_NOTE));
    
    logger.info("Loaded extended button configuration (16 buttons)");
}

void Config::loadFullConfiguration() {
    clearConfigs();
    
    // === PRESERVE EXISTING 3-BUTTON BEHAVIOR ===
    // These match your current MidiButtonManager exactly on Channel 16
    
    // Button A - Record/Overdub button (C2/36)
    addButton(ButtonConfig(Notes::C2, 16, "Record/Overdub")
              .onShortPress(ActionType::TOGGLE_RECORD)     // Record/Overdub/Stop
              .onDoublePress(ActionType::UNDO)             // Undo
              .onTriplePress(ActionType::REDO)             // Redo  
              .onLongPress(ActionType::CLEAR_TRACK));      // Clear Track
    
    // Button B - Track Switch button (C#2/37)
    addButton(ButtonConfig(Notes::C2_SHARP, 16, "Track Switch")
              .onShortPress(ActionType::SELECT_TRACK)      // Switch to next track
              .onDoublePress(ActionType::UNDO_CLEAR_TRACK) // Undo clear track
              .onTriplePress(ActionType::REDO_CLEAR_TRACK) // Redo clear track
              .onLongPress(ActionType::MUTE_TRACK)         // Toggle mute
              .withParameter(255));                        // Special parameter for "next track" and "current track mute"
    
    // Encoder Button - Edit Mode button (D2/38)  
    addButton(ButtonConfig(Notes::D2, 16, "Edit Mode")
              .onShortPress(ActionType::CYCLE_EDIT_MODE)   // Cycle edit modes
              .onDoublePress(ActionType::DELETE_NOTE)      // Delete selected note
              .onLongPress(ActionType::EXIT_EDIT_MODE));   // Exit edit mode
    
    // === EXTEND WITH 37 MORE BUTTONS FOR 40 TOTAL ===
    
    // Transport Controls (Channel 1, Notes 39-46) - 8 buttons
    addButton(ButtonConfig(Notes::D2_SHARP, 1, "Play/Stop")
              .onShortPress(ActionType::TOGGLE_PLAY));
    
    addButton(ButtonConfig(Notes::E2, 1, "Set Loop Start")
              .onShortPress(ActionType::SET_LOOP_START));
    
    addButton(ButtonConfig(Notes::F2, 1, "Set Loop End")
              .onShortPress(ActionType::SET_LOOP_END));
    
    addButton(ButtonConfig(Notes::F2_SHARP, 1, "Quantize")
              .onShortPress(ActionType::QUANTIZE));
    
    addButton(ButtonConfig(Notes::G2, 1, "Copy Note")
              .onShortPress(ActionType::COPY_NOTE)
              .onLongPress(ActionType::PASTE_NOTE));
    
    addButton(ButtonConfig(Notes::G2_SHARP, 1, "Move Back Beat")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-96)); // Back 1 beat
    
    addButton(ButtonConfig(Notes::A2, 1, "Move Forward Beat")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(96)); // Forward 1 beat
    
    addButton(ButtonConfig(Notes::A2_SHARP, 1, "Move Back 16th")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-24)); // Back 1/16 beat
    
    // Track Selection (Channel 2, Notes 48-63) - 16 buttons for 16 tracks
    for (int i = 0; i < 16; i++) {
        addButton(ButtonConfig(Notes::C3 + i, 2, ("Track " + std::to_string(i + 1)).c_str())
                  .onShortPress(ActionType::SELECT_TRACK)
                  .onLongPress(ActionType::MUTE_TRACK)
                  .onDoublePress(ActionType::SOLO_TRACK)
                  .withParameter(i));
    }
    
    // Navigation with different step sizes (Channel 1, Notes 64-75) - 12 buttons
    struct NavButton {
        uint8_t note;
        const char* name;
        int32_t tickOffset;
    };
    
    NavButton navButtons[] = {
        {64, "Back 32nd", -6},      // 32nd note back
        {65, "Forward 32nd", 6},    // 32nd note forward
        {66, "Back 16th", -24},     // 16th note back  
        {67, "Forward 16th", 24},   // 16th note forward
        {68, "Back Beat", -96},     // Beat back
        {69, "Forward Beat", 96},   // Beat forward
        {70, "Back Bar", -384},     // Bar back
        {71, "Forward Bar", 384},   // Bar forward
        {72, "Back 2 Bars", -768}, // 2 bars back
        {73, "Forward 2 Bars", 768}, // 2 bars forward
        {74, "Back 4 Bars", -1536}, // 4 bars back
        {75, "Forward 4 Bars", 1536} // 4 bars forward
    };
    
    for (const auto& nav : navButtons) {
        addButton(ButtonConfig(nav.note, 1, nav.name)
                  .onShortPress(ActionType::MOVE_CURRENT_TICK)
                  .withParameter(nav.tickOffset));
    }
    
    logger.info("Loaded full button configuration (40 buttons) - preserves existing 3-button behavior");
}

} // namespace MidiButtonConfig 