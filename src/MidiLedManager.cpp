#include "MidiLedManager.h"
#include "Utils/NoteUtils.h"

MidiLedManager::MidiLedManager(MidiHandler& midiHandler) 
    : midiHandler(midiHandler), updateDelayMicros(DEFAULT_UPDATE_DELAY),
      lastUpdateBar(UINT32_MAX), lastLoopLength(0), hasInitialized(false), currentTickStep(-1) {
    for (int i = 0; i < NUM_LEDS; i++) {
        lastLedState[i] = false;
    }
    for (int i = 0; i < NUM_BAR_LEDS; i++) {
        lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
    }
}

void MidiLedManager::updateLeds(Track& track, uint32_t currentTick) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Force full refresh when loop length changes (avoids gaps when length edited mid-playback)
    if (loopLength != lastLoopLength) {
        lastLoopLength = loopLength;
        lastUpdateBar = UINT32_MAX;
        hasInitialized = false;
        // Don't reset lastBarVelocity - bars that move beyond loop need NoteOff
    }
    
    // Calculate current bar
    uint32_t currentBar = getCurrentBar(currentTick, loopLength);
    
    // Only update on the first tick of a new bar, or if not initialized
    uint32_t barStartTick = getCurrentBarStartTick(currentTick, loopLength);
    uint32_t tickInLoop = currentTick % loopLength;
    bool isFirstTickOfBar = (tickInLoop == barStartTick) || (currentTick == 0);
    
    if (!hasInitialized || isFirstTickOfBar || currentBar != lastUpdateBar) {
        // Analyze the current bar that's playing
        analyzeAndUpdateBar(track, barStartTick, loopLength);
        updateBarLeds(track, loopLength, currentBar);
        
        lastUpdateBar = currentBar;
        hasInitialized = true;
        
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Updated LEDs for current bar starting at tick %lu (current tick %lu)", 
                   barStartTick, currentTick);
    }
}

void MidiLedManager::forceUpdate(Track& track, uint32_t currentTick) {
    lastUpdateBar = UINT32_MAX;
    lastLoopLength = 0;
    hasInitialized = false;
    // Don't reset lastBarVelocity - bars beyond new loop need NoteOff (lastBarVelocity stays set)
    updateLeds(track, currentTick);
}

void MidiLedManager::clearAllLeds() {
    // Turn off all LEDs
    for (int i = 0; i < NUM_LEDS; i++) {
        sendLedUpdate(i, false);
        lastLedState[i] = false;
    }
    
    // Turn off current tick indicator (uses notes 16-31)
    if (currentTickStep >= 0 && currentTickStep < NUM_LEDS) {
        midiHandler.sendNoteOff(TICK_CHANNEL, TICK_NOTE_OFFSET + currentTickStep, 0);
        delayMicroseconds(updateDelayMicros);
    }
    currentTickStep = -1;
    
    // Turn off 8 bar LEDs (notes 40-47) - NoteOff required when clearing
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        midiHandler.sendNoteOff(LED_CHANNEL, BAR_LED_BASE_NOTE + i, 0);
        lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
        delayMicroseconds(updateDelayMicros);
    }
    
    logger.log(CAT_MIDI_LED, LOG_INFO, "LED Manager: All LEDs and tick indicator cleared");
}

uint32_t MidiLedManager::getCurrentBar(uint32_t currentTick, uint32_t loopLength) {
    uint32_t ticksPerBar = 16 * Config::TICKS_PER_16TH_STEP;
    return (currentTick % loopLength) / ticksPerBar;
}

uint32_t MidiLedManager::getCurrentBarStartTick(uint32_t currentTick, uint32_t loopLength) {
    uint32_t ticksPerBar = 16 * Config::TICKS_PER_16TH_STEP;
    uint32_t currentBar = getCurrentBar(currentTick, loopLength);
    
    // Return the start tick of the current bar
    return currentBar * ticksPerBar;
}

