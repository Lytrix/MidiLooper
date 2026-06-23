//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiFaderProcessor.h"
#include "Logger.h"
#include "EditManager.h"
#include "NoteEditFocus.h"
#include "TrackManager.h"
#include "Utils/NoteUtils.h"

MidiFaderProcessor::MidiFaderProcessor() 
    : currentDriverFader(MidiMapping::FaderType::FADER_SELECT)
    , lastDriverFaderTime(0)
    , lastDriverFaderUpdateTime(0)
    , lastSelectnoteFaderTime(0) {
}

void MidiFaderProcessor::setup() {
    initializeFaderStates();
    logger.info("MidiFaderProcessor setup complete with %d faders", faderStates.size());
}

void MidiFaderProcessor::update() {
    // NOTE: Scheduled update processing disabled - all cross-updates now handled by NoteEditManager
    // This eliminates the dual scheduling system that was causing conflicts
}

void MidiFaderProcessor::handlePitchbend(uint8_t channel, int16_t pitchValue) {
    // Find the configured fader for this channel and pitchbend
    const MidiFaderConfig::FaderConfig* config = 
        MidiFaderConfig::Config::findFaderConfigByChannel(channel, MidiFaderConfig::InputType::PITCHBEND);
    
    if (config == nullptr) {
        return; // No fader configured for this channel/pitchbend
    }
    
    processFaderInput(config->type, pitchValue, 0);
}

void MidiFaderProcessor::handleCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    // Find the configured fader for this channel and CC number
    const MidiFaderConfig::FaderConfig* config = 
        MidiFaderConfig::Config::findFaderConfigByChannel(channel, MidiFaderConfig::InputType::CC_CONTROL, ccNumber);
    
    if (config == nullptr) {
        return; // No fader configured for this channel/CC
    }
    
    processFaderInput(config->type, 0, value);
}

void MidiFaderProcessor::processFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    if (shouldIgnoreFaderInput(faderType, pitchbendValue, ccValue)) {
        return;
    }
    
    FaderState& state = getFaderStateMutable(faderType);
    uint32_t now = millis();
    
    // Initialize on first input
    if (!state.isInitialized) {
        state.lastPitchbendValue = pitchbendValue;
        state.lastCCValue = ccValue;
        state.isInitialized = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d initialized: pitchbend=%d cc=%d", 
                   (int)faderType, pitchbendValue, ccValue);
        return;
    }
    
    // Check for significant change
    if (!hasSignificantChange(state, pitchbendValue, ccValue)) {
        return; // Change too small - ignore to prevent jitter
    }
    
    // Update state
    state.lastPitchbendValue = pitchbendValue;
    state.lastCCValue = ccValue;
    state.lastUpdateTime = now;
    
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (editManager.isNoteEditActive() && focus.active && currentDriverFader != faderType) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Committing note movement - switching from fader %d to fader %d", 
                   (int)currentDriverFader, (int)faderType);
        commitMovingNote();
    }
    
    // Set this fader as the current driver
    setDriverFader(faderType);
    
    // Trigger the callback
    if (movementCallback) {
        movementCallback(faderType, pitchbendValue, ccValue);
    }
    
    // NOTE: Removed automatic scheduling - let NoteEditManager handle cross-updates
    // This prevents dual scheduling system conflicts
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d input processed: driver fader set", (int)faderType);
}

void MidiFaderProcessor::setDriverFader(MidiMapping::FaderType faderType) {
    currentDriverFader = faderType;
    uint32_t now = millis();
    lastDriverFaderTime = now;
    lastDriverFaderUpdateTime = now;
    
    if (faderType == MidiMapping::FaderType::FADER_SELECT) {
        lastSelectnoteFaderTime = now;
    }
}

const MidiFaderProcessor::FaderState& MidiFaderProcessor::getFaderState(MidiMapping::FaderType faderType) const {
    for (const auto& state : faderStates) {
        if (state.type == faderType) {
            return state;
        }
    }
    // Should never happen, but return first as fallback
    return faderStates[0];
}

MidiFaderProcessor::FaderState& MidiFaderProcessor::getFaderStateMutable(MidiMapping::FaderType faderType) {
    for (auto& state : faderStates) {
        if (state.type == faderType) {
            return state;
        }
    }
    // Should never happen, but return first as fallback
    return faderStates[0];
}

