//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopEditManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "LooperState.h"
#include "Loop.h"

namespace {

uint8_t resolveTrackIndex(const Track& track) {
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        if (&trackManager.getTrack(trackIndex) == &track) {
            return trackIndex;
        }
    }
    return trackManager.getSelectedTrackIndex();
}

}  // namespace

LoopEditManager::LoopEditManager(MidiHandler& midiHandler) 
    : midiHandler(midiHandler) {
}

bool LoopEditManager::isLoopEditMode() const {
    return editManager.getEditSessionType() == EditSessionType::Loop;
}

uint8_t LoopEditManager::selectedSlotForTrack(const Track& track) const {
    return trackManager.getSelectedSlotIndex(resolveTrackIndex(track));
}

void LoopEditManager::captureSessionBaseline(Track& track) {
    sessionSlot_ = selectedSlotForTrack(track);
    const Loop& loop = trackManager.getSelectedLoop(track);
    sessionBaselineLoopStart_ = loop.loopStartTick;
    sessionBaselineLoopLength_ = loop.loopLengthTicks;
}

void LoopEditManager::applyLoopStartTick(Track& track, uint32_t startTick) {
    Loop& loop = trackManager.getSelectedLoop(track);
    if (startTick == loop.loopStartTick) {
        return;
    }
    const uint32_t oldStartTick = loop.loopStartTick;
    if (startTick >= loop.loopLengthTicks && loop.loopLengthTicks > 0) {
        startTick = startTick % loop.loopLengthTicks;
    }
    loop.loopStartTick = startTick;
    logger.log(CAT_TRACK, LOG_INFO, "Loop start point changed: %lu -> %lu ticks",
               static_cast<unsigned long>(oldStartTick),
               static_cast<unsigned long>(loop.loopStartTick));
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndex(track),
                                                selectedSlotForTrack(track));
    track.invalidateCaches();
}

void LoopEditManager::applyLoopLength(Track& track, uint32_t loopLengthTicks) {
    Loop& loop = trackManager.getSelectedLoop(track);
    if (loop.loopLengthTicks == loopLengthTicks) {
        return;
    }
    loop.loopLengthTicks = loopLengthTicks;
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndex(track),
                                                selectedSlotForTrack(track));
    track.invalidateCaches();
}

void LoopEditManager::applyLoopLengthWithWrapping(Track& track, uint32_t newLoopLength) {
    Loop& loop = trackManager.getSelectedLoop(track);
    if (newLoopLength == loop.loopLengthTicks) {
        return;
    }
    const uint32_t oldLoopLength = loop.loopLengthTicks;
    logger.log(CAT_TRACK, LOG_INFO, "Loop length change: %lu -> %lu ticks",
               static_cast<unsigned long>(oldLoopLength),
               static_cast<unsigned long>(newLoopLength));
    loop.loopLengthTicks = newLoopLength;
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndex(track),
                                                selectedSlotForTrack(track));
    track.invalidateCaches();
    logger.log(CAT_TRACK, LOG_INFO,
               "Loop length updated to %lu ticks (wrapping handled dynamically)",
               static_cast<unsigned long>(loop.loopLengthTicks));
}

void LoopEditManager::scheduleDebouncedLoopEditSave() {
    pendingLoopEditSaveAtMs = millis() + LOOP_EDIT_SAVE_DEBOUNCE_MS;
}

void LoopEditManager::flushPendingLoopEditWork(Track& track) {
    updateLoopEndpointAfterGracePeriod(track);
    if (pendingLoopEditSaveAtMs == 0) {
        return;
    }
    pendingLoopEditSaveAtMs = 0;
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndex(track), selectedSlotForTrack(track));
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
    logger.log(CAT_MIDI, LOG_DEBUG, "State save queued (loop edit depart flush)");
}

bool LoopEditManager::shouldIgnoreLoopFaderInput() const {
    return feedbackIgnoreUntilMs_ != 0 && millis() < feedbackIgnoreUntilMs_;
}

std::vector<uint32_t> LoopEditManager::buildLoopStartFaderPositions(const Track& track) const {
    const Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t loopLength = loop.loopLengthTicks;
    std::vector<uint32_t> allPositions;
    if (loopLength == 0) {
        allPositions.push_back(0);
        return allPositions;
    }

    const uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    const uint32_t stepCount = numSteps > 0 ? numSteps : 1;

    const auto& notes = track.getVisualNotesForSlot(selectedSlotForTrack(track));
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
    sessionSlot_ = 255;
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Loop edit session left — grace cleared, ignoring loop fader input briefly");
}

void LoopEditManager::reopenLoopEditSession(Track& track) {
    onEnterLoopEditSession(track);
}

