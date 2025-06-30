//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file ButtonConfiguration40Example.cpp
 * @brief Example showing how to configure 40 MIDI buttons using the modular system
 * 
 * This example demonstrates:
 * 1. Using preset configurations (basic, extended, full)
 * 2. Adding custom button configurations
 * 3. Using different channels for logical grouping
 * 4. Custom actions with lambda functions
 * 5. Different press types (short, long, double, triple)
 */

#include "MidiButtonManagerV2.h"
#include "Utils/MidiButtonConfig.h"
#include "Logger.h"
#include <string>

// Example of setting up a custom 40-button configuration
void setupCustom40ButtonConfiguration() {
    using namespace MidiButtonConfig;
    
    // Clear any existing configuration
    Config::clearConfigs();
    
    logger.info("Setting up custom 40-button configuration...");
    
    // === TRANSPORT CONTROLS (Channel 1, Notes 36-43) ===
    Config::addButton(ButtonConfig(Notes::C2, Channels::TRANSPORT, "Record")
        .onShortPress(ActionType::TOGGLE_RECORD)
        .onLongPress(ActionType::CLEAR_TRACK));
    
    Config::addButton(ButtonConfig(Notes::C2_SHARP, Channels::TRANSPORT, "Play/Stop")
        .onShortPress(ActionType::TOGGLE_PLAY));
    
    Config::addButton(ButtonConfig(Notes::D2, Channels::TRANSPORT, "Loop Start")
        .onShortPress(ActionType::SET_LOOP_START));
    
    Config::addButton(ButtonConfig(Notes::D2_SHARP, Channels::TRANSPORT, "Loop End")
        .onShortPress(ActionType::SET_LOOP_END));
    
    Config::addButton(ButtonConfig(Notes::E2, Channels::TRANSPORT, "Undo/Redo")
        .onShortPress(ActionType::UNDO)
        .onLongPress(ActionType::REDO));
    
    Config::addButton(ButtonConfig(Notes::F2, Channels::TRANSPORT, "Edit Mode")
        .onShortPress(ActionType::ENTER_EDIT_MODE)
        .onLongPress(ActionType::CYCLE_EDIT_MODE)
        .onDoublePress(ActionType::EXIT_EDIT_MODE));
    
    Config::addButton(ButtonConfig(Notes::F2_SHARP, Channels::TRANSPORT, "Quantize")
        .onShortPress(ActionType::QUANTIZE));
    
    Config::addButton(ButtonConfig(Notes::G2, Channels::TRANSPORT, "Copy/Paste")
        .onShortPress(ActionType::COPY_NOTE)
        .onLongPress(ActionType::PASTE_NOTE));
    
    // === TRACK SELECTION (Channel 2, Notes 48-63) ===
    // 16 tracks with multi-function buttons
    for (int i = 0; i < 16; i++) {
        Config::addButton(ButtonConfig(Notes::C3 + i, Channels::TRACK_SELECT, 
                                     ("Track " + std::to_string(i + 1)).c_str())
            .onShortPress(ActionType::SELECT_TRACK)      // Select track
            .onLongPress(ActionType::MUTE_TRACK)         // Mute/unmute
            .onDoublePress(ActionType::SOLO_TRACK)       // Solo
            .withParameter(i));
    }
    
    // === NAVIGATION CONTROLS (Channel 1, Notes 44-51) ===
    // Different step sizes for precise navigation
    struct NavigationButton {
        uint8_t note;
        const char* name;
        int32_t tickOffset;
    };
    
    NavigationButton navButtons[] = {
        {Notes::G2_SHARP, "Back 32nd", -6},     // 32nd note back
        {Notes::A2, "Forward 32nd", 6},         // 32nd note forward
        {Notes::A2_SHARP, "Back 16th", -24},    // 16th note back
        {Notes::B2, "Forward 16th", 24},        // 16th note forward
        {44, "Back Beat", -96},                 // Beat back
        {45, "Forward Beat", 96},               // Beat forward
        {46, "Back Bar", -384},                 // Bar back
        {47, "Forward Bar", 384}                // Bar forward
    };
    
    for (const auto& nav : navButtons) {
        Config::addButton(ButtonConfig(nav.note, Channels::MAIN_BUTTONS, nav.name)
            .onShortPress(ActionType::MOVE_CURRENT_TICK)
            .withParameter(nav.tickOffset));
    }
    
    // === EDIT FUNCTIONS (Channel 3, Notes 64-71) ===
    Config::addButton(ButtonConfig(64, Channels::EDIT_FUNCTIONS, "Delete Note")
        .onShortPress(ActionType::DELETE_NOTE));
    
    Config::addButton(ButtonConfig(65, Channels::EDIT_FUNCTIONS, "Copy Note")
        .onShortPress(ActionType::COPY_NOTE));
    
    Config::addButton(ButtonConfig(66, Channels::EDIT_FUNCTIONS, "Paste Note")
        .onShortPress(ActionType::PASTE_NOTE));
    
    // Custom actions using lambda functions
    Config::addButton(ButtonConfig(67, Channels::EDIT_FUNCTIONS, "Jump to Start")
        .withCustomAction([](Track& track, uint32_t currentTick) {
            clockManager.setCurrentTick(0);
            logger.info("Jumped to track start");
        }));
    
    Config::addButton(ButtonConfig(68, Channels::EDIT_FUNCTIONS, "Jump to Loop Start")
        .withCustomAction([](Track& track, uint32_t currentTick) {
            clockManager.setCurrentTick(track.getLoopStartTick());
            logger.info("Jumped to loop start");
        }));
    
    Config::addButton(ButtonConfig(69, Channels::EDIT_FUNCTIONS, "Jump to Loop End")
        .withCustomAction([](Track& track, uint32_t currentTick) {
            clockManager.setCurrentTick(track.getLoopEndTick());
            logger.info("Jumped to loop end");
        }));
    
    Config::addButton(ButtonConfig(70, Channels::EDIT_FUNCTIONS, "Halve Loop")
        .withCustomAction([](Track& track, uint32_t currentTick) {
            uint32_t loopLength = track.getLoopLengthTicks();
            track.setLoopEndTick(track.getLoopStartTick() + loopLength / 2);
            logger.info("Loop length halved");
        }));
    
    Config::addButton(ButtonConfig(71, Channels::EDIT_FUNCTIONS, "Double Loop")
        .withCustomAction([](Track& track, uint32_t currentTick) {
            uint32_t loopLength = track.getLoopLengthTicks();
            track.setLoopEndTick(track.getLoopStartTick() + loopLength * 2);
            logger.info("Loop length doubled");
        }));
    
    logger.info("Custom 40-button configuration complete!");
}

