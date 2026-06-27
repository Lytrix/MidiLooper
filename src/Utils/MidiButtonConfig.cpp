//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MidiButtonConfig.h"
#include "Logger.h"
#include "Globals.h"
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
    loadConfiguration();
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

void Config::loadConfiguration() {
    clearConfigs();
    
    // === PRESERVE EXISTING 3-BUTTON BEHAVIOR ===
    // These match MidiButtonManager exactly on Channel 16
    
    constexpr uint8_t ch16 = MidiConfig::Channels::SELECT;
    
    // Button A - Record/Overdub
    addButton(ButtonConfig(MidiConfig::Transport::NOTE_RECORD, ch16, "Record/Overdub")
              .onShortPress(ActionType::TOGGLE_RECORD)
              .onDoublePress(ActionType::UNDO)
              .onTriplePress(ActionType::REDO)
              .onLongPress(ActionType::CLEAR_TRACK));
    
    // Button B - Track Switch
    addButton(ButtonConfig(MidiConfig::Transport::NOTE_PLAY, ch16, "Track Switch")
              .onShortPress(ActionType::SELECT_TRACK)
              .onDoublePress(ActionType::UNDO_CLEAR_TRACK)
              .onTriplePress(ActionType::REDO_CLEAR_TRACK)
              .onLongPress(ActionType::MUTE_TRACK)
              .withParameter(255));
    
    // Encoder Button - Edit Mode (mode cycle + exit only; add/delete on NOTELEN double)
    addButton(ButtonConfig(MidiConfig::Transport::NOTE_EDIT_MODE, ch16, "Edit Mode")
              .onShortPress(ActionType::CYCLE_EDIT_MODE)
              .onLongPress(ActionType::EXIT_EDIT_MODE));
    
    // NOTELEN (B2.32): position vs length toggle; double = delete selected or create at bracket
    addButton(ButtonConfig(MidiConfig::LengthEdit::NOTE, ch16, "Length Edit Mode")
              .onShortPress(ActionType::TOGGLE_LENGTH_EDIT_MODE)
              .onDoublePress(ActionType::DELETE_OR_CREATE_NOTE));

    // Global Transport Start/Stop
    addButton(ButtonConfig(MidiConfig::Transport::NOTE_REDO, ch16, "Global Transport")
              .onShortPress(ActionType::TOGGLE_TRANSPORT)
              .onDoublePress(ActionType::RESET_TO_LOOP_START)
              .withDebounce(50)   // Lower than double-tap window so second tap can register
              .withTiming(300, 400, 3000));
    
    // === EXTEND WITH TRANSPORT (all ch16, notes 40-47) ===
    
    constexpr int32_t tick16 = static_cast<int32_t>(::Config::TICKS_PER_16TH_STEP);
    
    // Extended transport (ch16; note 40 used for Play/Stop to avoid conflict with 39)
    // Double-press: load/save set browser. Long-press release: snap detailed window; hold tracks playhead.
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_PLAY_STOP, ch16, "Play/Stop")
              .onShortPress(ActionType::TOGGLE_PLAY)
              .onDoublePress(ActionType::TOGGLE_LOAD_SAVE_MODE)
              .onLongPress(ActionType::CENTER_DETAILED_WINDOW_ON_PLAYHEAD)
              .withDebounce(50));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_SET_LOOP_START, ch16, "Set Loop Start")
              .onShortPress(ActionType::SET_LOOP_START));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_SET_LOOP_END, ch16, "Set Loop End")
              .onShortPress(ActionType::SET_LOOP_END));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_QUANTIZE, ch16, "Quantize")
              .onShortPress(ActionType::QUANTIZE));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_COPY_PASTE, ch16, "Copy Note")
              .onShortPress(ActionType::COPY_NOTE)
              .onLongPress(ActionType::PASTE_NOTE));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_MOVE_BACK_BEAT, ch16, "Move Back Beat")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-tick16 * 2));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_MOVE_FORWARD_BEAT, ch16, "Move Forward Beat")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(tick16 * 2));
    
    addButton(ButtonConfig(MidiConfig::ExtendedTransport::NOTE_MOVE_BACK_16TH, ch16, "Move Back 16th")
              .onShortPress(ActionType::MOVE_CURRENT_TICK)
              .withParameter(-tick16 / 2));
    
    // Loop Selection (Channel 16, notes 50-57)
    // short: slot-aware toggle record flow
    //   - empty+playing: queue next-wrap record, second press = immediate punch-in
    //   - non-empty+playing: overdub toggle
    // long: clear slot, double/triple: undo/redo slot
    #define ENABLE_LOOP_BUTTONS 1
    #if ENABLE_LOOP_BUTTONS
    static char loopDescBuf[::Config::MAX_LOOPS_PER_TRACK][10];
    for (uint8_t i = 0; i < ::Config::MAX_LOOPS_PER_TRACK; i++) {
        snprintf(loopDescBuf[i], sizeof(loopDescBuf[0]), "Loop %d", i + 1);
        addButton(ButtonConfig(MidiConfig::Led::LOOP_SELECT_LED_BASE + i, Channels::TRACK_SELECT, loopDescBuf[i])
                  .onShortPress(ActionType::TOGGLE_RECORD_FOR_SLOT)
                  .onLongPress(ActionType::CLEAR_TRACK_FOR_SLOT)
                  .onDoublePress(ActionType::OVERDUB_FOR_SLOT)
                  .onTriplePress(ActionType::REDO_FOR_SLOT)
                  .withParameter(i));
    }
    #endif

    // Track Selection (Channel 16, notes 60+) - matches DROID, count from Config::NUM_TRACKS
    // short=select, double=mute, long=solo (select-primary; mute as short felt irrational)
    static char trackDescBuf[::Config::NUM_TRACKS][10];
    for (uint8_t i = 0; i < ::Config::NUM_TRACKS; i++) {
        snprintf(trackDescBuf[i], sizeof(trackDescBuf[0]), "Track %d", i + 1);
        addButton(ButtonConfig(MidiConfig::TrackSelect::NOTE_BASE + i, Channels::TRACK_SELECT, trackDescBuf[i])
                  .onShortPress(ActionType::SELECT_TRACK)
                  .onDoublePress(ActionType::MUTE_TRACK)
                  .onLongPress(ActionType::SOLO_TRACK)
                  .withParameter(i));
    }
    
    // Nav buttons (notes 64-75) removed: not implemented in DROID ini, and 64-67 would
    // conflict with track select if moved to ch16. Add back when DROID supports them.
    
    logger.info("Loaded DROID button configuration");
}

} // namespace MidiButtonConfig 