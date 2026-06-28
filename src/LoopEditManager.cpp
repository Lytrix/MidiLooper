//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopEditManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "LooperState.h"

LoopEditManager::LoopEditManager(MidiHandler& midiHandler) 
    : midiHandler(midiHandler) {
}

bool LoopEditManager::isLoopEditMode() const {
    return editManager.getEditSessionType() == EditSessionType::Loop;
}

void LoopEditManager::scheduleDebouncedLoopEditSave() {
    pendingLoopEditSaveAtMs = millis() + LOOP_EDIT_SAVE_DEBOUNCE_MS;
}

bool LoopEditManager::shouldIgnoreLoopFaderInput() const {
    return feedbackIgnoreUntilMs_ != 0 && millis() < feedbackIgnoreUntilMs_;
}

std::vector<uint32_t> LoopEditManager::buildLoopStartFaderPositions(const Track& track) const {
    const uint32_t loopLength = track.getLoopLength();
    std::vector<uint32_t> allPositions;
    if (loopLength == 0) {
        allPositions.push_back(0);
        return allPositions;
    }

    const uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    const uint32_t stepCount = numSteps > 0 ? numSteps : 1;

    const auto& notes = track.getCachedNotes();
    for (const auto& note : notes) {
        allPositions.push_back(note.startTick % loopLength);
    }
    for (uint32_t step = 0; step < stepCount; step++) {
        allPositions.push_back(step * Config::TICKS_PER_16TH_STEP);
    }

    std::sort(allPositions.begin(), allPositions.end());
    allPositions.erase(std::unique(allPositions.begin(), allPositions.end()),
                       allPositions.end());
    if (allPositions.empty()) {
        allPositions.push_back(0);
    }
    return allPositions;
}

void LoopEditManager::onLeaveLoopEditSession() {
    loopStartEditingTime = 0;
    loopStartEditingEnabled = false;
    feedbackIgnoreUntilMs_ = millis() + LOOP_EDIT_FEEDBACK_IGNORE_MS;
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Loop edit session left — grace cleared, ignoring loop fader input briefly");
}

void LoopEditManager::onEnterLoopEditSession(Track& track) {
    feedbackIgnoreUntilMs_ = millis() + LOOP_EDIT_FEEDBACK_IGNORE_MS;
    sendCurrentLoopStartPitchbend(track);
    sendCurrentLoopLengthCC(track);
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Loop edit session entered — sent loop start/length fader feedback");
}

void LoopEditManager::sendCurrentLoopStartPitchbend(Track& track) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    const std::vector<uint32_t> allPositions = buildLoopStartFaderPositions(track);
    const uint32_t currentStart = track.getLoopStartTick() % loopLength;

    uint32_t bestIndex = 0;
    for (size_t i = 0; i < allPositions.size(); ++i) {
        if (allPositions[i] == currentStart) {
            bestIndex = static_cast<uint32_t>(i);
            break;
        }
        if (allPositions[i] < currentStart) {
            bestIndex = static_cast<uint32_t>(i);
        }
    }

    const int16_t pitchbend = static_cast<int16_t>(map(
        bestIndex, 0U, static_cast<uint32_t>(allPositions.size() - 1),
        static_cast<uint32_t>(MidiConfig::Pitchbend::MIN),
        static_cast<uint32_t>(MidiConfig::Pitchbend::MAX)));

    midiHandler.sendPitchBend(MidiConfig::Fader::SELECT_CHANNEL, pitchbend);
    midiHandler.sendNoteOn(MidiConfig::Fader::SELECT_CHANNEL, 0, 127);
    midiHandler.sendNoteOff(MidiConfig::Fader::SELECT_CHANNEL, 0, 0);
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Sent loop start pitchbend feedback: start=%lu index=%lu pitchbend=%d",
               currentStart, bestIndex, pitchbend);
}