// Example of using the convenience methods
void setupUsingConvenienceMethods() {
    using namespace MidiButtonConfig;
    
    Config::clearConfigs();
    
    // Use the convenience methods for common button types
    Config::addRecordButton(36, 1);         // C2 on channel 1
    Config::addPlayButton(37, 1);           // C#2 on channel 1
    Config::addEditModeButton(38, 1);       // D2 on channel 1
    Config::addUndoRedoButton(39, 1);       // D#2 on channel 1
    
    // Add track selection buttons
    for (int i = 0; i < 8; i++) {
        Config::addTrackSelectButton(48 + i, i, 2); // C3-G3 on channel 2
    }
    
    // Add navigation buttons
    Config::addTickMoveButton(40, -96, 1);   // E2: back 1 beat
    Config::addTickMoveButton(41, 96, 1);    // F2: forward 1 beat
    Config::addTickMoveButton(42, -24, 1);   // F#2: back 1/16
    Config::addTickMoveButton(43, 24, 1);    // G2: forward 1/16
}

// Example showing how to dynamically add buttons based on hardware
void setupDynamicConfiguration(int numTracks, bool hasEditButtons, bool hasNavButtons) {
    using namespace MidiButtonConfig;
    
    Config::clearConfigs();
    
    // Always add basic transport
    Config::addRecordButton(36, 1);
    Config::addPlayButton(37, 1);
    Config::addUndoRedoButton(38, 1);
    
    // Add track buttons based on hardware
    for (int i = 0; i < numTracks && i < 16; i++) {
        Config::addTrackSelectButton(48 + i, i, 2);
    }
    
    // Conditionally add edit buttons
    if (hasEditButtons) {
        Config::addEditModeButton(39, 1);
        Config::addButton(ButtonConfig(64, 3, "Delete")
            .onShortPress(ActionType::DELETE_NOTE));
        Config::addButton(ButtonConfig(65, 3, "Copy/Paste")
            .onShortPress(ActionType::COPY_NOTE)
            .onLongPress(ActionType::PASTE_NOTE));
    }
    
    // Conditionally add navigation
    if (hasNavButtons) {
        Config::addTickMoveButton(40, -96, 1);   // Beat back
        Config::addTickMoveButton(41, 96, 1);    // Beat forward
        Config::addTickMoveButton(42, -24, 1);   // 16th back
        Config::addTickMoveButton(43, 24, 1);    // 16th forward
    }
    
    logger.info("Dynamic configuration: %d tracks, edit=%s, nav=%s", 
                numTracks, hasEditButtons ? "yes" : "no", hasNavButtons ? "yes" : "no");
}

// Example of how to integrate this into your main code
void setupMidiButtons() {
    // Initialize the manager
    midiButtonManagerV2.setup();
    
    // Choose your configuration approach:
    
    // Option 1: Use built-in presets
    // midiButtonManagerV2.loadButtonConfiguration("full");  // Loads 40 buttons
    
    // Option 2: Use custom configuration
    setupCustom40ButtonConfiguration();
    
    // Option 3: Use convenience methods
    // setupUsingConvenienceMethods();
    
    // Option 4: Dynamic configuration based on hardware
    // setupDynamicConfiguration(8, true, true);
    
    // Print the configuration for debugging
    midiButtonManagerV2.printButtonConfiguration();
    
    logger.info("MIDI Button system ready with %d configured buttons", 
                midiButtonManagerV2.getConfiguredButtonCount());
}

// Example of how to add buttons at runtime
void addRuntimeButton() {
    // You can add buttons dynamically
    midiButtonManagerV2.addCustomButton(
        72, // Note C5
        4,  // Channel 4
        "Runtime Button",
        MidiButtonConfig::ActionType::TOGGLE_RECORD,  // Short press
        MidiButtonConfig::ActionType::CLEAR_TRACK     // Long press
    );
}

// In your main loop, just call:
// midiButtonManagerV2.update();

// In your MIDI handler, call:
// midiButtonManagerV2.handleMidiNote(channel, note, velocity, isNoteOn); 