#include "MidiLedManager.h"
#include "Loop.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"
#include "TickPhase.h"

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

void MidiLedManager::updateLeds(Track& track, uint32_t currentTick, uint8_t displaySlotIndex) {
    const Loop& displayLoop = track.getLoop(displaySlotIndex);
    uint32_t loopLength = displayLoop.loopLengthTicks;
    if (displaySlotIndex != lastDisplaySlotIndex) {
        lastDisplaySlotIndex = displaySlotIndex;
        lastUpdateBar = UINT32_MAX;
        hasInitialized = false;
    }
    if (loopLength == 0) {
        // Clear only playback/tick/bar LEDs when selected active slot has no loop.
        // Keep track/loop select rows intact; those are managed by updateTrackSelectLeds().
        if (lastLoopLength != 0 || !hasInitialized) {
            clearPlaybackLedsOnly();
            lastLoopLength = 0;
            hasInitialized = true;
        }
        return;
    }

    uint32_t loopStartTick = displayLoop.loopStartTick;
    
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
    uint32_t currentBar = getCurrentBar(currentTick, displayLoop, track, displaySlotIndex);
    
    // Only update when bar index changes, or on first initialization.
    uint32_t barStartTickDisplay =
        getCurrentBarStartTick(currentTick, displayLoop, track, displaySlotIndex);
    if (!hasInitialized || currentBar != lastUpdateBar) {
        analyzeAndUpdateBar(displayLoop, barStartTickDisplay);
        updateBarLeds(displayLoop, currentBar);
        
        lastUpdateBar = currentBar;
        hasInitialized = true;
        
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Updated LEDs for current bar starting at tick %lu (current tick %lu)", 
                   barStartTickDisplay, currentTick);
    }
}