void LoopEditManager::handleLoopStartFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in LOOP_EDIT mode
    if (!isLoopEditMode()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: not in LOOP_EDIT mode");
        return;
    }
    if (shouldIgnoreLoopFaderInput()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: session feedback settle");
        return;
    }
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: no loop length set");
        return;
    }
    
    // Calculate new loop start position
    uint32_t newLoopStartTick = calculateLoopStartTick(pitchValue, track);
    uint32_t currentStart = track.getLoopStartTick();
    
    // Only process if this is a significant change
    if (newLoopStartTick != currentStart && isSignificantMovement(currentStart, newLoopStartTick)) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader: pitchbend=%d -> tick=%lu (significant change)", 
                   pitchValue, newLoopStartTick);
        
        // Push undo snapshot before making the change
        TrackUndo::pushLoopStartSnapshot(track);
        
        // Set the new loop start point
        track.setLoopStartTick(newLoopStartTick);

        // LEDs: rely on TrackManager::updateLedsDeferred (~8 ms) — it detects loopStartTick
        // changes and refreshes without forceUpdate, avoiding DROID USB MIDI floods.

        logger.log(CAT_MIDI, LOG_INFO, "LOOP START EDIT: Loop start moved from tick %lu to %lu", 
                   currentStart, newLoopStartTick);
        
        scheduleDebouncedLoopEditSave();
        
        // Mark editing activity to enable grace period and endpoint updating
        refreshLoopStartEditingActivity();
        
        // Enable loop start editing mode with grace period
        loopStartEditingTime = millis();
        loopStartEditingEnabled = true;
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader: pitchbend=%d -> tick=%lu (filtered - small change)", 
                   pitchValue, newLoopStartTick);
    }
}

uint32_t LoopEditManager::calculateLoopStartTick(int16_t pitchValue, Track& track) {
    const std::vector<uint32_t> allPositions = buildLoopStartFaderPositions(track);
    const uint32_t targetIndex = map(
        pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0U,
        static_cast<uint32_t>(allPositions.size() - 1));
    return allPositions[targetIndex];
}

bool LoopEditManager::isSignificantMovement(uint32_t currentStart, uint32_t newStart) {
    uint32_t movementDelta = (newStart > currentStart) ? 
        (newStart - currentStart) : (currentStart - newStart);
    
    return movementDelta >= Config::TICKS_PER_16TH_STEP / 4;
}

void LoopEditManager::refreshLoopStartEditingActivity() {
    lastLoopStartEditingActivityTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Loop start editing activity refreshed");
}

void LoopEditManager::updateLoopEndpointAfterGracePeriod(Track& track) {
    if (!isLoopEditMode()) {
        loopStartEditingTime = 0;
        loopStartEditingEnabled = false;
        return;
    }

    uint32_t now = millis();
    
    // Check if grace period has passed
    if (loopStartEditingTime > 0 && (now - loopStartEditingTime) >= LOOP_START_GRACE_PERIOD) {
        // Grace period has passed, update the loop endpoint based on bars relative to start point
        uint32_t loopLength = track.getLoopLength();
        uint32_t loopStartTick = track.getLoopStartTick();
        
        // Calculate loop length in bars (round to nearest bar)
        uint32_t loopLengthBars = (loopLength + (Config::TICKS_PER_BAR / 2)) / Config::TICKS_PER_BAR;
        if (loopLengthBars == 0) loopLengthBars = 1;
        
        // Calculate new loop end based on start + bars
        uint32_t newLoopEndTick = loopStartTick + (loopLengthBars * Config::TICKS_PER_BAR);
        uint32_t newLoopLength = loopLengthBars * Config::TICKS_PER_BAR;
        
        // Update the loop length to maintain the bar-based length relative to new start
        if (newLoopLength != loopLength) {
            track.setLoopLength(newLoopLength);
            logger.log(CAT_MIDI, LOG_INFO, "LOOP ENDPOINT UPDATE: Loop length adjusted from %lu to %lu ticks (%lu bars)", 
                       loopLength, newLoopLength, loopLengthBars);
            
            scheduleDebouncedLoopEditSave();
        }
        
        logger.log(CAT_MIDI, LOG_INFO, "LOOP ENDPOINT UPDATE: Grace period ended, loop end=%lu (start=%lu + %lu bars)", 
                   newLoopEndTick, loopStartTick, loopLengthBars);
        
        // Reset grace period state
        loopStartEditingTime = 0;
        loopStartEditingEnabled = false;
    }
}