bool MidiLedManager::hasNoteInSixteenthStep(Track& track, uint32_t stepStartTick, uint32_t stepEndTick) {
    auto& midiEvents = track.getMidiEvents();
    
    // Check if any note-on events fall within this 16th step
    for (const auto& event : midiEvents) {
        if (event.type == midi::NoteOn && event.data.noteData.velocity > 0) {
            uint32_t noteTick = event.tick;
            
            // Handle normal case (step doesn't wrap around loop)
            if (stepStartTick < stepEndTick) {
                if (noteTick >= stepStartTick && noteTick < stepEndTick) {
                    return true;
                }
            }
            // Handle wrap-around case (step crosses loop boundary)
            else {
                if (noteTick >= stepStartTick || noteTick < stepEndTick) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool MidiLedManager::hasNoteInBar(Track& track, uint32_t barStartTick, uint32_t barEndTick, uint32_t loopLength) {
    auto& midiEvents = track.getMidiEvents();
    
    for (const auto& event : midiEvents) {
        if (event.type != midi::NoteOn || event.data.noteData.velocity == 0) continue;
        uint32_t noteTick = event.tick;
        
        if (barEndTick <= loopLength) {
            if (noteTick >= barStartTick && noteTick < barEndTick) return true;
        } else {
            if (noteTick >= barStartTick || noteTick < (barEndTick - loopLength)) return true;
        }
    }
    return false;
}

void MidiLedManager::updateBarLeds(Track& track, uint32_t loopLength, uint32_t currentBar) {
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
    
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        uint32_t barStartTick = i * ticksPerBar;
        uint32_t barEndTick = (i + 1) * ticksPerBar;
        uint8_t note = BAR_LED_BASE_NOTE + i;
        
        if (loopLength <= barStartTick) {
            // NoteOff only when required: bar beyond loop (track switch, length edit)
            if (lastBarVelocity[i] != BAR_VEL_NEVER_SENT) {
                midiHandler.sendNoteOff(LED_CHANNEL, note, 0);
                lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
                delayMicroseconds(updateDelayMicros);
            }
        } else {
            bool isCurrentBar = (i == currentBar && currentBar < NUM_BAR_LEDS);
            bool hasNotes = hasNoteInBar(track, barStartTick, barEndTick, loopLength);
            uint8_t velocity = isCurrentBar ? VEL_BAR_CURRENT : (hasNotes ? VEL_BAR_HAS_NOTES : VEL_BAR_USED);
            // Only send NoteOn when velocity changes - no NoteOff during normal playback
            if (lastBarVelocity[i] != velocity) {
                midiHandler.sendNoteOn(LED_CHANNEL, note, velocity);
                lastBarVelocity[i] = velocity;
                delayMicroseconds(updateDelayMicros);
            }
        }
    }
}

void MidiLedManager::sendLedUpdate(uint8_t ledIndex, bool state) {
    if (ledIndex >= NUM_LEDS) return;
    
    if (state) {
        // Turn LED on
        midiHandler.sendNoteOn(LED_CHANNEL, ledIndex, LED_VELOCITY);
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: LED %d ON", ledIndex);
    } else {
        // Turn LED off
        midiHandler.sendNoteOff(LED_CHANNEL, ledIndex, 0);
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: LED %d OFF", ledIndex);
    }
    
    // Small delay to ensure MIDI controller processes the message
    delayMicroseconds(updateDelayMicros);
}

void MidiLedManager::setUpdateDelay(uint16_t delayMicros) {
    updateDelayMicros = delayMicros;
    logger.log(CAT_MIDI_LED, LOG_INFO, "LED Manager: Update delay set to %d microseconds", delayMicros);
}

void MidiLedManager::updateCurrentTick(uint32_t currentTick, uint32_t loopLength) {
    uint32_t ticksPerSixteenth = Config::TICKS_PER_16TH_STEP;
    uint32_t ticksPerBar = ticksPerSixteenth * NUM_LEDS;
    
    // Calculate current position within the current bar
    uint32_t tickInLoop = currentTick % loopLength;
    uint32_t tickInBar = tickInLoop % ticksPerBar;
    int8_t newTickStep = tickInBar / ticksPerSixteenth;
    
    // Ensure we're within valid range
    if (newTickStep >= NUM_LEDS) {
        newTickStep = NUM_LEDS - 1;
    }
    
    // Only update if the step changed
    if (newTickStep != currentTickStep) {
        // Turn off previous tick indicator (uses notes 16-31)
        if (currentTickStep >= 0 && currentTickStep < NUM_LEDS) {
            midiHandler.sendNoteOff(TICK_CHANNEL, TICK_NOTE_OFFSET + currentTickStep, 0);
            delayMicroseconds(updateDelayMicros);
        }
        
        // Turn on new tick indicator (notes 16-31)
        midiHandler.sendNoteOn(TICK_CHANNEL, TICK_NOTE_OFFSET + newTickStep, TICK_VELOCITY);
        delayMicroseconds(updateDelayMicros);
        
        currentTickStep = newTickStep;
        
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Current tick step %d (tick %lu)", 
                   newTickStep, currentTick);
    }
}

void MidiLedManager::analyzeAndUpdateBar(Track& track, uint32_t barStartTick, uint32_t loopLength) {
    uint32_t ticksPerSixteenth = Config::TICKS_PER_16TH_STEP;
    bool newLedState[NUM_LEDS];
    
    // Analyze each 16th note position in the bar
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t stepStartTick = (barStartTick + (i * ticksPerSixteenth)) % loopLength;
        uint32_t stepEndTick = (barStartTick + ((i + 1) * ticksPerSixteenth)) % loopLength;
        
        newLedState[i] = hasNoteInSixteenthStep(track, stepStartTick, stepEndTick);
    }
    
    // Always send all LED states to ensure sync
    for (int i = 0; i < NUM_LEDS; i++) {
        sendLedUpdate(i, newLedState[i]);
        lastLedState[i] = newLedState[i];
    }
    
    // Debug logging
    String ledPattern = "";
    for (int i = 0; i < NUM_LEDS; i++) {
        ledPattern += newLedState[i] ? "1" : "0";
    }
    logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Bar pattern (tick %lu): %s", 
               barStartTick, ledPattern.c_str());
} 