//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "MidiFaderProcessor.h"
#include "Logger.h"
#include "EditManager.h"
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
    
    // Check if we need to commit any active note movement before switching faders
    if (editManager.movingNote.active && currentDriverFader != faderType) {
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
    if (state.lastSentTime > 0) {
        uint32_t timeSinceLastSent = now - state.lastSentTime;
        uint32_t remaining = (timeSinceLastSent < FEEDBACK_IGNORE_PERIOD) ? 
                            (FEEDBACK_IGNORE_PERIOD - timeSinceLastSent) : 0;
        
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
            if (diff < PITCHBEND_DEADBAND) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d pitchbend input %d (too close to sent %d, diff=%d)", 
                           (int)faderType, pitchbendValue, state.lastSentPitchbend, diff);
                return true;
            }
        }
    } else if (faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        if (ccValue != 255) {
            uint8_t diff = abs((int)ccValue - (int)state.lastSentCC);
            if (diff < CC_DEADBAND_FINE) {
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
    if (state.type == MidiMapping::FaderType::FADER_SELECT || state.type == MidiMapping::FaderType::FADER_COARSE) {
        // For pitchbend faders, check if change is significant
        int16_t pitchbendDiff = abs(pitchbendValue - state.lastPitchbendValue);
        return pitchbendDiff >= PITCHBEND_DEADBAND;
    } else if (state.type == MidiMapping::FaderType::FADER_FINE || state.type == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        // For CC faders, use smaller deadband for precise control
        uint8_t ccDiff = abs((int)ccValue - (int)state.lastCCValue);
        return ccDiff >= CC_DEADBAND_FINE;
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
    
    // IMPORTANT: If updating any channel 15 fader, all channel 15 faders get updated together.
    // Set ignore periods for all to prevent feedback from any MIDI message causing unwanted processing.
    if (faderType == MidiMapping::FaderType::FADER_COARSE || 
        faderType == MidiMapping::FaderType::FADER_FINE || 
        faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        
        getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = now;
        getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = now;
        getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = now;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Set ignore periods for all channel 15 faders (shared channel)");
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d marked as sent - ignoring incoming for %dms", 
               (int)faderType, FEEDBACK_IGNORE_PERIOD);
}

void MidiFaderProcessor::initializeFaderStates() {
    faderStates.clear();
    faderStates.resize(4);
    
    // Initialize Fader 1: Note Selection (Channel 16, Pitchbend)
    faderStates[0] = {
        .type = MidiMapping::FaderType::FADER_SELECT,
        .channel = 16,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = MidiMapping::FaderType::FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    // Initialize Fader 2: Coarse Positioning (Channel 15, Pitchbend)
    faderStates[1] = {
        .type = MidiMapping::FaderType::FADER_COARSE,
        .channel = 15,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = MidiMapping::FaderType::FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    // Initialize Fader 3: Fine Positioning (Channel 15, CC2)
    faderStates[2] = {
        .type = MidiMapping::FaderType::FADER_FINE,
        .channel = 15,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = MidiMapping::FaderType::FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    // Initialize Fader 4: Note Value Editing (Channel 15, CC3)
    faderStates[3] = {
        .type = MidiMapping::FaderType::FADER_NOTE_VALUE,
        .channel = 15,
        .isInitialized = false,
        .lastPitchbendValue = PITCHBEND_CENTER,
        .lastCCValue = 64,
        .lastUpdateTime = 0,
        .lastSentTime = 0,
        .pendingUpdate = false,
        .updateScheduledTime = 0,
        .scheduledByDriver = MidiMapping::FaderType::FADER_SELECT,
        .lastSentPitchbend = 0,
        .lastSentCC = 0
    };
    
    logger.info("Fader state machine initialized with 4 faders");
}

void MidiFaderProcessor::commitMovingNote() {
    if (!editManager.movingNote.active) {
        return;
    }
    
    // Find the currently moving note and commit the movement
    Track& track = trackManager.getSelectedTrack();
    auto& midiEvents = track.getMidiEvents();
    auto currentNotes = NoteUtils::reconstructNotes(midiEvents, track.getLoopLength());
    
    for (const auto& note : currentNotes) {
        if (note.note == editManager.movingNote.note && 
            note.startTick == editManager.movingNote.lastStart) {
            // We need to delegate this to MidiButtonManager for the actual implementation
            // This will be handled in the actions class
            logger.log(CAT_MIDI, LOG_DEBUG, "Committing note movement for note %d at tick %lu", 
                       note.note, note.startTick);
            break;
        }
    }
} 