void LoopEditManager::handleLoopLengthInput(uint8_t ccValue, Track& track) {
    // Only process loop length input when in LOOP_EDIT mode
    if (!isLoopEditMode()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length input ignored: not in LOOP_EDIT mode");
        return;
    }
    if (shouldIgnoreLoopFaderInput()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length input ignored: session feedback settle");
        return;
    }
    
    // Convert CC value to loop length
    uint32_t newLoopLengthTicks = calculateLoopLengthFromCC(ccValue);
    
    // Get current loop length for comparison
    uint32_t currentLoopLength = track.getLoopLength();
    uint32_t currentBars = (currentLoopLength > 0) ? (currentLoopLength / Config::TICKS_PER_BAR) : 0;
    uint32_t newBars = newLoopLengthTicks / Config::TICKS_PER_BAR;
    
    // Only update if the length actually changes
    if (newLoopLengthTicks != currentLoopLength) {
        logger.log(CAT_MIDI, LOG_INFO, "LOOP EDIT: Changing loop length from %lu bars (%lu ticks) to %lu bars (%lu ticks)", 
                   currentBars, currentLoopLength, newBars, newLoopLengthTicks);
        
        // Set the new loop length with proper note wrapping
        track.setLoopLengthWithWrapping(newLoopLengthTicks);

        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length updated successfully: CC=%d -> %lu bars (%lu ticks)", 
                   ccValue, newBars, newLoopLengthTicks);
        
        scheduleDebouncedLoopEditSave();
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length unchanged: CC=%d maps to current length (%lu bars)", 
                   ccValue, newBars);
    }
}

uint32_t LoopEditManager::calculateLoopLengthFromCC(uint8_t ccValue) {
    // Convert CC value (0-127) to bars (1-128)
    // CC 0 = 1 bar, CC 127 = 128 bars
    uint8_t bars = map(ccValue, 0, 127, 1, 128);
    
    // Convert bars to ticks
    return bars * Config::TICKS_PER_BAR;
}

void LoopEditManager::sendCurrentLoopLengthCC(Track& track) {
    // Convert current loop length back to CC value for fader feedback
    uint32_t currentLoopLength = track.getLoopLength();
    
    if (currentLoopLength == 0) {
        // No loop set yet, send CC for 1 bar
        midiHandler.sendControlChange(MidiConfig::LoopEdit::LENGTH_CC_CHANNEL, MidiConfig::LoopEdit::LENGTH_CC_NUMBER, 0);  // CC 0 = 1 bar
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)");
        return;
    }
    
    uint8_t ccValue = calculateCCFromLoopLength(currentLoopLength);
    
    // Send CC feedback
    midiHandler.sendControlChange(MidiConfig::LoopEdit::LENGTH_CC_CHANNEL, MidiConfig::LoopEdit::LENGTH_CC_NUMBER, ccValue);
    
    uint32_t currentBars = currentLoopLength / Config::TICKS_PER_BAR;
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC feedback: %lu bars (%lu ticks) -> CC=%d", 
               currentBars, currentLoopLength, ccValue);
}

uint8_t LoopEditManager::calculateCCFromLoopLength(uint32_t loopLength) {
    // Convert ticks back to bars
    uint32_t currentBars = loopLength / Config::TICKS_PER_BAR;
    
    // Clamp to valid range (1-128 bars)
    if (currentBars < 1) currentBars = 1;
    if (currentBars > 128) currentBars = 128;
    
    // Convert bars back to CC value (1-128 bars -> 0-127 CC)
    return map(currentBars, 1, 128, 0, 127);
}

void LoopEditManager::onTrackChanged(Track& newTrack) {
    if (isLoopEditMode()) {
        onEnterLoopEditSession(newTrack);
        logger.log(CAT_MIDI, LOG_DEBUG, "Track changed while in loop edit mode, updating loop faders");
    }
}

void LoopEditManager::update() {
    if (pendingLoopEditSaveAtMs != 0) {
        uint32_t now = millis();
        if ((int32_t)(now - pendingLoopEditSaveAtMs) >= 0) {
            pendingLoopEditSaveAtMs = 0;
            StorageManager::markCurrentSetLoopSlotDirty(trackManager.getSelectedTrackIndex(),
                                                        trackManager.getSelectedTrack().getActiveLoopIndex());
            StorageManager::requestDeferredSaveState(looperState.getLooperState());
            logger.log(CAT_MIDI, LOG_DEBUG, "State save queued (debounced after loop edit)");
        }
    }
    // Check for grace period updates (only while LOOP_EDIT is active)
    if (loopStartEditingTime > 0 && isLoopEditMode()) {
        Track& track = trackManager.getSelectedTrack();
        updateLoopEndpointAfterGracePeriod(track);
    }
} 