void MidiLedManager::forceUpdate(Track& track, uint32_t currentTick, uint8_t displaySlotIndex) {
    lastUpdateBar = UINT32_MAX;
    lastLoopLength = 0;
    lastLoopStartTick = UINT32_MAX;
    lastDisplaySlotIndex = Config::INVALID_LOOP_SLOT;
    lastFocusSlotIndex = Config::INVALID_LOOP_SLOT;
    lastSelectedTrackIndex = Config::INVALID_TRACK_INDEX;
    for (uint8_t i = 0; i < NUM_TRACK_LEDS; i++) {
        lastTrackSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    for (uint8_t i = 0; i < MidiConfig::Led::LOOP_SELECT_LED_COUNT; i++) {
        lastLoopSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    hasInitialized = false;
    updateLeds(track, currentTick, displaySlotIndex);
}

void MidiLedManager::clearAllLeds() {
    // Turn off all LEDs
    for (int i = 0; i < NUM_LEDS; i++) {
        sendLedUpdate(i, false);
        lastLedState[i] = false;
    }
    
    // Turn off current tick indicator (uses notes 16-31)
    if (currentTickStep >= 0 && currentTickStep < NUM_LEDS) {
        midiHandler.sendLedFeedbackNoteOff(TICK_NOTE_OFFSET + currentTickStep);
    }
    currentTickStep = -1;
    
    // Turn off 8 bar LEDs (notes 40-47) - NoteOff required when clearing
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        midiHandler.sendLedFeedbackNoteOff(BAR_LED_BASE_NOTE + i);
        lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    
    // Turn off track (60-67) and loop (50-57) select LEDs
    for (uint8_t i = 0; i < NUM_TRACK_LEDS; i++) {
        midiHandler.sendLedFeedbackNoteOff(MidiConfig::Led::TRACK_SELECT_LED_BASE + i);
        lastTrackSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    for (uint8_t i = 0; i < MidiConfig::Led::LOOP_SELECT_LED_COUNT; i++) {
        midiHandler.sendLedFeedbackNoteOff(MidiConfig::Led::LOOP_SELECT_LED_BASE + i);
        lastLoopSelectVelocity[i] = BAR_VEL_NEVER_SENT;
    }
    lastFocusSlotIndex = Config::INVALID_LOOP_SLOT;
    lastSelectedTrackIndex = Config::INVALID_TRACK_INDEX;
    
    logger.log(CAT_MIDI_LED, LOG_INFO, "LED Manager: All LEDs and tick indicator cleared");
}

void MidiLedManager::clearPlaybackLedsOnly() {
    // 16-step note LEDs
    for (int i = 0; i < NUM_LEDS; i++) {
        sendLedUpdate(i, false);
        lastLedState[i] = false;
    }

    // Tick indicator (notes 16-31)
    if (currentTickStep >= 0 && currentTickStep < NUM_LEDS) {
        midiHandler.sendLedFeedbackNoteOff(TICK_NOTE_OFFSET + currentTickStep);
    }
    currentTickStep = -1;

    // Bar LEDs (notes 40-47)
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        midiHandler.sendLedFeedbackNoteOff(BAR_LED_BASE_NOTE + i);
        lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
    }
}

void MidiLedManager::updateTrackSelectLeds(uint8_t selectedTrackIndex, const bool trackHasData[Config::NUM_TRACKS],
                                          uint8_t focusSlotIndex, const uint8_t slotVelocities[Config::MAX_LOOPS_PER_TRACK]) {
    static constexpr uint8_t VEL_SELECTED = 127;
    static constexpr uint8_t VEL_HAS_DATA = 32;
    const bool focusChanged = (focusSlotIndex != lastFocusSlotIndex);
    const bool trackChanged = (selectedTrackIndex != lastSelectedTrackIndex);
    lastFocusSlotIndex = focusSlotIndex;
    lastSelectedTrackIndex = selectedTrackIndex;
    
    // Track row (notes 60-67)
    for (uint8_t i = 0; i < NUM_TRACK_LEDS && i < Config::NUM_TRACKS; i++) {
        uint8_t velocity = (i == selectedTrackIndex) ? VEL_SELECTED
                         : (trackHasData[i] ? VEL_HAS_DATA : 0);
        uint8_t note = MidiConfig::Led::TRACK_SELECT_LED_BASE + i;
        
        if (lastTrackSelectVelocity[i] != velocity) {
            if (velocity > 0) {
                midiHandler.sendLedFeedbackNoteOn(note, velocity);
            } else {
                midiHandler.sendLedFeedbackNoteOff(note);
            }
            lastTrackSelectVelocity[i] = velocity;
        }
    }
    
    // Loop row (notes 50-57) for selected track: slotVelocities drive the LED directly.
    for (uint8_t i = 0; i < MidiConfig::Led::LOOP_SELECT_LED_COUNT && i < Config::MAX_LOOPS_PER_TRACK; i++) {
        uint8_t velocity = slotVelocities[i];
        uint8_t note = MidiConfig::Led::LOOP_SELECT_LED_BASE + i;

        // Force one-shot resend on focus change. This recovers physical LED state
        // if a prior update was dropped while logical values stayed identical.
        if (focusChanged || trackChanged) {
            lastLoopSelectVelocity[i] = BAR_VEL_NEVER_SENT;
        }

        if (lastLoopSelectVelocity[i] != velocity) {
            if (velocity > 0) {
                midiHandler.sendLedFeedbackNoteOn(note, velocity);
            } else {
                midiHandler.sendLedFeedbackNoteOff(note);
            }
            lastLoopSelectVelocity[i] = velocity;
        }
    }
}

uint32_t MidiLedManager::getCurrentBar(uint32_t currentTick, const Loop& loop, const Track& track,
                                       uint8_t displaySlotIndex) {
    uint32_t ticksPerBar = 16 * Config::TICKS_PER_16TH_STEP;
    const bool alignWithPlaybackCycle =
        displaySlotIndex == track.getActiveLoopIndex() &&
        (track.isPlaying() || track.isOverdubbing());
    const uint32_t tickInLoopStorage =
        alignWithPlaybackCycle
            ? IntervalProjection::tickPhaseInProjectionCycle(
                  currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks)
            : tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    uint32_t tickInLoopDisplay = IntervalProjection::noteRelativeTick(
        tickInLoopStorage, loop.loopStartTick, loop.loopLengthTicks);
    return tickInLoopDisplay / ticksPerBar;
}

uint32_t MidiLedManager::getCurrentBarStartTick(uint32_t currentTick, const Loop& loop,
                                                const Track& track, uint8_t displaySlotIndex) {
    uint32_t ticksPerBar = 16 * Config::TICKS_PER_16TH_STEP;
    uint32_t currentBar = getCurrentBar(currentTick, loop, track, displaySlotIndex);
    return currentBar * ticksPerBar;
}

namespace {

bool noteOnInRange(const MidiEvent& event, uint32_t rangeStart, uint32_t rangeEnd) {
    if (event.type != midi::NoteOn || event.data.noteData.velocity == 0) {
        return false;
    }
    const uint32_t noteTick = event.tick;
    if (rangeStart < rangeEnd) {
        return noteTick >= rangeStart && noteTick < rangeEnd;
    }
    return noteTick >= rangeStart || noteTick < rangeEnd;
}

bool displayNoteStartsInRange(const NoteUtils::DisplayNote& note, uint32_t loopLength,
                            uint32_t rangeStart, uint32_t rangeEnd) {
    if (loopLength == 0) {
        return false;
    }
    const uint32_t startTick = IntervalProjection::tickPhaseInLoop(note.startTick, 0, loopLength);
    if (rangeStart < rangeEnd) {
        return startTick >= rangeStart && startTick < rangeEnd;
    }
    return startTick >= rangeStart || startTick < rangeEnd;
}

bool hasNoteOnInRange(const Loop& loop, uint32_t rangeStart, uint32_t rangeEnd) {
    if (loop.hasPublishedEvents()) {
        if (loop.visualCacheDirty) {
            return false;
        }
        const uint32_t loopLength = loop.loopLengthTicks;
        for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
            if (displayNoteStartsInRange(note, loopLength, rangeStart, rangeEnd)) {
                return true;
            }
        }
    }
    if (loop.captureActive()) {
        const size_t captureCount = loop.capture.store.size();
        for (size_t i = 0; i < captureCount; ++i) {
            const MidiEvent& event = loop.capture.store.at(i);
            if (noteOnInRange(event, rangeStart, rangeEnd)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

bool MidiLedManager::hasNoteInSixteenthStep(const Loop& loop, uint32_t stepStartStorage, uint32_t stepEndStorage) {
    return hasNoteOnInRange(loop, stepStartStorage, stepEndStorage);
}

bool MidiLedManager::hasNoteInBar(const Loop& loop, uint32_t barStartStorage, uint32_t barEndStorage) {
    return hasNoteOnInRange(loop, barStartStorage, barEndStorage);
}

void MidiLedManager::updateBarLeds(const Loop& loop, uint32_t currentBar) {
    const uint32_t loopLength = loop.loopLengthTicks;
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
    
    for (uint8_t i = 0; i < NUM_BAR_LEDS; i++) {
        uint32_t barStartDisplay = i * ticksPerBar;
        uint32_t barEndDisplay = (i + 1) * ticksPerBar;
        uint8_t note = BAR_LED_BASE_NOTE + i;
        
        if (loopLength <= barStartDisplay) {
            // NoteOff when bar is beyond loop (resize smaller, track switch, length edit)
            midiHandler.sendLedFeedbackNoteOff(note);
            lastBarVelocity[i] = BAR_VEL_NEVER_SENT;
        } else {
            // Convert display-space bar to storage-space for note lookup
            uint32_t barStartStorage =
                IntervalProjection::noteStorageTick(barStartDisplay, loop.loopStartTick, loopLength);
            uint32_t barEndStorage =
                IntervalProjection::noteStorageTick(barEndDisplay, loop.loopStartTick, loopLength);
            bool isCurrentBar = (i == currentBar && currentBar < NUM_BAR_LEDS);
            bool hasNotes = hasNoteInBar(loop, barStartStorage, barEndStorage);
            uint8_t velocity = isCurrentBar ? VEL_BAR_CURRENT : (hasNotes ? VEL_BAR_HAS_NOTES : VEL_BAR_USED);
            // Only send NoteOn when velocity changes - no NoteOff during normal playback
            if (lastBarVelocity[i] != velocity) {
                midiHandler.sendLedFeedbackNoteOn(note, velocity);
                lastBarVelocity[i] = velocity;
            }
        }
    }
}

void MidiLedManager::sendLedUpdate(uint8_t ledIndex, bool state) {
    if (ledIndex >= NUM_LEDS) return;
    
    if (state) {
        // Turn LED on
        midiHandler.sendLedFeedbackNoteOn(ledIndex, LED_VELOCITY);
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: LED %d ON", ledIndex);
    } else {
        // Turn LED off
        midiHandler.sendLedFeedbackNoteOff(ledIndex);
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: LED %d OFF", ledIndex);
    }
}

void MidiLedManager::updateCurrentTick(Track& track, uint32_t currentTick, uint8_t displaySlotIndex) {
    const Loop& displayLoop = track.getLoop(displaySlotIndex);
    uint32_t loopLength = displayLoop.loopLengthTicks;
    if (loopLength == 0) return;

    uint32_t ticksPerSixteenth = Config::TICKS_PER_16TH_STEP;
    uint32_t ticksPerBar = ticksPerSixteenth * NUM_LEDS;
    
    // Position in loop relative to loop start (matches OLED playhead when this slot is playing).
    const bool alignWithPlaybackCycle =
        displaySlotIndex == track.getActiveLoopIndex() &&
        (track.isPlaying() || track.isOverdubbing());
    const uint32_t tickInLoopStorage =
        alignWithPlaybackCycle
            ? IntervalProjection::tickPhaseInProjectionCycle(
                  currentTick, track.getProjectionCycleStartTick(), loopLength)
            : tickPhaseInLoop(currentTick, displayLoop.startLoopTick, loopLength);
    uint32_t tickInLoopDisplay = IntervalProjection::noteRelativeTick(
        tickInLoopStorage, displayLoop.loopStartTick, loopLength);
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
            midiHandler.sendLedFeedbackNoteOff(TICK_NOTE_OFFSET + currentTickStep);
        }
        
        // Turn on new tick indicator (notes 16-31)
        midiHandler.sendLedFeedbackNoteOn(TICK_NOTE_OFFSET + newTickStep, TICK_VELOCITY);
        
        currentTickStep = newTickStep;
        
        logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED Manager: Current tick step %d (tick %lu)", 
                   newTickStep, currentTick);
    }
}

void MidiLedManager::analyzeAndUpdateBar(const Loop& loop, uint32_t barStartTickDisplay) {
    uint32_t ticksPerSixteenth = Config::TICKS_PER_16TH_STEP;
    const uint32_t loopLength = loop.loopLengthTicks;
    bool newLedState[NUM_LEDS];
    
    // Analyze each 16th note position (convert display-space bar to storage-space for note lookup)
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t stepStartDisplay = barStartTickDisplay + (i * ticksPerSixteenth);
        uint32_t stepEndDisplay = barStartTickDisplay + ((i + 1) * ticksPerSixteenth);
        uint32_t stepStartStorage =
            IntervalProjection::noteStorageTick(stepStartDisplay, loop.loopStartTick, loopLength);
        uint32_t stepEndStorage =
            IntervalProjection::noteStorageTick(stepEndDisplay, loop.loopStartTick, loopLength);
        
        newLedState[i] = hasNoteInSixteenthStep(loop, stepStartStorage, stepEndStorage);
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