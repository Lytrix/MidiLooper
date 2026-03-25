#include "MidiLedManager.h"
#include "Utils/NoteUtils.h"

MidiLedManager::MidiLedManager(MidiHandler& midiHandler) 
    : midiHandler(midiHandler),
      lastUpdateBar(UINT32_MAX), lastLoopLength(0), lastLoopStartTick(UINT32_MAX), hasInitialized(false), currentTickStep(-1) {
    for (int i = 0; i < NUM_LEDS; i++) {
        lastLedState[i] = false;
    }
    for (int i = 0; i < NUM_BAR_LEDS; i++) {
        lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    for (int i = 0; i < NUM_TRACK_LEDS; i++) {
        lastTrackSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    for (uint8_t i = 0; i < MidiConfig::Led::LOOP_SELECT_LED_COUNT; i++) {
        lastLoopSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
}

void MidiLedManager::updateLeds(Track& track, uint32_t currentTick) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        // Clear LEDs when track has no loop (startup, or switched to empty track)
        if (lastLoopLength != 0 || !hasInitialized) {
            clearAllLeds();
            lastLoopLength = 0;
            hasInitialized = true;
        }
        return;
    }

    uint32_t startLoopTick = track.getStartLoopTick();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    // Force full refresh when loop length or loop start changes
    if (loopLength != lastLoopLength) {
        lastLoopLength = loopLength;
        lastUpdateBar = UINT32_MAX;
        hasInitialized = false;
    }
    if (loopStartTick != lastLoopStartTick) {
        lastLoopStartTick = loopStartTick;
        lastUpdateBar = UINT32_MAX;
        hasInitialized = false;
    }
    
    // Calculate current bar (relative to loop start for correct 16th display)
    uint32_t currentBar = getCurrentBar(currentTick, loopLength, startLoopTick, loopStartTick);
    
    // Only update when bar index changes, or on first initialization.
    uint32_t barStartTickDisplay = getCurrentBarStartTick(currentTick, loopLength, startLoopTick, loopStartTick);
    if (!hasInitialized || currentBar != lastUpdateBar) {
        analyzeAndUpdateBar(track, barStartTickDisplay, loopLength, loopStartTick);
        updateBarLeds(track, loopLength, currentBar, loopStartTick);
        
        lastUpdateBar = currentBar;
        hasInitialized = true;
        
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Updated LEDs for current bar starting at tick %lu (current tick %lu)", 
                   barStartTickDisplay, currentTick);
    }
}

