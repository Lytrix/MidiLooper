#include "MidiLedManager.h"
#include "Utils/NoteUtils.h"

MidiLedManager::MidiLedManager(MidiHandler& midiHandler) 
    : midiHandler(midiHandler), lastUpdateBar(UINT32_MAX), hasInitialized(false),
      updateDelayMicros(DEFAULT_UPDATE_DELAY), currentTickStep(-1) {
    // Initialize LED state tracking
    for (int i = 0; i < NUM_LEDS; i++) {
        lastLedState[i] = false;
    }
}

void MidiLedManager::updateLeds(Track& track, uint32_t currentTick) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Calculate current bar
    uint32_t currentBar = getCurrentBar(currentTick, loopLength);
    
    // Only update on the first tick of a new bar, or if not initialized
    uint32_t barStartTick = getCurrentBarStartTick(currentTick, loopLength);
    bool isFirstTickOfBar = (currentTick == barStartTick) || (currentTick == 0);
    
    if (!hasInitialized || isFirstTickOfBar || currentBar != lastUpdateBar) {
        // Analyze the current bar that's playing
        analyzeAndUpdateBar(track, barStartTick, loopLength);
        
        lastUpdateBar = currentBar;
        hasInitialized = true;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "LED Manager: Updated LEDs for current bar starting at tick %lu (current tick %lu)", 
                   barStartTick, currentTick);
    }
}

void MidiLedManager::forceUpdate(Track& track, uint32_t currentTick) {
    lastUpdateBar = UINT32_MAX; // Force update
    hasInitialized = false;
    updateLeds(track, currentTick);
}

void MidiLedManager::clearAllLeds() {
    // Turn off all LEDs
    for (int i = 0; i < NUM_LEDS; i++) {
        sendLedUpdate(i, false);
        lastLedState[i] = false;
    }
    
    // Turn off current tick indicator
    if (currentTickStep >= 0 && currentTickStep < NUM_LEDS) {
        midiHandler.sendNoteOff(TICK_CHANNEL, currentTickStep, 0);
        delayMicroseconds(updateDelayMicros);
    }
    currentTickStep = -1;
    
    logger.log(CAT_MIDI, LOG_INFO, "LED Manager: All LEDs and tick indicator cleared");
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

void MidiLedManager::sendLedUpdate(uint8_t ledIndex, bool state) {
    if (ledIndex >= NUM_LEDS) return;
    
    if (state) {
        // Turn LED on
        midiHandler.sendNoteOn(LED_CHANNEL, ledIndex, LED_VELOCITY);
        logger.log(CAT_MIDI, LOG_DEBUG, "LED Manager: LED %d ON", ledIndex);
    } else {
        // Turn LED off
        midiHandler.sendNoteOff(LED_CHANNEL, ledIndex, 0);
        logger.log(CAT_MIDI, LOG_DEBUG, "LED Manager: LED %d OFF", ledIndex);
    }
    
    // Small delay to ensure MIDI controller processes the message
    delayMicroseconds(updateDelayMicros);
}

void MidiLedManager::setUpdateDelay(uint16_t delayMicros) {
    updateDelayMicros = delayMicros;
    logger.log(CAT_MIDI, LOG_INFO, "LED Manager: Update delay set to %d microseconds", delayMicros);
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
        // Turn off previous tick indicator
        if (currentTickStep >= 0 && currentTickStep < NUM_LEDS) {
            midiHandler.sendNoteOff(TICK_CHANNEL, currentTickStep, 0);
            delayMicroseconds(updateDelayMicros);
        }
        
        // Turn on new tick indicator
        midiHandler.sendNoteOn(TICK_CHANNEL, newTickStep, TICK_VELOCITY);
        delayMicroseconds(updateDelayMicros);
        
        currentTickStep = newTickStep;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "LED Manager: Current tick step %d (tick %lu)", 
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
    logger.log(CAT_MIDI, LOG_DEBUG, "LED Manager: Bar pattern (tick %lu): %s", 
               barStartTick, ledPattern.c_str());
} 