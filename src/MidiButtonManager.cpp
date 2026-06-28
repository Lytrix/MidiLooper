//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiButtonManager.h"
#include "Logger.h"
#include "DisplayManager.h"
#include "Utils/PressTiming.h"
#include "MidiConfig.h"
#include "LooperState.h"
#include <functional>

MidiButtonManager midiButtonManager;

MidiButtonManager::MidiButtonManager() {
    // Set up the callback from processor to this manager
    processor.setButtonPressCallback(
        std::bind(&MidiButtonManager::onButtonPress, this, 
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
}

void MidiButtonManager::setup() {
    // Initialize config (loads DROID button configuration)
    MidiButtonConfig::Config::initialize();
    
    // Setup the processor after config is loaded
    processor.setup();
    
    logger.info("MidiButtonManager setup complete with %d configured buttons", 
                getConfiguredButtonCount());
}

void MidiButtonManager::update() {
    // Update the processor to handle pending button presses
    processor.update();
    updateLoopHoldLayering();
}

void MidiButtonManager::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    // Validate input
    if (!isValidChannel(channel) || !isValidNote(note)) {
        return;
    }
    
    // Delegate to processor
    processor.handleMidiNote(channel, note, velocity, isNoteOn);
}

void MidiButtonManager::onButtonPress(uint8_t note, uint8_t channel, MidiButtonConfig::PressType pressType) {
    // Find the button configuration
    const MidiButtonConfig::ButtonConfig* config = 
        MidiButtonConfig::Config::findButtonConfig(note, channel);
    
    if (config == nullptr) {
        logger.debug("No configuration found for button: Ch%d Note%d", channel, note);
        return;
    }

    if (looperState.isLoadSaveModeActive() &&
        config->channel == MidiConfig::Channels::SELECT &&
        config->note == MidiConfig::LengthEdit::NOTE) {
        using PressType = MidiButtonConfig::PressType;
        using OverlayPress = DisplayManager::LoadSaveOverlayPressType;
        if (pressType == PressType::LONG_PRESS) {
            displayManager.handleLoadSaveOverlayPress(OverlayPress::Long);
            return;
        }
        if (pressType == PressType::DOUBLE_PRESS) {
            displayManager.handleLoadSaveOverlayPress(OverlayPress::Double);
            return;
        }
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

void MidiButtonManager::addCustomButton(uint8_t note, uint8_t channel, const char* description,
                                         MidiButtonConfig::ActionType shortAction,
                                         MidiButtonConfig::ActionType longAction) {
    MidiButtonConfig::ButtonConfig config(note, channel, description);
    config.onShortPress(shortAction)
          .onLongPress(longAction);
    
    MidiButtonConfig::Config::addButton(config);
    logger.info("Added custom button: %s (note %d, channel %d)", description, note, channel);
}

bool MidiButtonManager::isButtonPressed(uint8_t note, uint8_t channel) const {
    return processor.isButtonPressed(note, channel);
}

uint32_t MidiButtonManager::getButtonPressStartTime(uint8_t note, uint8_t channel) const {
    return processor.getButtonPressStartTime(note, channel);
}

void MidiButtonManager::printButtonConfiguration() const {
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
                case MidiButtonConfig::ActionType::CREATE_NOTE_AT_BRACKET: return "Create Note At Bracket";
                case MidiButtonConfig::ActionType::DELETE_OR_CREATE_NOTE: return "Delete Or Create Note";
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

uint32_t MidiButtonManager::getConfiguredButtonCount() const {
    return MidiButtonConfig::Config::getButtonConfigs().size();
}

bool MidiButtonManager::isValidChannel(uint8_t channel) const {
    return channel >= 1 && channel <= 16;
}

bool MidiButtonManager::isValidNote(uint8_t note) const {
    return note <= 127;
}

void MidiButtonManager::updateLoopHoldLayering() {
    if (looperState.isLoadSaveModeActive()) {
        for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
            loopButtonHeld[slot] = false;
        }
        return;
    }

    constexpr uint8_t ch = MidiButtonConfig::Channels::TRACK_SELECT;
    constexpr uint8_t baseNote = MidiConfig::Led::LOOP_SELECT_LED_BASE;
    uint32_t defaultLongPressMs = PressTiming::LONG_PRESS_TIME;
    uint32_t doubleTap = 0, tripleTap = 0;
    MidiButtonConfig::Config::getTimingConfig(doubleTap, tripleTap, defaultLongPressMs);

    const uint32_t now = millis();
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        const uint8_t note = baseNote + slot;
        const auto* cfg = MidiButtonConfig::Config::findButtonConfig(note, ch - 1);
        uint32_t longPressMs = defaultLongPressMs;
        if (cfg && cfg->longPressTime > 0) {
            longPressMs = cfg->longPressTime;
        }
        // Arm multi-slot layering only after long-press threshold so the same hold can still be a long-press clear on release.
        const uint32_t layerArmAfterMs = longPressMs + 50;

        bool pressed = processor.isButtonPressed(note, ch);
        if (pressed) {
            uint32_t start = processor.getButtonPressStartTime(note, ch);
            uint32_t heldMs;
            if (now >= start) {
                heldMs = now - start;
            } else {
                heldMs = (0xFFFFFFFFu - start + 1u) + now;
            }
            if (heldMs >= layerArmAfterMs && !loopButtonHeld[slot]) {
                loopButtonHeld[slot] = true;
                actions.beginSlotLayerHold(slot);
            }
        } else if (loopButtonHeld[slot]) {
            loopButtonHeld[slot] = false;
            actions.endSlotLayerHold(slot);
        }
    }
}