void LoopEditManager::onEnterLoopEditSession(Track& track) {
    feedbackIgnoreUntilMs_ = millis() + LOOP_EDIT_FEEDBACK_IGNORE_MS;
    captureSessionBaseline(track);
    sendCurrentLoopStartPitchbend(track);
    sendCurrentLoopLengthCC(track);
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Loop edit session entered — sent loop start/length fader feedback");
}

void LoopEditManager::commitLoopEditOnDepart(Track& track) {
    if (!isLoopEditMode() || sessionSlot_ >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    flushPendingLoopEditWork(track);

    Loop& loop = track.getLoop(sessionSlot_);
    const bool geometryChanged =
        loop.loopStartTick != sessionBaselineLoopStart_ ||
        loop.loopLengthTicks != sessionBaselineLoopLength_;
    if (geometryChanged) {
        TrackUndo::pushLoopGeometryDepartSnapshot(track, sessionSlot_, sessionBaselineLoopStart_,
                                                  sessionBaselineLoopLength_);
        logger.log(CAT_TRACK, LOG_INFO,
                   "Loop geometry committed on depart slot=%u start=%lu->%lu len=%lu->%lu",
                   static_cast<unsigned>(sessionSlot_) + 1u,
                   static_cast<unsigned long>(sessionBaselineLoopStart_),
                   static_cast<unsigned long>(loop.loopStartTick),
                   static_cast<unsigned long>(sessionBaselineLoopLength_),
                   static_cast<unsigned long>(loop.loopLengthTicks));
    }

    sessionSlot_ = 255;
}

void LoopEditManager::sendCurrentLoopStartPitchbend(Track& track) {
    const Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t loopLength = loop.loopLengthTicks;
    if (loopLength == 0) {
        return;
    }

    const std::vector<uint32_t> allPositions = buildLoopStartFaderPositions(track);
    const uint32_t currentStart = loop.loopStartTick % loopLength;

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

    const uint32_t positionSpan = static_cast<uint32_t>(allPositions.size() - 1);
    int16_t pitchbend = static_cast<int16_t>(MidiConfig::Pitchbend::MIN);
    if (positionSpan > 0) {
        pitchbend = static_cast<int16_t>(map(
            bestIndex, 0U, positionSpan,
            static_cast<uint32_t>(MidiConfig::Pitchbend::MIN),
            static_cast<uint32_t>(MidiConfig::Pitchbend::MAX)));
    }

    midiHandler.sendPitchBend(MidiConfig::Fader::SELECT_CHANNEL, pitchbend);
    midiHandler.sendNoteOn(MidiConfig::Fader::SELECT_CHANNEL, 0, 127);
    midiHandler.sendNoteOff(MidiConfig::Fader::SELECT_CHANNEL, 0, 0);
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Sent loop start pitchbend feedback: start=%lu index=%lu pitchbend=%d",
               currentStart, bestIndex, pitchbend);
}


void LoopEditManager::handleLoopStartFaderInput(int16_t pitchValue, Track& track) {
    if (!isLoopEditMode()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: not in LOOP_EDIT mode");
        return;
    }
    if (shouldIgnoreLoopFaderInput()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: session feedback settle");
        return;
    }
    const Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t loopLength = loop.loopLengthTicks;
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: no loop length set");
        return;
    }
    
    uint32_t newLoopStartTick = calculateLoopStartTick(pitchValue, track);
    const uint32_t currentStart = loop.loopStartTick;
    
    if (newLoopStartTick != currentStart && isSignificantMovement(currentStart, newLoopStartTick)) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader: pitchbend=%d -> tick=%lu (significant change)", 
                   pitchValue, newLoopStartTick);
        
        TrackUndo::pushLoopStartSnapshot(track, selectedSlotForTrack(track));
        applyLoopStartTick(track, newLoopStartTick);
        logger.log(CAT_MIDI, LOG_INFO, "LOOP START EDIT: Loop start moved from tick %lu to %lu", 
                   currentStart, newLoopStartTick);
        
        scheduleDebouncedLoopEditSave();
        refreshLoopStartEditingActivity();
        loopStartEditingTime = millis();
        loopStartEditingEnabled = true;
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader: pitchbend=%d -> tick=%lu (filtered - small change)", 
                   pitchValue, newLoopStartTick);
    }
}