void MidiLedManager::forceUpdate(Track& track, uint32_t currentTick) {
    lastUpdateBar = UINT32_MAX;
    lastLoopLength = 0;
    lastLoopStartTick = UINT32_MAX;
    hasInitialized = false;
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
    }
    currentTickStep = -1;
    
    // Turn off 8 bar LEDs (notes 40-47) - NoteOff required when clearing
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        midiHandler.sendNoteOff(LED_CHANNEL, BAR_LED_BASE_NOTE + i, 0);
        lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    
    // Turn off track (60-67) and loop (50-57) select LEDs
    for (uint8_t i = 0; i < NUM_TRACK_LEDS; i++) {
        midiHandler.sendNoteOff(LED_CHANNEL, MidiConfig::Led::TRACK_SELECT_LED_BASE + i, 0);
        lastTrackSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    for (uint8_t i = 0; i < MidiConfig::Led::LOOP_SELECT_LED_COUNT; i++) {
        midiHandler.sendNoteOff(LED_CHANNEL, MidiConfig::Led::LOOP_SELECT_LED_BASE + i, 0);
        lastLoopSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    
    logger.log(CAT_MIDI_LED, LOG_INFO, "LED Manager: All LEDs and tick indicator cleared");
}

void MidiLedManager::updateTrackSelectLeds(uint8_t selectedTrackIndex, const bool trackHasData[Config::NUM_TRACKS],
                                          uint8_t focusSlotIndex, const uint8_t slotVelocities[Config::MAX_LOOPS_PER_TRACK]) {
    static constexpr uint8_t VEL_SELECTED = 127;
    static constexpr uint8_t VEL_HAS_DATA = 32;
    (void)focusSlotIndex;  // Loop row is driven by slotVelocities
    
    // Track row (notes 60-67)
    for (uint8_t i = 0; i < NUM_TRACK_LEDS && i < Config::NUM_TRACKS; i++) {
        uint8_t velocity = (i == selectedTrackIndex) ? VEL_SELECTED
                         : (trackHasData[i] ? VEL_HAS_DATA : 0);
        uint8_t note = MidiConfig::Led::TRACK_SELECT_LED_BASE + i;
        
        if (lastTrackSelectVelocity[i] != velocity) {
            if (velocity > 0) {
                midiHandler.sendNoteOn(LED_CHANNEL, note, velocity);
            } else {
                midiHandler.sendNoteOff(LED_CHANNEL, note, 0);
            }
            lastTrackSelectVelocity[i] = velocity;
        }
    }
    
    // Loop row (notes 50-57) for selected track: slotVelocities drive the LED directly.
    for (uint8_t i = 0; i < MidiConfig::Led::LOOP_SELECT_LED_COUNT && i < Config::MAX_LOOPS_PER_TRACK; i++) {
        uint8_t velocity = slotVelocities[i];
        // Safety: if the caller doesn't fill velocities, keep previous semantics.
        if (velocity == 0 && slotVelocities[i] == 0 && i < Config::MAX_LOOPS_PER_TRACK) {
            // no-op
        }
        uint8_t note = MidiConfig::Led::LOOP_SELECT_LED_BASE + i;
        
        if (lastLoopSelectVelocity[i] != velocity) {
            if (velocity > 0) {
                midiHandler.sendNoteOn(LED_CHANNEL, note, velocity);
            } else {
                midiHandler.sendNoteOff(LED_CHANNEL, note, 0);
            }
            lastLoopSelectVelocity[i] = velocity;
        }
    }
}

uint32_t MidiLedManager::getCurrentBar(uint32_t currentTick, uint32_t loopLength, uint32_t startLoopTick, uint32_t loopStartTick) {
    uint32_t ticksPerBar = 16 * Config::TICKS_PER_16TH_STEP;
    uint32_t tickInLoopStorage = (currentTick - startLoopTick) % loopLength;
    uint32_t tickInLoopDisplay = (tickInLoopStorage - loopStartTick + loopLength) % loopLength;
    return tickInLoopDisplay / ticksPerBar;
}

uint32_t MidiLedManager::getCurrentBarStartTick(uint32_t currentTick, uint32_t loopLength, uint32_t startLoopTick, uint32_t loopStartTick) {
    uint32_t ticksPerBar = 16 * Config::TICKS_PER_16TH_STEP;
    uint32_t currentBar = getCurrentBar(currentTick, loopLength, startLoopTick, loopStartTick);
    return currentBar * ticksPerBar;
}

bool MidiLedManager::hasNoteInSixteenthStep(Track& track, uint32_t stepStartStorage, uint32_t stepEndStorage) {
    auto& midiEvents = track.getMidiEvents();
    
    // Check if any note-on events fall within this 16th step
    for (const auto& event : midiEvents) {
        if (event.type == midi::NoteOn && event.data.noteData.velocity > 0) {
            uint32_t noteTick = event.tick;
            
            // Handle normal case (step doesn't wrap around loop)
            if (stepStartStorage < stepEndStorage) {
                if (noteTick >= stepStartStorage && noteTick < stepEndStorage) {
                    return true;
                }
            }
            // Handle wrap-around case (step crosses loop boundary)
            else {
                if (noteTick >= stepStartStorage || noteTick < stepEndStorage) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool MidiLedManager::hasNoteInBar(Track& track, uint32_t barStartStorage, uint32_t barEndStorage, uint32_t loopLength) {
    auto& midiEvents = track.getMidiEvents();
    
    for (const auto& event : midiEvents) {
        if (event.type != midi::NoteOn || event.data.noteData.velocity == 0) continue;
        uint32_t noteTick = event.tick;
        
        if (barStartStorage < barEndStorage) {
            if (noteTick >= barStartStorage && noteTick < barEndStorage) return true;
        } else {
            if (noteTick >= barStartStorage || noteTick < barEndStorage) return true;
        }
    }
    return false;
}

void MidiLedManager::updateBarLeds(Track& track, uint32_t loopLength, uint32_t currentBar, uint32_t loopStartTick) {
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
    
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        uint32_t barStartDisplay = i * ticksPerBar;
        uint32_t barEndDisplay = (i + 1) * ticksPerBar;
        uint8_t note = BAR_LED_BASE_NOTE + i;
        
        if (loopLength <= barStartDisplay) {
            // NoteOff when bar is beyond loop (resize smaller, track switch, length edit)
            midiHandler.sendNoteOff(LED_CHANNEL, note, 0);
            lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
        } else {
            // Convert display-space bar to storage-space for note lookup
            uint32_t barStartStorage = (loopStartTick + barStartDisplay) % loopLength;
            uint32_t barEndStorage = (loopStartTick + barEndDisplay) % loopLength;
            bool isCurrentBar = (i == currentBar && currentBar < NUM_BAR_LEDS);
            bool hasNotes = hasNoteInBar(track, barStartStorage, barEndStorage, loopLength);
            uint8_t velocity = isCurrentBar ? VEL_BAR_CURRENT : (hasNotes ? VEL_BAR_HAS_NOTES : VEL_BAR_USED);
            // Only send NoteOn when velocity changes - no NoteOff during normal playback
            if (lastBarVelocity[i] != velocity) {
                midiHandler.sendNoteOn(LED_CHANNEL, note, velocity);
                lastBarVelocity[i] = velocity;
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
}

void MidiLedManager::updateCurrentTick(Track& track, uint32_t currentTick) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    uint32_t startLoopTick = track.getStartLoopTick();
    uint32_t loopStartTick = track.getLoopStartTick();
    uint32_t ticksPerSixteenth = Config::TICKS_PER_16TH_STEP;
    uint32_t ticksPerBar = ticksPerSixteenth * NUM_LEDS;
    
    // Position in loop relative to loop start (matches 16th/bar LED display)
    uint32_t tickInLoopStorage = (currentTick - startLoopTick) % loopLength;
    uint32_t tickInLoopDisplay = (tickInLoopStorage - loopStartTick + loopLength) % loopLength;
    uint32_t tickInBar = tickInLoopDisplay % ticksPerBar;
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
        }
        
        // Turn on new tick indicator (notes 16-31)
        midiHandler.sendNoteOn(TICK_CHANNEL, TICK_NOTE_OFFSET + newTickStep, TICK_VELOCITY);
        
        currentTickStep = newTickStep;
        
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Current tick step %d (tick %lu)", 
                   newTickStep, currentTick);
    }
}

void MidiLedManager::analyzeAndUpdateBar(Track& track, uint32_t barStartTickDisplay, uint32_t loopLength, uint32_t loopStartTick) {
    uint32_t ticksPerSixteenth = Config::TICKS_PER_16TH_STEP;
    bool newLedState[NUM_LEDS];
    
    // Analyze each 16th note position (convert display-space bar to storage-space for note lookup)
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t stepStartDisplay = barStartTickDisplay + (i * ticksPerSixteenth);
        uint32_t stepEndDisplay = barStartTickDisplay + ((i + 1) * ticksPerSixteenth);
        uint32_t stepStartStorage = (loopStartTick + stepStartDisplay) % loopLength;
        uint32_t stepEndStorage = (loopStartTick + stepEndDisplay) % loopLength;
        
        newLedState[i] = hasNoteInSixteenthStep(track, stepStartStorage, stepEndStorage);
    }
    
    // Only send when LED state changed (state-driven, no redundant updates)
    for (int i = 0; i < NUM_LEDS; i++) {
        if (newLedState[i] != lastLedState[i]) {
            sendLedUpdate(i, newLedState[i]);
            lastLedState[i] = newLedState[i];
        }
    }
    
    // Debug logging
    String ledPattern = "";
    for (int i = 0; i < NUM_LEDS; i++) {
        ledPattern += newLedState[i] ? "1" : "0";
    }
    logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Bar pattern (tick %lu): %s", 
               barStartTickDisplay, ledPattern.c_str());
} 