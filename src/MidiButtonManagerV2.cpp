//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiButtonManagerV2.h"
#include "Logger.h"
#include <functional>

MidiButtonManagerV2 midiButtonManagerV2;

MidiButtonManagerV2::MidiButtonManagerV2() {
    // Set up the callback from processor to this manager
    processor.setButtonPressCallback(
        std::bind(&MidiButtonManagerV2::onButtonPress, this, 
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
}

void MidiButtonManagerV2::setup() {
    // Initialize the configuration system
    MidiButtonConfig::Config::initialize();
    
    // Setup the processor (actions don't need setup in simplified version)
    processor.setup();
    
    // Load default configuration
    loadButtonConfiguration("basic");
    
    logger.info("MidiButtonManagerV2 setup complete with %d configured buttons", 
                getConfiguredButtonCount());
}

void MidiButtonManagerV2::update() {
    // Update the processor to handle pending button presses
    processor.update();
}

void MidiButtonManagerV2::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    // Validate input
    if (!isValidChannel(channel) || !isValidNote(note)) {
        return;
    }
    
    // Delegate to processor
    processor.handleMidiNote(channel, note, velocity, isNoteOn);
}

void MidiButtonManagerV2::onButtonPress(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType) {
    // Find the button configuration
    const MidiButtonConfig::ButtonConfig* config = 
        MidiButtonConfig::Config::findButtonConfig(note, channel);
    
    if (config == nullptr) {
        logger.debug("No configuration found for button: Ch%d Note%d", channel, note);
        return;
    }
    
    logger.info("Button press: %s (%s)", config->description, 
                pressType == MidiButtonConfig::PressType::SHORT_PRESS ? "short" :
                pressType == MidiButtonConfig::PressType::LONG_PRESS ? "long" :
                pressType == MidiButtonConfig::PressType::DOUBLE_PRESS ? "double" : "triple");
    
    // Execute the action
    // Determine which action to execute based on press type
    MidiButtonConfig::ActionType action = MidiButtonConfig::ActionType::NONE;
    
    switch (pressType) {
        case MidiButtonConfig::PressType::SHORT_PRESS:
            action = config->shortPressAction;
            break;
        case MidiButtonConfig::PressType::LONG_PRESS:
            action = config->longPressAction;
            break;
        case MidiButtonConfig::PressType::DOUBLE_PRESS:
            action = config->doublePressAction;
            break;
        case MidiButtonConfig::PressType::TRIPLE_PRESS:
            action = config->triplePressAction;
            break;
    }
    
    // Execute the action with the configured parameter
    actions.executeAction(action, config->parameter);
}

void MidiButtonManagerV2::loadButtonConfiguration(const char* configName) {
    if (strcmp(configName, "basic") == 0) {
        MidiButtonConfig::Config::loadBasicConfiguration();
    } else if (strcmp(configName, "extended") == 0) {
        MidiButtonConfig::Config::loadExtendedConfiguration();
    } else if (strcmp(configName, "full") == 0) {
        MidiButtonConfig::Config::loadFullConfiguration();
    } else {
        logger.warning("Unknown configuration: %s, loading basic", configName);
        MidiButtonConfig::Config::loadBasicConfiguration();
    }
    
    logger.info("Loaded button configuration: %s (%d buttons)", 
                configName, getConfiguredButtonCount());
}

void MidiButtonManagerV2::addCustomButton(uint8_t note, uint8_t channel, const char* description,
                                         MidiButtonConfig::ActionType shortAction,
                                         MidiButtonConfig::ActionType longAction) {
    MidiButtonConfig::ButtonConfig config(note, channel, description);
    config.onShortPress(shortAction)
          .onLongPress(longAction);
    
    MidiButtonConfig::Config::addButton(config);
    logger.info("Added custom button: %s (note %d, channel %d)", description, note, channel);
}

bool MidiButtonManagerV2::isButtonPressed(uint8_t note, uint8_t channel) const {
    return processor.isButtonPressed(note, channel);
}

uint32_t MidiButtonManagerV2::getButtonPressStartTime(uint8_t note, uint8_t channel) const {
    return processor.getButtonPressStartTime(note, channel);
}

void MidiButtonManagerV2::printButtonConfiguration() const {
    const auto& configs = MidiButtonConfig::Config::getButtonConfigs();
    
    logger.info("Button Configuration (%d buttons):", configs.size());
    logger.info("Note  Ch  Description                 Short Press      Long Press       Double Press     Triple Press");
    logger.info("----  --  --------------------------  ---------------  ---------------  ---------------  ---------------");
    
    for (const auto& config : configs) {
        const char* shortAction = "None";
        const char* longAction = "None";
        const char* doubleAction = "None";
        const char* tripleAction = "None";
        
        // Convert action types to strings (simplified)
        auto actionToString = [](MidiButtonConfig::ActionType action) -> const char* {
            switch (action) {
                case MidiButtonConfig::ActionType::TOGGLE_RECORD: return "Record";
                case MidiButtonConfig::ActionType::TOGGLE_PLAY: return "Play";
                case MidiButtonConfig::ActionType::MOVE_CURRENT_TICK: return "Move Tick";
                case MidiButtonConfig::ActionType::SELECT_TRACK: return "Select Track";
                case MidiButtonConfig::ActionType::UNDO: return "Undo";
                case MidiButtonConfig::ActionType::REDO: return "Redo";
                case MidiButtonConfig::ActionType::ENTER_EDIT_MODE: return "Enter Edit";
                case MidiButtonConfig::ActionType::EXIT_EDIT_MODE: return "Exit Edit";
                case MidiButtonConfig::ActionType::CYCLE_EDIT_MODE: return "Cycle Edit";
                case MidiButtonConfig::ActionType::DELETE_NOTE: return "Delete Note";
                case MidiButtonConfig::ActionType::COPY_NOTE: return "Copy Note";
                case MidiButtonConfig::ActionType::PASTE_NOTE: return "Paste Note";
                case MidiButtonConfig::ActionType::CUSTOM_ACTION: return "Custom";
                default: return "None";
            }
        };
        
        shortAction = actionToString(config.shortPressAction);
        longAction = actionToString(config.longPressAction);
        doubleAction = actionToString(config.doublePressAction);
        tripleAction = actionToString(config.triplePressAction);
        
        logger.info("%-4d  %-2d  %-26s  %-15s  %-15s  %-15s  %-15s",
                   config.note, config.channel, config.description,
                   shortAction, longAction, doubleAction, tripleAction);
    }
}

uint32_t MidiButtonManagerV2::getConfiguredButtonCount() const {
    return MidiButtonConfig::Config::getButtonConfigs().size();
}

bool MidiButtonManagerV2::isValidChannel(uint8_t channel) const {
    return channel >= 1 && channel <= 16;
}

bool MidiButtonManagerV2::isValidNote(uint8_t note) const {
    return note <= 127;
} 