uint32_t LoopEditManager::calculateLoopStartTick(int16_t pitchValue, Track& track) {
    const std::vector<uint32_t> allPositions = buildLoopStartFaderPositions(track);
    if (allPositions.empty()) {
        return 0;
    }
    const uint32_t positionSpan = static_cast<uint32_t>(allPositions.size() - 1);
    const uint32_t targetIndex = positionSpan > 0
                                     ? static_cast<uint32_t>(map(
                                           pitchValue, MidiConfig::Pitchbend::MIN,
                                           MidiConfig::Pitchbend::MAX, 0U, positionSpan))
                                     : 0U;
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
    
    if (loopStartEditingTime > 0 && (now - loopStartEditingTime) >= LOOP_START_GRACE_PERIOD) {
        const Loop& loop = trackManager.getSelectedLoop(track);
        const uint32_t loopLength = loop.loopLengthTicks;
        const uint32_t loopStartTick = loop.loopStartTick;
        
        uint32_t loopLengthBars = (loopLength + (Config::TICKS_PER_BAR / 2)) / Config::TICKS_PER_BAR;
        if (loopLengthBars == 0) loopLengthBars = 1;
        
        const uint32_t newLoopEndTick = loopStartTick + (loopLengthBars * Config::TICKS_PER_BAR);
        const uint32_t newLoopLength = loopLengthBars * Config::TICKS_PER_BAR;
        
        if (newLoopLength != loopLength) {
            applyLoopLength(track, newLoopLength);
            logger.log(CAT_MIDI, LOG_INFO, "LOOP ENDPOINT UPDATE: Loop length adjusted from %lu to %lu ticks (%lu bars)", 
                       loopLength, newLoopLength, loopLengthBars);
            
            scheduleDebouncedLoopEditSave();
        }
        
        logger.log(CAT_MIDI, LOG_INFO, "LOOP ENDPOINT UPDATE: Grace period ended, loop end=%lu (start=%lu + %lu bars)", 
                   newLoopEndTick, loopStartTick, loopLengthBars);
        
        loopStartEditingTime = 0;
        loopStartEditingEnabled = false;
    }
}

void LoopEditManager::handleLoopLengthInput(uint8_t ccValue, Track& track) {
    if (!isLoopEditMode()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length input ignored: not in LOOP_EDIT mode");
        return;
    }
    if (shouldIgnoreLoopFaderInput()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length input ignored: session feedback settle");
        return;
    }
    
    const uint32_t newLoopLengthTicks = calculateLoopLengthFromCC(ccValue);
    const Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t currentLoopLength = loop.loopLengthTicks;
    const uint32_t currentBars = (currentLoopLength > 0) ? (currentLoopLength / Config::TICKS_PER_BAR) : 0;
    const uint32_t newBars = newLoopLengthTicks / Config::TICKS_PER_BAR;
    
    if (newLoopLengthTicks != currentLoopLength) {
        logger.log(CAT_MIDI, LOG_INFO, "LOOP EDIT: Changing loop length from %lu bars (%lu ticks) to %lu bars (%lu ticks)", 
                   currentBars, currentLoopLength, newBars, newLoopLengthTicks);
        
        applyLoopLengthWithWrapping(track, newLoopLengthTicks);

        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length updated successfully: CC=%d -> %lu bars (%lu ticks)", 
                   ccValue, newBars, newLoopLengthTicks);
        
        scheduleDebouncedLoopEditSave();
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length unchanged: CC=%d maps to current length (%lu bars)", 
                   ccValue, newBars);
    }
}

uint32_t LoopEditManager::calculateLoopLengthFromCC(uint8_t ccValue) {
    uint8_t bars = map(ccValue, 0, 127, 1, 128);
    return bars * Config::TICKS_PER_BAR;
}

void LoopEditManager::sendCurrentLoopLengthCC(Track& track) {
    const uint32_t currentLoopLength = trackManager.getSelectedLoop(track).loopLengthTicks;
    
    if (currentLoopLength == 0) {
        midiHandler.sendControlChange(MidiConfig::LoopEdit::LENGTH_CC_CHANNEL, MidiConfig::LoopEdit::LENGTH_CC_NUMBER, 0);
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)");
        return;
    }
    
    uint8_t ccValue = calculateCCFromLoopLength(currentLoopLength);
    midiHandler.sendControlChange(MidiConfig::LoopEdit::LENGTH_CC_CHANNEL, MidiConfig::LoopEdit::LENGTH_CC_NUMBER, ccValue);
    
    uint32_t currentBars = currentLoopLength / Config::TICKS_PER_BAR;
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC feedback: %lu bars (%lu ticks) -> CC=%d", 
               currentBars, currentLoopLength, ccValue);
}

uint8_t LoopEditManager::calculateCCFromLoopLength(uint32_t loopLength) {
    uint32_t currentBars = loopLength / Config::TICKS_PER_BAR;
    if (currentBars < 1) currentBars = 1;
    if (currentBars > 128) currentBars = 128;
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
            const uint8_t trackIndex = trackManager.getSelectedTrackIndex();
            StorageManager::markCurrentSetLoopSlotDirty(trackIndex,
                                                        trackManager.getSelectedSlotIndex(trackIndex));
            StorageManager::requestDeferredSaveState(looperState.getLooperState());
            logger.log(CAT_MIDI, LOG_DEBUG, "State save queued (debounced after loop edit)");
        }
    }
    if (loopStartEditingTime > 0 && isLoopEditMode()) {
        Track& track = trackManager.getSelectedTrack();
        updateLoopEndpointAfterGracePeriod(track);
    }
}