bool MidiFaderProcessor::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) const {
    const FaderState& state = getFaderState(faderType);
    uint32_t now = millis();
    
    // Ignore input within the feedback ignore period
    const MidiFaderConfig::FaderConfig* cfg = MidiFaderConfig::Config::findFaderConfig(faderType);
    uint32_t feedbackIgnoreMs = cfg ? cfg->feedbackIgnoreMs : 100;
    int16_t pitchbendDeadband = cfg ? cfg->pitchbendDeadband : 23;
    uint8_t ccDeadband = cfg ? cfg->ccDeadband : 1;

    if (state.lastSentTime > 0) {
        uint32_t timeSinceLastSent = now - state.lastSentTime;
        uint32_t remaining = (timeSinceLastSent < feedbackIgnoreMs) ? 
                            (feedbackIgnoreMs - timeSinceLastSent) : 0;
        
        if (remaining > 0) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (feedback protection, %lu ms remaining)", 
                       (int)faderType, remaining);
            return true;
        }
    }
    
    // Check if the input matches what we recently sent (prevent feedback)
    if (faderType == MidiMapping::FaderType::FADER_SELECT || faderType == MidiMapping::FaderType::FADER_COARSE) {
        if (pitchbendValue != -1) {
            int16_t diff = abs(pitchbendValue - state.lastSentPitchbend);
            if (diff < pitchbendDeadband) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d pitchbend input %d (too close to sent %d, diff=%d)", 
                           (int)faderType, pitchbendValue, state.lastSentPitchbend, diff);
                return true;
            }
        }
    } else if (faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        if (ccValue != 255) {
            uint8_t diff = abs((int)ccValue - (int)state.lastSentCC);
            if (diff < ccDeadband) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d CC input %d (too close to sent %d, diff=%d)", 
                           (int)faderType, ccValue, state.lastSentCC, diff);
                return true;
            }
        }
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Accepting fader %d input", (int)faderType);
    return false;
}

bool MidiFaderProcessor::hasSignificantChange(const FaderState& state, int16_t pitchbendValue, uint8_t ccValue) const {
    const MidiFaderConfig::FaderConfig* cfg = MidiFaderConfig::Config::findFaderConfig(state.type);
    int16_t pitchbendDeadband = cfg ? cfg->pitchbendDeadband : 23;
    uint8_t ccDeadband = cfg ? cfg->ccDeadband : 1;
    if (state.type == MidiMapping::FaderType::FADER_SELECT || state.type == MidiMapping::FaderType::FADER_COARSE) {
        // For pitchbend faders, check if change is significant
        int16_t pitchbendDiff = abs(pitchbendValue - state.lastPitchbendValue);
        return pitchbendDiff >= pitchbendDeadband;
    } else if (state.type == MidiMapping::FaderType::FADER_FINE || state.type == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        // For CC faders, use smaller deadband for precise control
        uint8_t ccDiff = abs((int)ccValue - (int)state.lastCCValue);
        return ccDiff >= ccDeadband;
    }
    
    return false;
}

void MidiFaderProcessor::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    // NOTE: This function is disabled - all cross-updates now handled by NoteEditManager
    // This eliminates the dual scheduling system that was causing conflicts
    (void)driverFader; // Suppress unused parameter warning
}

void MidiFaderProcessor::markFaderSent(MidiMapping::FaderType faderType) {
    FaderState& state = getFaderStateMutable(faderType);
    uint32_t now = millis();
    state.lastSentTime = now;
    
    // Grouping: set ignore periods for all faders sharing the same group key (or channel if groupKey==0)
    const MidiFaderConfig::FaderConfig* driverCfg = MidiFaderConfig::Config::findFaderConfig(faderType);
    uint8_t groupKey = 0;
    if (driverCfg) {
        groupKey = (driverCfg->groupKey != 0) ? driverCfg->groupKey : driverCfg->channel;
    }
    for (auto& s : faderStates) {
        const MidiFaderConfig::FaderConfig* cfg = MidiFaderConfig::Config::findFaderConfig(s.type);
        uint8_t sKey = 0;
        if (cfg) {
            sKey = (cfg->groupKey != 0) ? cfg->groupKey : cfg->channel;
        }
        if (groupKey != 0 && sKey == groupKey) {
            s.lastSentTime = now;
        }
    }
    
    uint32_t ignoreMs = driverCfg ? driverCfg->feedbackIgnoreMs : 100;
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d marked as sent - ignoring incoming for %dms", 
               (int)faderType, ignoreMs);
}

void MidiFaderProcessor::initializeFaderStates() {
    faderStates.clear();
    const auto& configs = MidiFaderConfig::Config::getFaderConfigs();
    faderStates.reserve(configs.size());
    for (const auto& cfg : configs) {
        FaderState s;
        s.type = cfg.type;
        s.channel = cfg.channel;
        s.isInitialized = false;
        s.lastPitchbendValue = cfg.pitchbendCenter;
        s.lastCCValue = cfg.initialCC;
        s.lastUpdateTime = 0;
        s.lastSentTime = 0;
        s.pendingUpdate = false;
        s.updateScheduledTime = 0;
        s.scheduledByDriver = MidiMapping::FaderType::FADER_SELECT;
        s.lastSentPitchbend = 0;
        s.lastSentCC = 0;
        faderStates.push_back(s);
    }
    logger.info("Fader state machine initialized with %d faders", faderStates.size());
}

void MidiFaderProcessor::commitMovingNote() {
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (!editManager.isNoteEditActive() || !focus.active) {
        return;
    }
    logger.log(CAT_MIDI, LOG_DEBUG, "Committing note movement for note %d at tick %lu",
               focus.last.pitch, static_cast<unsigned long>(focus.last.startTick));
} 