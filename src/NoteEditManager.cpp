//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstdint>
#include <set>
#include <algorithm>
#include "Globals.h"
#include "NoteEditManager.h"

#include "ClockManager.h"
#include "TrackManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "EditManager.h"
#include "EditStates/EditLengthNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Utils/NoteUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/ValidationUtils.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/MidiMapping.h"
#include "MidiFaderManagerV2.h"
#include "MidiFaderProcessor.h"

NoteEditManager noteEditManager;

NoteEditManager::NoteEditManager() {
    // Initialize the fader states array with all 4 faders
    initializeFaderStates();
}

// Delegate MIDI note handling to V2 system
void NoteEditManager::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    buttonHandler.handleMidiNote(channel, note, velocity, isNoteOn);
}

void NoteEditManager::update() {
    uint32_t now = millis();
    
    // Handle pending selectnote fader updates
    if (pendingSelectnoteUpdate && now >= selectnoteUpdateTime) {
        pendingSelectnoteUpdate = false;
        Track& track = trackManager.getSelectedTrack();
        sendSelectnoteFaderUpdate(track);
    }
    
    // Handle start editing grace period - re-enable note editing faders after grace period
    if (!startEditingEnabled && noteSelectionTime > 0) {
        enableStartEditing();
    }
    
    // Handle loop start editing grace period and endpoint updating
    if (loopStartEditingEnabled && currentMainEditMode == MAIN_MODE_LOOP_EDIT) {
        Track& track = trackManager.getSelectedTrack();
        updateLoopEndpointAfterGracePeriod(track);
    }
    
    // Delegate to V2 managers for their update cycles
    faderHandler.update();
    buttonHandler.update();
}

void NoteEditManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    // Log all pitchbend messages for debugging
    logger.log(CAT_MIDI, LOG_DEBUG, "Received pitchbend: ch=%d value=%d", channel, pitchValue);
    
    // Route channel 16 based on current edit mode
    if (channel == PITCHBEND_SELECT_CHANNEL) {  // Channel 16
        if (currentMainEditMode == MAIN_MODE_LOOP_EDIT) {
            // In loop edit mode: Route to loop start fader
            handleLoopStartFaderInput(pitchValue, trackManager.getSelectedTrack());
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ch=%d routed to loop start fader (LOOP_EDIT mode)", channel);
            return;
        } else {
            // In note edit mode: Route to select fader
            handleFaderInput(MidiMapping::FaderType::FADER_SELECT, pitchValue, 0);
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ch=%d routed to select fader (NOTE_EDIT mode)", channel);
            return;
        }
    } else if (channel == PITCHBEND_START_CHANNEL) {  // Channel 15
        handleFaderInput(MidiMapping::FaderType::FADER_COARSE, pitchValue, 0);
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ignored: not on monitored channels (%d or %d)", 
               PITCHBEND_SELECT_CHANNEL, PITCHBEND_START_CHANNEL);
}

void NoteEditManager::handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    logger.log(CAT_MIDI, LOG_DEBUG, "Received CC: ch=%d cc=%d value=%d", channel, ccNumber, value);
    
    // Check for loop length control first (CC 101 on channel 16)
    if (channel == 16 && ccNumber == 101) {
        handleLoopLengthInput(value, trackManager.getSelectedTrack());
        return;
    }
    
    // Route to unified fader system
    if (channel == FINE_CC_CHANNEL && ccNumber == FINE_CC_NUMBER) {
        handleFaderInput(MidiMapping::FaderType::FADER_FINE, 0, value);
        return;
    } else if (channel == NOTE_VALUE_CC_CHANNEL && ccNumber == NOTE_VALUE_CC_NUMBER) {
        handleFaderInput(MidiMapping::FaderType::FADER_NOTE_VALUE, 0, value);
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "CC ignored: not on monitored channels/CC (%d/%d, %d/%d, or 16/101)", 
               FINE_CC_CHANNEL, FINE_CC_NUMBER, NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER);
}

void NoteEditManager::moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick) {
    // Use the enhanced version with overlap handling
    moveNoteToPositionWithOverlapHandling(track, currentNote, targetTick, false);
}

void NoteEditManager::moveNoteToPositionWithOverlapHandling(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick, bool commitChanges) {
    // Calculate movement delta for the utility function
    int32_t tickDifference = (int32_t)targetTick - (int32_t)currentNote.startTick;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Note movement with overlap handling: from=%lu to=%lu difference=%ld commit=%s deletedNotes=%zu", 
               currentNote.startTick, targetTick, tickDifference, commitChanges ? "true" : "false", 
               editManager.movingNote.deletedNotes.size());
    
    // If committing changes, permanently delete overlapping notes
    if (commitChanges) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Committing note movement - permanently deleting %zu overlapping notes", 
                   editManager.movingNote.deletedNotes.size());
        
        // Clear the deleted notes list (they're now permanently deleted)
        editManager.movingNote.deletedNotes.clear();
        editManager.movingNote.active = false;
        
        // Just move the note to final position
        moveNoteToPositionSimple(track, currentNote, targetTick);
        return;
    }
    
    // Ensure the moving note is properly initialized (but don't switch to different notes!)
    if (!editManager.movingNote.active) {
        editManager.movingNote.note = currentNote.note;
        editManager.movingNote.origStart = currentNote.startTick;
        editManager.movingNote.origEnd = currentNote.endTick;
        editManager.movingNote.lastStart = currentNote.startTick;
        editManager.movingNote.lastEnd = currentNote.endTick;
        editManager.movingNote.active = true;
        editManager.movingNote.movementDirection = 0;
        editManager.movingNote.deletedNotes.clear();
        logger.log(CAT_MIDI, LOG_DEBUG, "Initialized moving note: pitch=%d, start=%lu, end=%lu", 
                   currentNote.note, currentNote.startTick, currentNote.endTick);
    }
    
    // Use the proven overlap handling logic from the encoder
    NoteMovementUtils::moveNoteWithOverlapHandling(track, editManager, currentNote, targetTick, tickDifference);
}

void NoteEditManager::moveNoteToPositionSimple(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick) {
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    
    // Find and update both note start and end positions to maintain duration
    bool noteStartUpdated = false;
    bool noteEndUpdated = false;
    uint32_t newEndTick = targetTick + (currentNote.endTick - currentNote.startTick);
    
    // Constrain the new end tick to stay within the loop
    if (newEndTick >= loopLength) {
        newEndTick = newEndTick % loopLength;
    }
    
    // Find the specific note-on event for this note
    MidiEvent* noteOnEvent = nullptr;
    for (auto& event : midiEvents) {
        if (event.type == midi::NoteOn && 
            event.data.noteData.note == currentNote.note &&
            event.tick == currentNote.startTick &&
            event.data.noteData.velocity > 0) {
            noteOnEvent = &event;
            break;
        }
    }
    
    if (!noteOnEvent) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Failed to find note-on event for pitch=%d, start=%lu", 
                   currentNote.note, currentNote.startTick);
        return;
    }
    
    // Find the corresponding note-off event using LIFO pairing logic
    MidiEvent* noteOffEvent = findCorrespondingNoteOff(midiEvents, noteOnEvent, currentNote.note, currentNote.startTick, currentNote.endTick);
    
    if (!noteOffEvent) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Failed to find corresponding note-off event for pitch=%d, start=%lu, end=%lu", 
                   currentNote.note, currentNote.startTick, currentNote.endTick);
        return;
    }
    
    // Update both events atomically
    logger.log(CAT_MIDI, LOG_DEBUG, "Moving note: pitch=%d from start=%lu,end=%lu to start=%lu,end=%lu", 
               currentNote.note, currentNote.startTick, currentNote.endTick, targetTick, newEndTick);
    
    noteOnEvent->tick = targetTick;
    noteOffEvent->tick = newEndTick;
    noteStartUpdated = true;
    noteEndUpdated = true;
    
    if (noteStartUpdated && noteEndUpdated) {
        // Update the bracket to follow the note
        editManager.setBracketTick(targetTick);
        uint32_t noteDuration = currentNote.endTick - currentNote.startTick;
        logger.log(CAT_MIDI, LOG_DEBUG, "Note moved successfully: start=%lu end=%lu duration=%lu ticks", 
                   targetTick, newEndTick, noteDuration);
        
        // CRITICAL: Update the selectedNoteIdx to point to the moved note in the new reconstructed list
        const auto& updatedNotes = track.getCachedNotes();
        int newSelectedIdx = -1;
        
        // Find the moved note in the updated notes list
        for (int i = 0; i < (int)updatedNotes.size(); i++) {
            if (updatedNotes[i].note == currentNote.note && 
                updatedNotes[i].startTick == targetTick &&
                updatedNotes[i].endTick == newEndTick) {
                newSelectedIdx = i;
                break;
            }
        }
        
        if (newSelectedIdx >= 0) {
            int oldSelectedIdx = editManager.getSelectedNoteIdx();
            editManager.setSelectedNoteIdx(newSelectedIdx);
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note at new position)", 
                       oldSelectedIdx, newSelectedIdx);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in reconstructed list");
        }
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Failed to update note: start=%s end=%s", 
                   noteStartUpdated ? "OK" : "FAILED", noteEndUpdated ? "OK" : "FAILED");
    }
}

// void NoteEditManager::processEncoderMovement(int rawDelta) {
//     if (rawDelta == 0) return;
    
//     uint32_t now = millis();
//     uint32_t interval = now - lastEncoderTime;
//     lastEncoderTime = now;
    
//     // Apply acceleration based on timing and current edit state
//     int accel = 1;
//     if (editManager.getCurrentState() == editManager.getStartNoteState()) {
//         if (interval < 25) accel = 24;
//         else if (interval < 50) accel = 8;
//         else if (interval < 100) accel = 4;
//     } else if (editManager.getCurrentState() == editManager.getLengthNoteState()) {
//         // Length editing: use moderate acceleration
//         if (interval < 25) accel = 8;
//         else if (interval < 50) accel = 4;
//         else if (interval < 100) accel = 2;
//     } else if (editManager.getCurrentState() == editManager.getPitchNoteState()) {
//         // Pitch editing: slower acceleration for precision
//         if (interval < 50) accel = 4;
//         else if (interval < 75) accel = 3;
//         else if (interval < 100) accel = 2;
//     } else {
//         // Default edit mode acceleration
//         if (interval < 50) accel = 4;
//         else if (interval < 75) accel = 3;
//         else if (interval < 100) accel = 2;
//     }
    
//     int finalDelta = rawDelta * accel;
    
//     if (editManager.getCurrentState() != nullptr) {
//         // In edit mode: encoder changes value
//         editManager.onEncoderTurn(trackManager.getSelectedTrack(), finalDelta);
//         logger.log(CAT_MIDI, LOG_DEBUG, "[EDIT] MIDI Encoder value change: %d (accel=%d, raw=%d, state=%s)", 
//                    finalDelta, accel, rawDelta, editManager.getCurrentState()->getName());
//     } else {
//         // Not in edit mode - just log for debug
//         logger.log(CAT_MIDI, LOG_DEBUG, "MIDI Encoder delta: %d (not in edit mode)", finalDelta);
//     }
    
//     // Update encoder position for consistency
//     midiEncoderPosition += rawDelta;
// }

void NoteEditManager::cycleEditMode(Track& track) {
    // Use the main edit mode system instead of the old complex state system
    cycleMainEditMode(track);
}

void NoteEditManager::cycleMainEditMode(Track& track) {
    // Toggle between the two modes
    currentMainEditMode = (currentMainEditMode == MAIN_MODE_NOTE_EDIT) ? 
                          MAIN_MODE_LOOP_EDIT : MAIN_MODE_NOTE_EDIT;
    
    // Send the mode change
    sendMainEditModeChange(currentMainEditMode);
    
    logger.log(CAT_MIDI, LOG_INFO, "Cycled to mode: %s", 
               (currentMainEditMode == MAIN_MODE_NOTE_EDIT) ? "NOTE_EDIT" : "LOOP_EDIT");
}

void NoteEditManager::enterNextEditMode(Track& track) {
    cycleEditMode(track);
}

void NoteEditManager::deleteSelectedNote(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Use cached notes to find the selected one
    const auto& notes = track.getCachedNotes();
    if (editManager.getSelectedNoteIdx() >= (int)notes.size()) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return;
    }
    
    auto& selectedNote = notes[editManager.getSelectedNoteIdx()];
    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t noteEnd = selectedNote.endTick;
    
    logger.info("MIDI Encoder: Deleting note pitch=%d, start=%lu, end=%lu", 
                notePitch, noteStart, noteEnd);
    
    // Push undo snapshot before deletion
    TrackUndo::pushUndoSnapshot(track);
    
    // Remove NoteOn and NoteOff events
    auto it = midiEvents.begin();
    int deletedCount = 0;
    while (it != midiEvents.end()) {
        bool matchOn = (it->type == midi::NoteOn && it->data.noteData.velocity > 0 && 
                       it->data.noteData.note == notePitch && it->tick == noteStart);
        bool matchOff = ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                        it->data.noteData.note == notePitch && it->tick == noteEnd);
        
        if (matchOn || matchOff) {
            it = midiEvents.erase(it);
            deletedCount++;
        } else {
            ++it;
        }
    }
    
    logger.info("MIDI Encoder: Deleted %d MIDI events for note", deletedCount);
    
    // Since we're using dedicated faders now, we don't need to manage complex edit modes
    // Just send the current main edit mode to keep the system synchronized
    sendMainEditModeChange(currentMainEditMode);
    
    logger.info("MIDI Encoder: Note deleted, maintaining current edit mode");
}

void NoteEditManager::sendMainEditModeChange(MainEditMode mode) {
    uint8_t program;
    uint8_t triggerNote;
    const char* modeName;
    
    switch (mode) {
        case MAIN_MODE_LOOP_EDIT:
            program = 2;
            triggerNote = 100;
            modeName = "LOOP_EDIT";
            break;
        case MAIN_MODE_NOTE_EDIT:
            program = 1;
            triggerNote = 0;
            modeName = "NOTE_EDIT";
            break;
        default:
            logger.log(CAT_MIDI, LOG_ERROR, "Unknown main edit mode: %d", static_cast<int>(mode));
            return;
    }
    
    // Send program change on channel 16
    midiHandler.sendProgramChange(PROGRAM_CHANGE_CHANNEL, program);
    
    // Send trigger note on channel 16
    midiHandler.sendNoteOn(15, triggerNote, 64);
    delay(10);  // Short note duration
    midiHandler.sendNoteOff(15, triggerNote, 0);
    
    logger.log(CAT_MIDI, LOG_INFO, "Main Edit Mode: %s (Program %d, Note %d trigger)", 
               modeName, program, triggerNote);
    
    // If switching to LOOP_EDIT mode, send current loop length as CC feedback
    if (mode == MAIN_MODE_LOOP_EDIT) {
        sendCurrentLoopLengthCC(trackManager.getSelectedTrack());
    }
}

void NoteEditManager::sendStartNotePitchbend(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for start pitchbend");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Use the SAME tick value as EditSelectNoteState::sendTargetPitchbend for consistency
    // This ensures both fader 1 and fader 2 use the same reference position
    uint32_t bracketTick = editManager.getBracketTick();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // Convert absolute note position to relative position (same as selection system)
        uint32_t noteStartTick = notes[selectedIdx].startTick;
        uint32_t relativeNoteStartTick = (noteStartTick >= loopStartTick) ? 
            (noteStartTick - loopStartTick) : (noteStartTick + loopLength - loopStartTick);
        relativeNoteStartTick = relativeNoteStartTick % loopLength;
        
        // Also convert bracket tick to relative position
        uint32_t relativeBracketTick = (bracketTick >= loopStartTick) ? 
            (bracketTick - loopStartTick) : (bracketTick + loopLength - loopStartTick);
        relativeBracketTick = relativeBracketTick % loopLength;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fader 2 position sync: bracketTick=%lu (rel=%lu), noteStartTick=%lu (rel=%lu), diff=%ld", 
                   bracketTick, relativeBracketTick, noteStartTick, relativeNoteStartTick, 
                   (int32_t)relativeBracketTick - (int32_t)relativeNoteStartTick);
        
        // For fader 2, use a simpler 16th-step based calculation that matches the hardware expectations
        uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
        
        // Find which 16th step the relative bracket tick is closest to
        float stepPosition = (float)relativeBracketTick / (float)Config::TICKS_PER_16TH_STEP;
        uint32_t nearestStep = (uint32_t)(stepPosition + 0.5f);  // Round to nearest step
        if (nearestStep >= numSteps) nearestStep = numSteps - 1;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fader 2 calculation: relativeBracketTick=%lu, stepPos=%.2f, nearestStep=%lu/%lu", 
                   relativeBracketTick, stepPosition, nearestStep, numSteps);
        
        if (numSteps > 1) {
            // COARSE FADER (Channel 15): Map 16th step position to pitchbend range  
            const int16_t PITCHBEND_MIN = -8192;
            const int16_t PITCHBEND_MAX = 8191;
            
            // Map step position to pitchbend range
            float normalizedPos = (float)nearestStep / (float)(numSteps - 1);  // 0.0 to 1.0
            int16_t coarseMidiPitchbend = (int16_t)(PITCHBEND_MIN + normalizedPos * (PITCHBEND_MAX - PITCHBEND_MIN));
            coarseMidiPitchbend = constrain(coarseMidiPitchbend, PITCHBEND_MIN, PITCHBEND_MAX);
            
            logger.log(CAT_MIDI, LOG_DEBUG, "SENDING COARSE PITCHBEND: ch=%d bracketTick=%lu step=%lu/%lu pitchbend=%d", 
                       PITCHBEND_START_CHANNEL, bracketTick, nearestStep, numSteps, coarseMidiPitchbend);
            
            // Send coarse position to channel 15
            midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);
            
            // FINE CC2 (Channel 15): Position within 127 steps around 16th step center
            // IMPORTANT: Don't send CC values to fader 3 when it was recently the driver
            uint32_t now = millis();
            uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
            bool shouldSendFineCC = true;
            
            if (currentDriverFader == MidiMapping::FaderType::FADER_FINE && timeSinceDriverSet < 5000) {
                shouldSendFineCC = false;
                logger.log(CAT_MIDI, LOG_DEBUG, "Skipping legacy fine CC update - fader 3 was recently the driver (%lu ms ago)", 
                           timeSinceDriverSet);
            }
            
            if (shouldSendFineCC) {
            uint32_t currentSixteenthStep = noteStartTick / Config::TICKS_PER_16TH_STEP;
            uint32_t sixteenthStepStartTick = currentSixteenthStep * Config::TICKS_PER_16TH_STEP;
            int32_t halfSixteenth = Config::TICKS_PER_16TH_STEP / 2;  // 48 ticks
            int32_t offsetFromSixteenthCenter = (int32_t)noteStartTick - ((int32_t)sixteenthStepStartTick + halfSixteenth);
            
            // Map offset to CC2 value: 64 = center, range ±63
            uint8_t fineCCValue = (uint8_t)constrain(64 + offsetFromSixteenthCenter, 0, 127);
            
            logger.log(CAT_MIDI, LOG_DEBUG, "SENDING FINE CC2: ch=%d cc=%d stepStart=%lu offset=%ld ccValue=%d", 
                       FINE_CC_CHANNEL, FINE_CC_NUMBER, sixteenthStepStartTick, offsetFromSixteenthCenter, fineCCValue);
            
            // Send fine position as CC2 on channel 15
            midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);
            }
        }
        
        // Record when we sent this pitchbend to ignore incoming feedback
        lastPitchbendSentTime = millis();
        logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend and CC2 sent to faders - ignoring incoming for %dms", PITCHBEND_IGNORE_PERIOD);
    }
}

void NoteEditManager::sendSelectnoteFaderUpdate(Track& track) {
    // Cancel any previous pending update and schedule a new one
    // This prevents multiple overlapping updates when fader 2 is moved continuously
    uint32_t now = millis();
    
    if (pendingSelectnoteUpdate) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Canceling previous selectnote update and scheduling new one");
    }
    
    selectnoteUpdateTime = now + SELECTNOTE_UPDATE_DELAY;
    pendingSelectnoteUpdate = true;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Scheduled selectnote fader update for %lu ms from now", SELECTNOTE_UPDATE_DELAY);
}

void NoteEditManager::performSelectnoteFaderUpdate(Track& track) {
    // Send program change first to activate selectnote fader
    midiHandler.sendProgramChange(PITCHBEND_SELECT_CHANNEL, 1);  // Program 1 for SELECT mode
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=1 (SELECT mode for selectnote fader)", 
               PITCHBEND_SELECT_CHANNEL);
    
    // Send selectnote fader position update
    EditSelectNoteState::sendTargetPitchbend(editManager, track);
    
    // Check if we should update fader 2 position
    // Use extended protection period to prevent updates during active fader 2 use
    uint32_t now = millis();
    bool recentEditingActivity = (lastEditingActivityTime > 0 && (now - lastEditingActivityTime) < FADER2_PROTECTION_PERIOD);
    bool inPitchbendIgnorePeriod = (lastPitchbendSentTime > 0 && (now - lastPitchbendSentTime) < PITCHBEND_IGNORE_PERIOD);
    
    if (!recentEditingActivity && !inPitchbendIgnorePeriod) {
        // Safe to update fader 2 position - no recent activity and not in ignore period
        midiHandler.sendProgramChange(PITCHBEND_START_CHANNEL, 2);  // Program 2 for COARSE+FINE editing
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=2 (updating fader 2 position)", 
                   PITCHBEND_START_CHANNEL);
        
        // Send updated coarse and fine positions for fader 2 and 3
        sendStartNotePitchbend(track);
        logger.log(CAT_MIDI, LOG_DEBUG, "Updated fader 2/3 positions to match note position");
    } else {
        if (recentEditingActivity) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader 2 update - recent editing activity (%lu ms ago)", 
                       now - lastEditingActivityTime);
        }
        if (inPitchbendIgnorePeriod) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader 2 update - in pitchbend ignore period (%lu ms ago)", 
                       now - lastPitchbendSentTime);
        }
    }
    
    // Record when we sent this update to ignore incoming feedback
    lastSelectnoteSentTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Selectnote fader updated - ignoring incoming for %dms", PITCHBEND_IGNORE_PERIOD);
}

void NoteEditManager::enableStartEditing() {
    uint32_t now = millis();
    if (now - noteSelectionTime >= NOTE_SELECTION_GRACE_PERIOD) {
        if (!startEditingEnabled) {
            startEditingEnabled = true;
            logger.info("Start editing enabled - grace period elapsed (%lu ms since selection)", now - noteSelectionTime);
            Track& track = trackManager.getSelectedTrack();
            sendFaderUpdate(MidiMapping::FaderType::FADER_COARSE, track);
            sendFaderUpdate(MidiMapping::FaderType::FADER_FINE, track);
            sendFaderUpdate(MidiMapping::FaderType::FADER_NOTE_VALUE, track);
        }
    }
}

void NoteEditManager::refreshEditingActivity() {
    lastEditingActivityTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Editing activity refreshed - note selection disabled for %dms", NOTE_SELECTION_GRACE_PERIOD);
}

// Unified Fader State Machine Implementation

void NoteEditManager::initializeFaderStates() {
    faderStates.clear();
    faderStates.resize(4);
    
    // Initialize Fader 1: Note Selection (Channel 16, Pitchbend)
    faderStates[0] = {
        .type = MidiMapping::FaderType::FADER_SELECT,
        .channel = PITCHBEND_SELECT_CHANNEL,
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
        .channel = PITCHBEND_START_CHANNEL,
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
        .channel = FINE_CC_CHANNEL,
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
        .channel = NOTE_VALUE_CC_CHANNEL,
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

// MidiFaderProcessor::FaderState& NoteEditManager::getFaderState(MidiMapping::FaderType faderType) {
//     for (auto& state : faderStates) {
//         if (state.type == faderType) {
//             return state;
//         }
//     }
//     // Should never happen, but return first as fallback
//     return faderStates[0];
// }

bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, -1, -1); // Use overloaded version with unknown values
}

bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    MidiFaderProcessor::FaderState& state = midiFaderManagerV2.getFaderStateMutable(faderType);
    uint32_t now = millis();    
    
    // No feedback prevention if we haven't sent anything recently
    if (state.lastSentTime == 0 || (now - state.lastSentTime) >= FEEDBACK_IGNORE_PERIOD) {
        return false;
    }
    
    // If we don't have the incoming values, use the old blanket ignore logic as fallback
    if (pitchbendValue == -1 && ccValue == (uint8_t)-1) {
        uint32_t remaining = FEEDBACK_IGNORE_PERIOD - (now - state.lastSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (feedback prevention): %lu ms remaining", 
                   faderType, remaining);
        return true;
    }
    
    // EDGE CASE FIX: Immediate post-update grace period
    // For the first 200ms after sending an update, ignore ALL input to prevent
    // user reactions to automatic fader movements from triggering new updates
    const uint32_t POST_UPDATE_GRACE_PERIOD = 200; // 200ms strict ignore period
    if ((now - state.lastSentTime) < POST_UPDATE_GRACE_PERIOD) {
        uint32_t remaining = POST_UPDATE_GRACE_PERIOD - (now - state.lastSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (post-update grace period): %lu ms remaining", 
                   faderType, remaining);
        return true;
    }
    
    // Smart feedback detection: only ignore if the incoming value matches what we just sent
    const int16_t FEEDBACK_TOLERANCE_PITCHBEND = 100;  // Allow 100 units tolerance for pitchbend
    const uint8_t FEEDBACK_TOLERANCE_CC = 3;           // Allow 3 units tolerance for CC
    
    bool isProbablyFeedback = false;
    
    if (faderType == MidiMapping::FaderType::FADER_SELECT || faderType == MidiMapping::FaderType::FADER_COARSE) {
        // For pitchbend faders, check if incoming value is close to what we last sent
        int16_t diff = abs(pitchbendValue - state.lastSentPitchbend);
        if (diff <= FEEDBACK_TOLERANCE_PITCHBEND) {
            isProbablyFeedback = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d pitchbend %d (feedback: sent %d, diff=%d)", 
                       faderType, pitchbendValue, state.lastSentPitchbend, diff);
        }
    } else if (faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        // For CC faders, check if incoming value is close to what we last sent
        uint8_t diff = abs((int)ccValue - (int)state.lastSentCC);
        if (diff <= FEEDBACK_TOLERANCE_CC) {
            isProbablyFeedback = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d CC %d (feedback: sent %d, diff=%d)", 
                       faderType, ccValue, state.lastSentCC, diff);
        }
    }
    
    if (!isProbablyFeedback) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Accepting fader %d input (significant user movement, not feedback)", 
                   faderType);
    }
    
    return isProbablyFeedback;
}

void NoteEditManager::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    uint32_t now = millis();
    uint32_t updateTime = now + FADER_UPDATE_DELAY;
    std::vector<MidiMapping::FaderType> fadersToUpdate;

    if (driverFader == MidiMapping::FaderType::FADER_SELECT) {
        // Fader 1 (SELECT) schedules updates for faders 2, 3, 4
        fadersToUpdate = {
            MidiMapping::FaderType::FADER_COARSE,
            MidiMapping::FaderType::FADER_FINE,
            MidiMapping::FaderType::FADER_NOTE_VALUE
        };
    } else if (driverFader == MidiMapping::FaderType::FADER_COARSE) {
        // Fader 2 (COARSE) schedules update for fader 1
        fadersToUpdate = { MidiMapping::FaderType::FADER_SELECT };
    } else {
        return;
    }

    for (auto targetFader : fadersToUpdate) {
        for (auto& state : faderStates) {
            if (state.type == targetFader) {
                state.pendingUpdate = true;
                state.updateScheduledTime = updateTime;
                state.scheduledByDriver = driverFader;
                break;
            }
        }
    }
}

void NoteEditManager::updateFaderStates() {
    uint32_t now = millis();
    for (auto& state : faderStates) {
        if (state.pendingUpdate && now >= state.updateScheduledTime) {
            // Only update if the driver is not still active
            bool driverStillActive = false;
            if (state.scheduledByDriver == MidiMapping::FaderType::FADER_SELECT) {
                driverStillActive = (lastSelectFaderTime > 0 && (now - lastSelectFaderTime) < 200);
            } else if (state.scheduledByDriver == MidiMapping::FaderType::FADER_COARSE) {
                driverStillActive = (lastDriverFaderTime > 0 && currentDriverFader == MidiMapping::FaderType::FADER_COARSE && (now - lastDriverFaderTime) < FADER_UPDATE_DELAY);
            }
            if (!driverStillActive) {
                state.pendingUpdate = false;
                Track& track = trackManager.getSelectedTrack();
                sendFaderUpdate(state.type, track);
            }
        }
    }
}

void NoteEditManager::sendFaderUpdate(MidiMapping::FaderType faderType, Track& track) {
    // IMPORTANT: Don't update CC faders (faders 3 and 4) when they were recently the driver
    // These faders represent user input and should maintain their position for a reasonable time
    // The MIDI events are the single source of truth - don't send calculated positions back to these faders
    if ((faderType == MidiMapping::FaderType::FADER_FINE && currentDriverFader == MidiMapping::FaderType::FADER_FINE) ||
        (faderType == MidiMapping::FaderType::FADER_NOTE_VALUE && currentDriverFader == MidiMapping::FaderType::FADER_NOTE_VALUE)) {
        uint32_t now = millis();
        uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
        if (timeSinceDriverSet < 1000) { // 1 second protection period
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader %d update - fader %d was recently the driver (%lu ms ago)", 
                       faderType, faderType, timeSinceDriverSet);
            return;
        }
    }
    
    // Faders 3 and 4 (CC faders) never need program changes - they only use CC messages
    bool shouldSendProgramChange = (faderType != MidiMapping::FaderType::FADER_FINE && faderType != MidiMapping::FaderType::FADER_NOTE_VALUE);
    
    // Channel conflict logic: Only apply to faders 2, 3, 4 updating each other
    // Fader 1 (SELECT) should always be able to update faders 2, 3, 4 when it schedules them
    bool currentDriverOnChannel15 = (currentDriverFader == MidiMapping::FaderType::FADER_COARSE || currentDriverFader == MidiMapping::FaderType::FADER_FINE || currentDriverFader == MidiMapping::FaderType::FADER_NOTE_VALUE);
    bool faderOnChannel15 = (faderType == MidiMapping::FaderType::FADER_COARSE || faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE);
    
    // Only apply channel conflict logic if both the driver and target fader are on channel 15
    // AND the target fader is not being updated by fader 1 (SELECT)
    if (shouldSendProgramChange && currentDriverOnChannel15 && faderOnChannel15 && currentDriverFader != faderType) {
        // Check if this update was scheduled by fader 1 (SELECT)
        bool scheduledBySelect = false;
        for (auto& state : faderStates) {
            if (state.type == faderType && state.scheduledByDriver == MidiMapping::FaderType::FADER_SELECT) {
                scheduledBySelect = true;
                break;
            }
        }
        
        // If scheduled by fader 1, allow the update regardless of channel conflicts
        if (!scheduledBySelect) {
            // Driver and fader both share channel 15 - skip program change
            shouldSendProgramChange = false;
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping program change for fader %d (shares channel 15 with driver %d)", 
                       faderType, currentDriverFader);
        }
    }

    if (shouldSendProgramChange) {
        uint8_t program = (faderType == MidiMapping::FaderType::FADER_SELECT) ? 1 : 2;
        midiHandler.sendProgramChange(midiFaderManagerV2.getFaderStateMutable(faderType).channel, program);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=%d (fader %d update)", 
                   midiFaderManagerV2.getFaderStateMutable(faderType).channel, program, faderType);
    } else if (faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Skipped program change for fader %d (CC fader) - only uses CC messages", faderType);
    }
    
    // Send position update
    sendFaderPosition(faderType, track);
    
    // Record when we sent this update and set ignore periods
    uint32_t now = millis();
    midiFaderManagerV2.getFaderStateMutable(faderType).lastSentTime = now;
    
    // IMPORTANT: If updating any channel 15 fader, all channel 15 faders get updated together.
    // Set ignore periods for all to prevent feedback from any MIDI message causing unwanted processing.
    if (faderType == MidiMapping::FaderType::FADER_COARSE || faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        midiFaderManagerV2.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = now;
        midiFaderManagerV2.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = now;
        midiFaderManagerV2.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = now;
        logger.log(CAT_MIDI, LOG_DEBUG, "Set ignore periods for all channel 15 faders (shared channel)");
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d updated - ignoring incoming for %dms", 
               faderType, FEEDBACK_IGNORE_PERIOD);
}

void NoteEditManager::sendFaderPosition(MidiMapping::FaderType faderType, Track& track) {
    switch (faderType) {
        case MidiMapping::FaderType::FADER_SELECT:
            EditSelectNoteState::sendTargetPitchbend(editManager, track);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
            sendCoarseFaderPosition(track);
            break;
        case MidiMapping::FaderType::FADER_FINE:
            sendFineFaderPosition(track);
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            sendNoteValueFaderPosition(track);
            break;
    }
}

void NoteEditManager::sendCoarseFaderPosition(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse position");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t targetTick, currentSixteenthStep;
        
        if (lengthEditingMode) {
            // LENGTH EDIT mode: Use note END position
            uint32_t noteEndTick = notes[selectedIdx].endTick;
            // Convert to relative position
            targetTick = (noteEndTick >= loopStartTick) ? 
                (noteEndTick - loopStartTick) : (noteEndTick + loopLength - loopStartTick);
            targetTick = targetTick % loopLength;
            currentSixteenthStep = targetTick / Config::TICKS_PER_16TH_STEP;
            logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader position (LENGTH EDIT): step %lu/80 -> pitchbend %d + note 1 trigger", 
                       currentSixteenthStep, targetTick);
        } else {
            // POSITION EDIT mode: Use note START position  
            uint32_t noteStartTick = notes[selectedIdx].startTick;
            // Convert to relative position
            targetTick = (noteStartTick >= loopStartTick) ? 
                (noteStartTick - loopStartTick) : (noteStartTick + loopLength - loopStartTick);
            targetTick = targetTick % loopLength;
            currentSixteenthStep = targetTick / Config::TICKS_PER_16TH_STEP;
            logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader position (POSITION EDIT): step %lu/80 -> pitchbend %d + note 1 trigger", 
                       currentSixteenthStep, targetTick);
        }
        
        uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
        
        if (numSteps > 1) {
            const int16_t PITCHBEND_MIN = -8192;
            const int16_t PITCHBEND_MAX = 8191;
            
            float normalizedPos = (float)currentSixteenthStep / (float)(numSteps - 1);
            int16_t coarseMidiPitchbend = (int16_t)(PITCHBEND_MIN + normalizedPos * (PITCHBEND_MAX - PITCHBEND_MIN));
            coarseMidiPitchbend = constrain(coarseMidiPitchbend, PITCHBEND_MIN, PITCHBEND_MAX);
            
            midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);
            
            // Send note 1 trigger on channel 15 to help motorized fader 2 update
            midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, 1, 127);
            midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, 1, 0);
            
            // Record the value we sent for smart feedback detection
            midiFaderManagerV2.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentPitchbend = coarseMidiPitchbend;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Sent coarse pitchbend=%d + note 1 trigger (note at step %lu)", 
                       coarseMidiPitchbend, currentSixteenthStep);
        }
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse position: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

void NoteEditManager::sendFineFaderPosition(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine position");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t targetTick;
        
        if (lengthEditingMode) {
            // LENGTH EDIT mode: Use note END position
            uint32_t noteEndTick = notes[selectedIdx].endTick;
            // Convert to relative position
            targetTick = (noteEndTick >= loopStartTick) ? 
                (noteEndTick - loopStartTick) : (noteEndTick + loopLength - loopStartTick);
            targetTick = targetTick % loopLength;
        } else {
            // POSITION EDIT mode: Use note START position
            uint32_t noteStartTick = notes[selectedIdx].startTick;
            // Convert to relative position
            targetTick = (noteStartTick >= loopStartTick) ? 
                (noteStartTick - loopStartTick) : (noteStartTick + loopLength - loopStartTick);
            targetTick = targetTick % loopLength;
        }
        
        // Use the reference step (established by coarse/select faders) as the base for CC calculation
        // This ensures fader 3 represents the note's position relative to a stable reference
        uint32_t referenceStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
        int32_t offsetFromReferenceStep = (int32_t)targetTick - (int32_t)referenceStepStartTick;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader position (%s): offset %ld/47 -> CC=%d + note 2 trigger", 
                   lengthEditingMode ? "LENGTH EDIT" : "POSITION EDIT", offsetFromReferenceStep, targetTick);
        
        // CC64 = 0 tick offset from reference step start, CC0 = -64 ticks, CC127 = +63 ticks
        uint8_t fineCCValue = (uint8_t)constrain(64 + offsetFromReferenceStep, 0, 127);
        midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);
        
        // Send note-on with velocity 127 followed by note-off to trigger fader update
        midiHandler.sendNoteOn(FINE_CC_CHANNEL, 0, 127);
        midiHandler.sendNoteOff(FINE_CC_CHANNEL, 0, 0);
        
        // Record the value we sent for smart feedback detection
        midiFaderManagerV2.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentCC = fineCCValue;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent fine CC=%d + note trigger (note offset %ld from reference step %lu)", 
                   fineCCValue, offsetFromReferenceStep, referenceStep);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine position: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

void NoteEditManager::sendNoteValueFaderPosition(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value position");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // Note value doesn't need relative positioning - it's just the MIDI note number
        uint8_t noteValue = notes[selectedIdx].note;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader position: note %d -> CC=%d + note 3 trigger", 
                   noteValue, noteValue);
        
        midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER, noteValue);
        
        // Send note 3 trigger on channel 15 to help motorized fader 4 update
        midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, 3, 127);
        midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, 3, 0);
        
        // Record the value we sent for smart feedback detection
        midiFaderManagerV2.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentCC = noteValue;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent note value CC=%d + note 3 trigger (note value %d)", 
                   noteValue, noteValue);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

void NoteEditManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (currentMainEditMode != MAIN_MODE_NOTE_EDIT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (currentMainEditMode == MAIN_MODE_LOOP_EDIT) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // With dedicated faders, we don't need to check edit mode - the fader is always active
    uint32_t now = millis();
    
    // SMART SELECTION STABILITY: Only process significant movements that indicate user intent
    // This prevents motorized fader feedback from being interpreted as user input
    bool isSignificantMovement = false;
    
    if (lastSelectFaderTime == 0) {
        // First movement - always significant
        isSignificantMovement = true;
    } else {
        int16_t movementDelta = abs(pitchValue - lastUserSelectFaderValue);
        uint32_t timeSinceLastMovement = now - lastSelectFaderTime;
        
        // Movement is significant if:
        // 1. Large enough change (> threshold), OR
        // 2. Enough time has passed since last movement (user settled then moved again)
        if (movementDelta >= SELECT_MOVEMENT_THRESHOLD || timeSinceLastMovement >= SELECT_STABILITY_TIME) {
            isSignificantMovement = true;
        }
    }
    
    if (!isSignificantMovement) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: ignoring small movement (delta=%d, time=%lu ms)", 
                   abs(pitchValue - lastUserSelectFaderValue), now - lastSelectFaderTime);
        return;
    }
    
    // Update tracking for next comparison
    lastUserSelectFaderValue = pitchValue;
    lastSelectFaderTime = now;
    
    // Check if we're in grace period after recent editing activity
    if (lastEditingActivityTime > 0 && (now - lastEditingActivityTime) < NOTE_SELECTION_GRACE_PERIOD) {
        uint32_t remaining = NOTE_SELECTION_GRACE_PERIOD - (now - lastEditingActivityTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection disabled - editing grace period: %lu ms remaining", remaining);
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Get loop start offset for relative positioning
    uint32_t loopStartTick = track.getLoopStartTick();
    
    // Collect ALL note positions and empty step positions for fader navigation
    uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    const auto& notes = track.getCachedNotes();
    std::vector<uint32_t> allPositions;
    
    // First, add all note positions - adjust to be relative to loop start point
    for (int i = 0; i < (int)notes.size(); i++) {
        uint32_t absolutePos = notes[i].startTick;
        uint32_t relativePos = (absolutePos >= loopStartTick) ? 
            (absolutePos - loopStartTick) : (absolutePos + loopLength - loopStartTick);
        relativePos = relativePos % loopLength;
        allPositions.push_back(relativePos);
    }
    
    // Then, add empty step positions for steps that have no notes
    for (uint32_t step = 0; step < numSteps; step++) {
        uint32_t stepTick = step * Config::TICKS_PER_16TH_STEP;
        
        // Check if this step position already has a note (using relative positions)
        bool hasNoteAtStep = false;
        for (int i = 0; i < (int)notes.size(); i++) {
            uint32_t absolutePos = notes[i].startTick;
            uint32_t relativePos = (absolutePos >= loopStartTick) ? 
                (absolutePos - loopStartTick) : (absolutePos + loopLength - loopStartTick);
            relativePos = relativePos % loopLength;
            if (relativePos == stepTick) {
                hasNoteAtStep = true;
                break;
            }
        }
        
        // If no note at this exact step position, add the empty step
        if (!hasNoteAtStep) {
            allPositions.push_back(stepTick);
        }
    }
    
    // Sort all positions
    std::sort(allPositions.begin(), allPositions.end());
    
    // Remove duplicates
    allPositions.erase(std::unique(allPositions.begin(), allPositions.end()), allPositions.end());
    
    if (!allPositions.empty()) {
        int posIndex = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, allPositions.size() - 1);
        uint32_t relativeTargetTick = allPositions[posIndex];
        
        // Convert relative position back to absolute position for internal logic
        uint32_t absoluteTargetTick = (relativeTargetTick + loopStartTick) % loopLength;
        
        // Find all notes at the absolute target tick for multi-note cycling
        std::vector<int> notesAtTick;
        for (int i = 0; i < (int)notes.size(); i++) {
            if (notes[i].startTick == absoluteTargetTick) {
                notesAtTick.push_back(i);
            }
        }
        
        int noteIdx = -1;
        if (!notesAtTick.empty()) {
            // Sort notes by pitch for consistent cycling order
            std::sort(notesAtTick.begin(), notesAtTick.end(), [&notes](int a, int b) {
                return notes[a].note < notes[b].note;
            });
            
            // Simple cycling logic: if we're at the same tick and have multiple notes, cycle through them
            if (absoluteTargetTick == editManager.getBracketTick() && editManager.getSelectedNoteIdx() >= 0 && notesAtTick.size() > 1) {
                int currentIdx = editManager.getSelectedNoteIdx();
                auto currentIt = std::find(notesAtTick.begin(), notesAtTick.end(), currentIdx);
                
                if (currentIt != notesAtTick.end()) {
                    // Move to next note in the list
                    auto nextIt = std::next(currentIt);
                    if (nextIt != notesAtTick.end()) {
                        noteIdx = *nextIt;
                    } else {
                        // Wrap to first note
                        noteIdx = notesAtTick[0];
                    }
                    logger.log(CAT_MIDI, LOG_INFO, "Cycling through notes at tick %lu (%d notes total)", 
                               absoluteTargetTick, (int)notesAtTick.size());
                } else {
                    // Current selection not in this tick's notes, select first
                    noteIdx = notesAtTick[0];
                }
            } else {
                // Different tick, single note, or no current selection - select first note
                noteIdx = notesAtTick[0];
                if (absoluteTargetTick != editManager.getBracketTick()) {
                    logger.log(CAT_MIDI, LOG_INFO, "Moving to tick position: %lu", absoluteTargetTick);
                }
            }
        }
        
        if (absoluteTargetTick != editManager.getBracketTick() || noteIdx != editManager.getSelectedNoteIdx()) {
            editManager.setBracketTick(absoluteTargetTick);
            
            if (noteIdx >= 0) {
                editManager.setSelectedNoteIdx(noteIdx);
                if (notesAtTick.size() > 1) {
                    // Find which note we selected in the multi-note list
                    auto selectedIt = std::find(notesAtTick.begin(), notesAtTick.end(), noteIdx);
                    int notePosition = std::distance(notesAtTick.begin(), selectedIt) + 1;
                    logger.log(CAT_MIDI, LOG_INFO, "Select fader: selected note %d at tick %lu (%d/%d notes at this position)", 
                               noteIdx, absoluteTargetTick, notePosition, (int)notesAtTick.size());
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected note %d at tick %lu", noteIdx, absoluteTargetTick);
                }
                
                // CRITICAL: Reset moving note state when selecting a new note
                // This ensures that when fader 2 is used, it will track the newly selected note
                // instead of continuing to track a previously moving note
                editManager.movingNote.active = false;
                editManager.movingNote.deletedNotes.clear();
                logger.log(CAT_MIDI, LOG_DEBUG, "Reset moving note state for new selection");
                
                // Set initial reference step based on note position
                referenceStep = absoluteTargetTick / Config::TICKS_PER_16TH_STEP;
                
                // Schedule updates for faders 2, 3, and 4 after note selection
                scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_SELECT);
                
                // Keep current movement tracking values to maintain proper filtering
                // Don't reset to 0 as this would cause incorrect delta calculations
                
                // Start grace period for start editing
                noteSelectionTime = millis();
                startEditingEnabled = false;
            } else {
                // STABILITY FIX: Only reset selection if we don't currently have a note selected
                // This prevents rapid fader movements from accidentally resetting a valid selection
                if (editManager.getSelectedNoteIdx() < 0) {
                    editManager.resetSelection();
                    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected empty step at tick %lu (no note found)", absoluteTargetTick);
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: ignoring empty step at tick %lu (preserving current selection %d)", 
                               absoluteTargetTick, editManager.getSelectedNoteIdx());
                }
            }
        }
    }
}

void NoteEditManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (currentMainEditMode != MAIN_MODE_NOTE_EDIT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (currentMainEditMode == MAIN_MODE_LOOP_EDIT) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Start editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse editing");
        return;
    }
    
    // Movement filtering - prevent jitter from rescheduling updates
    uint32_t now = millis();
    int16_t movementDelta = abs(pitchValue - lastUserCoarseFaderValue);
    uint32_t timeSinceLastMovement = (lastCoarseFaderTime > 0) ? (now - lastCoarseFaderTime) : COARSE_STABILITY_TIME;
    
    // Only process if movement is significant enough or enough time has passed
    if (movementDelta >= COARSE_MOVEMENT_THRESHOLD || timeSinceLastMovement >= COARSE_STABILITY_TIME) {
        // Update tracking values
        lastUserCoarseFaderValue = pitchValue;
        lastCoarseFaderTime = now;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader: significant movement (delta=%d, time=%lu ms) - %s mode", 
                   movementDelta, timeSinceLastMovement, lengthEditingMode ? "LENGTH EDIT" : "POSITION EDIT");
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader: ignoring small movement (delta=%d, time=%lu ms)", 
                   movementDelta, timeSinceLastMovement);
        return; // Skip processing for small movements
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // CRITICAL: Use stable note identity if we're already moving a note
        NoteUtils::DisplayNote currentNote;
        uint32_t currentNoteStartTick;
        uint32_t loopStartTick = track.getLoopStartTick();
        
        if (editManager.movingNote.active) {
            // Use the moving note's current position, not the selected index
            currentNote.note = editManager.movingNote.note;
            currentNote.startTick = editManager.movingNote.lastStart;
            currentNote.endTick = editManager.movingNote.lastEnd;
            currentNote.velocity = 64; // Default velocity for the calculation
            currentNoteStartTick = editManager.movingNote.lastStart;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Using stable moving note identity: pitch=%d, start=%lu", 
                       currentNote.note, currentNote.startTick);
        } else {
            // First movement - use the selected note
            currentNote = notes[selectedIdx];
            currentNoteStartTick = notes[selectedIdx].startTick;
        }
        
        if (lengthEditingMode) {
            // LENGTH EDIT MODE: Move the note END position in 16th step increments
            uint32_t currentNoteEndTick = currentNote.endTick;
            
            // Convert current end tick to relative position
            uint32_t relativeEndTick = (currentNoteEndTick >= loopStartTick) ? 
                (currentNoteEndTick - loopStartTick) : (currentNoteEndTick + loopLength - loopStartTick);
            relativeEndTick = relativeEndTick % loopLength;
            
            // Calculate how many 16th steps are in the loop
            uint32_t totalSixteenthSteps = loopLength / Config::TICKS_PER_16TH_STEP;
            
            // Calculate current end step and offset (using relative position)
            uint32_t currentSixteenthStep = relativeEndTick / Config::TICKS_PER_16TH_STEP;
            uint32_t offsetWithinSixteenth = relativeEndTick % Config::TICKS_PER_16TH_STEP;
            
            // Map pitchbend to 16th step across entire loop
            uint32_t targetSixteenthStep = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, totalSixteenthSteps - 1);
            
            // Calculate target end tick: new 16th step + preserved offset (relative)
            uint32_t relativeTargetEndTick = (targetSixteenthStep * Config::TICKS_PER_16TH_STEP) + offsetWithinSixteenth;
            
            // Constrain to valid range within the loop
            if (relativeTargetEndTick >= loopLength) {
                relativeTargetEndTick = loopLength - 1;
            }
            
            // Convert back to absolute position
            uint32_t targetEndTick = (relativeTargetEndTick + loopStartTick) % loopLength;
            
            // Calculate new note length and enforce minimum
            uint32_t newNoteDuration = calculateNoteLength(currentNote.startTick, targetEndTick, loopLength);
            uint32_t minNoteDuration = Config::TICKS_PER_16TH_STEP; // Minimum 1/16th step
            
            if (newNoteDuration < minNoteDuration) {
                // Enforce minimum note length
                targetEndTick = (currentNote.startTick + minNoteDuration) % loopLength;
                newNoteDuration = minNoteDuration;
                logger.log(CAT_MIDI, LOG_DEBUG, "Enforced minimum note length: %lu -> %lu ticks", newNoteDuration, minNoteDuration);
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "LENGTH EDIT: Note end moved from step %lu to %lu (tick %lu -> %lu, relative %lu -> %lu)", 
                       currentSixteenthStep, targetSixteenthStep, currentNoteEndTick, targetEndTick, relativeEndTick, relativeTargetEndTick);
            
            // Store the target step as reference for fine adjustments
            referenceStep = targetSixteenthStep;
            
            // Update the note end position
            auto& midiEvents = track.getMidiEvents();
            
            // Find the note-on event
            MidiEvent* noteOnEvent = nullptr;
            for (auto& event : midiEvents) {
                if (event.type == midi::NoteOn && 
                    event.data.noteData.note == currentNote.note &&
                    event.tick == currentNote.startTick &&
                    event.data.noteData.velocity > 0) {
                    noteOnEvent = &event;
                    break;
                }
            }
            
            if (noteOnEvent) {
                // Find corresponding note-off event
                MidiEvent* noteOffEvent = findCorrespondingNoteOff(midiEvents, noteOnEvent, currentNote.note, currentNote.startTick, currentNote.endTick);
                if (noteOffEvent) {
                    // Update the note-off event tick
                    noteOffEvent->tick = targetEndTick;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated note-off event: pitch=%d, start=%lu, new end=%lu", 
                               currentNote.note, currentNote.startTick, targetEndTick);
                    
                    // Update moving note tracking if active
                    if (editManager.movingNote.active) {
                        editManager.movingNote.lastEnd = targetEndTick;
                    }
                }
            }
        } else {
            // POSITION EDIT MODE: Move the note START position in 16th step increments
            // Convert current start tick to relative position
            uint32_t relativeStartTick = (currentNoteStartTick >= loopStartTick) ? 
                (currentNoteStartTick - loopStartTick) : (currentNoteStartTick + loopLength - loopStartTick);
            relativeStartTick = relativeStartTick % loopLength;
            
            // Calculate how many 16th steps are in the loop
            uint32_t totalSixteenthSteps = loopLength / Config::TICKS_PER_16TH_STEP;
            
            // Calculate the offset within the current 16th step (using relative position)
            uint32_t currentSixteenthStep = relativeStartTick / Config::TICKS_PER_16TH_STEP;
            uint32_t offsetWithinSixteenth = relativeStartTick % Config::TICKS_PER_16TH_STEP;
            
            // Map pitchbend to 16th step across entire loop
            uint32_t targetSixteenthStep = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, totalSixteenthSteps - 1);
            
            // Calculate target tick: new 16th step + preserved offset (relative)
            uint32_t relativeTargetTick = (targetSixteenthStep * Config::TICKS_PER_16TH_STEP) + offsetWithinSixteenth;
            
            // Constrain to valid range within the loop
            if (relativeTargetTick >= loopLength) {
                relativeTargetTick = loopLength - 1;
            }
            
            // Convert back to absolute position
            uint32_t targetTick = (relativeTargetTick + loopStartTick) % loopLength;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "POSITION EDIT: Note moved from step %lu to %lu (tick %lu -> %lu, relative %lu -> %lu)", 
                       currentSixteenthStep, targetSixteenthStep, currentNoteStartTick, targetTick, relativeStartTick, relativeTargetTick);
            
            // Store the target step as reference for fine adjustments
            referenceStep = targetSixteenthStep;
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
            // Set up driver tracking for coarse fader
            this->currentDriverFader = MidiMapping::FaderType::FADER_COARSE;
            this->lastDriverFaderTime = millis();
            moveNoteToPosition(track, currentNote, targetTick);
        }
        
        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        // Set up driver tracking for coarse fader
        this->currentDriverFader = MidiMapping::FaderType::FADER_COARSE;
        this->lastDriverFaderTime = millis();
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_COARSE);
        // NOTE: Fader 2 (COARSE) now uses 500ms grace period to update fader 1
        // This prevents erratic movement and allows proper settling time
    }
}

void NoteEditManager::handleFineFaderInput(uint8_t ccValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (currentMainEditMode != MAIN_MODE_NOTE_EDIT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (currentMainEditMode == MAIN_MODE_LOOP_EDIT) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine editing");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // CRITICAL: Use stable note identity if we're already moving a note
        NoteUtils::DisplayNote currentNote;
        uint32_t loopStartTick = track.getLoopStartTick();
        
        if (editManager.movingNote.active) {
            // Use the moving note's current position, not the selected index
            currentNote.note = editManager.movingNote.note;
            currentNote.startTick = editManager.movingNote.lastStart;
            currentNote.endTick = editManager.movingNote.lastEnd;
            currentNote.velocity = 64; // Default velocity for the calculation
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Using stable moving note identity: pitch=%d, start=%lu", 
                       currentNote.note, currentNote.startTick);
        } else {
            // First movement - use the selected note
            currentNote = notes[selectedIdx];
        }
        
        if (lengthEditingMode) {
            // LENGTH EDIT MODE: Adjust note END position with fine control
            uint32_t currentNoteEndTick = currentNote.endTick;
            
            // Convert to relative position for calculation
            uint32_t relativeEndTick = (currentNoteEndTick >= loopStartTick) ? 
                (currentNoteEndTick - loopStartTick) : (currentNoteEndTick + loopLength - loopStartTick);
            relativeEndTick = relativeEndTick % loopLength;
            
            // Use the reference step established by coarse fader as the base
            uint32_t sixteenthStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
            
            // CC2 gives us 127 steps for precise control: CC=64 is center (no offset)
            int32_t offset = (int32_t)ccValue - 64;  // -64 to +63
            
            // Calculate target end tick: 16th step boundary + CC offset (relative)
            int32_t relativeTargetEndTickSigned = (int32_t)sixteenthStepStartTick + offset;
            
            // Handle negative values by wrapping to end of loop
            uint32_t relativeTargetEndTick;
            if (relativeTargetEndTickSigned < 0) {
                relativeTargetEndTick = loopLength + relativeTargetEndTickSigned;
            } else {
                relativeTargetEndTick = (uint32_t)relativeTargetEndTickSigned;
            }
            
            // Constrain to valid range within the loop
            if (relativeTargetEndTick >= loopLength) {
                relativeTargetEndTick = relativeTargetEndTick % loopLength;
            }
            
            // Convert back to absolute position
            uint32_t targetEndTick = (relativeTargetEndTick + loopStartTick) % loopLength;
            
            // Calculate new note length and enforce minimum
            uint32_t newNoteDuration = calculateNoteLength(currentNote.startTick, targetEndTick, loopLength);
            uint32_t minNoteDuration = Config::TICKS_PER_16TH_STEP; // Minimum 1/16th step
            
            if (newNoteDuration < minNoteDuration) {
                // Enforce minimum note length
                targetEndTick = (currentNote.startTick + minNoteDuration) % loopLength;
                logger.log(CAT_MIDI, LOG_DEBUG, "Enforced minimum note length: %lu -> %lu ticks", newNoteDuration, minNoteDuration);
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "LENGTH EDIT (fine): Note end adjusted: offset %ld -> %ld (tick %lu -> %lu)", 
                       (int32_t)currentNoteEndTick - (int32_t)sixteenthStepStartTick, offset, currentNoteEndTick, targetEndTick);
            
            // Update the note end position
            auto& midiEvents = track.getMidiEvents();
            
            // Find the note-on event
            MidiEvent* noteOnEvent = nullptr;
            for (auto& event : midiEvents) {
                if (event.type == midi::NoteOn && 
                    event.data.noteData.note == currentNote.note &&
                    event.tick == currentNote.startTick &&
                    event.data.noteData.velocity > 0) {
                    noteOnEvent = &event;
                    break;
                }
            }
            
            if (noteOnEvent) {
                // Find corresponding note-off event
                MidiEvent* noteOffEvent = findCorrespondingNoteOff(midiEvents, noteOnEvent, currentNote.note, currentNote.startTick, currentNote.endTick);
                if (noteOffEvent) {
                    // Update the note-off event tick
                    noteOffEvent->tick = targetEndTick;
                    
                    // Update moving note tracking if active
                    if (editManager.movingNote.active) {
                        editManager.movingNote.lastEnd = targetEndTick;
                    }
                }
            }
        } else {
            // POSITION EDIT MODE: Adjust note START position with fine control
            uint32_t currentNoteStartTick = currentNote.startTick;
            
            // Convert to relative position for calculation
            uint32_t relativeStartTick = (currentNoteStartTick >= loopStartTick) ? 
                (currentNoteStartTick - loopStartTick) : (currentNoteStartTick + loopLength - loopStartTick);
            relativeStartTick = relativeStartTick % loopLength;
            
            // Use the reference step established by coarse fader as the base
            uint32_t sixteenthStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
            
            // CC2 gives us 127 steps for precise control: CC=64 is center (no offset)
            int32_t offset = (int32_t)ccValue - 64;  // -64 to +63
            
            // Calculate target start tick: 16th step boundary + CC offset (relative)
            int32_t relativeTargetStartTickSigned = (int32_t)sixteenthStepStartTick + offset;
            
            // Handle negative values by wrapping to end of loop
            uint32_t relativeTargetStartTick;
            if (relativeTargetStartTickSigned < 0) {
                relativeTargetStartTick = loopLength + relativeTargetStartTickSigned;
            } else {
                relativeTargetStartTick = (uint32_t)relativeTargetStartTickSigned;
            }
            
            // Constrain to valid range within the loop
            if (relativeTargetStartTick >= loopLength) {
                relativeTargetStartTick = relativeTargetStartTick % loopLength;
            }
            
            // Convert back to absolute position
            uint32_t targetStartTick = (relativeTargetStartTick + loopStartTick) % loopLength;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "POSITION EDIT: Fine adjustment from relative tick %lu to %lu (absolute %lu -> %lu)", 
                       relativeStartTick, relativeTargetStartTick, currentNoteStartTick, targetStartTick);
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
            // Set up driver tracking for fine fader
            this->currentDriverFader = MidiMapping::FaderType::FADER_FINE;
            this->lastDriverFaderTime = millis();
            moveNoteToPosition(track, currentNote, targetStartTick);
        }
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader: CC=%d - %s mode", 
                   ccValue, lengthEditingMode ? "LENGTH EDIT" : "POSITION EDIT");
        
        // Mark editing activity to prevent note selection changes (for both modes)
        refreshEditingActivity();
        // Set up driver tracking for fine fader
        this->currentDriverFader = MidiMapping::FaderType::FADER_FINE;
        this->lastDriverFaderTime = millis();
        
        // NOTE: Fader 3 (FINE) does not update other faders to prevent erratic UX
        // Only fader 1 (SELECT) and fader 2 (COARSE) perform cross-updates
    }
}

void NoteEditManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (currentMainEditMode != MAIN_MODE_NOTE_EDIT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (currentMainEditMode == MAIN_MODE_LOOP_EDIT) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value editing");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    auto& midiEvents = track.getMidiEvents();
    auto notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint8_t currentNoteValue = notes[selectedIdx].note;
        uint8_t newNoteValue = ccValue;  // Direct 1:1 mapping from CC to MIDI note value
        
        // Constrain to valid MIDI note range (0-127)
        newNoteValue = constrain(newNoteValue, 0, 127);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: currentNote=%d newNote=%d (cc=%d)", 
                   currentNoteValue, newNoteValue, ccValue);
        
        // If the pitch isn't changing, no need to do anything
        if (currentNoteValue == newNoteValue) {
            return;
        }
        
        // Before changing pitch, handle overlaps and restoration
        uint32_t noteStart = notes[selectedIdx].startTick;
        uint32_t noteEnd = notes[selectedIdx].endTick;
        
        // STEP 1: Check if we should restore any previously deleted/shortened notes
        // When changing pitch, we should restore all notes of the current pitch since they won't overlap anymore
        std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRestore;
        
        for (const auto& deletedNote : editManager.movingNote.deletedNotes) {
            // Restore all deleted notes that match the current pitch (before change)
            // Since we're changing to a different pitch, these notes of the old pitch no longer have overlap conflicts
            if (deletedNote.note == currentNoteValue) {
                notesToRestore.push_back(deletedNote);
                logger.log(CAT_MIDI, LOG_DEBUG, "Will restore note after pitch change: pitch=%d, start=%lu, end=%lu (no longer conflicts with new pitch %d)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick, newNoteValue);
            }
        }
        
        // Apply restoration if needed
        if (!notesToRestore.empty()) {
            auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
            NoteMovementUtils::restoreNotes(midiEvents, notesToRestore, editManager, loopLength, onIndex, offIndex);
            logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu notes after pitch change", notesToRestore.size());
        }
        
        // STEP 2: Check for overlaps with notes of the target pitch
        std::vector<NoteUtils::DisplayNote> otherNotesOfTargetPitch;
        
        // Get fresh note list after potential restoration
        notes = track.getCachedNotes();
        
        // Create a set of restored note positions to avoid processing them as overlaps
        std::set<std::pair<uint32_t, uint32_t>> restoredNotePositions;
        for (const auto& restored : notesToRestore) {
            restoredNotePositions.insert({restored.startTick, restored.endTick});
        }
        
        for (const auto& note : notes) {
            // Only include notes of the target pitch that are NOT the current note
            if (note.note == newNoteValue && 
                !(note.startTick == noteStart && note.endTick == noteEnd)) {
                
                // Skip notes that were just restored to avoid immediately shortening them
                bool wasJustRestored = restoredNotePositions.count({note.startTick, note.endTick}) > 0;
                if (wasJustRestored) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Skipping recently restored note from overlap processing: pitch=%d, start=%lu, end=%lu", 
                              note.note, note.startTick, note.endTick);
                    continue;
                }
                
                otherNotesOfTargetPitch.push_back(note);
            }
        }
        
        if (!otherNotesOfTargetPitch.empty()) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Found %zu notes of target pitch %d, checking for overlaps", 
                      otherNotesOfTargetPitch.size(), newNoteValue);
            
            // Use the existing overlap handling logic from NoteMovementUtils
            std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;
            std::vector<NoteUtils::DisplayNote> notesToDelete;
            
            for (const auto& note : otherNotesOfTargetPitch) {
                bool overlaps = NoteMovementUtils::notesOverlap(noteStart, noteEnd, note.startTick, note.endTick, loopLength);
                if (!overlaps) continue;
                
                // Check if the overlapping note is completely contained within the current note's position
                bool noteCompletelyContained = false;
                
                // Handle both wrapped and unwrapped cases  
                if (noteEnd >= noteStart) {
                    // Current note doesn't wrap around
                    noteCompletelyContained = (note.startTick >= noteStart && note.endTick <= noteEnd);
                } else {
                    // Current note wraps around the loop boundary
                    noteCompletelyContained = (note.startTick >= noteStart || note.endTick <= noteEnd);
                }
                
                if (noteCompletelyContained) {
                    // Delete the note entirely if it's completely contained within the current note
                    notesToDelete.push_back(note);
                    logger.log(CAT_MIDI, LOG_DEBUG, "Will delete completely contained note: pitch=%d, start=%lu, end=%lu (within current note %lu-%lu)", 
                              note.note, note.startTick, note.endTick, noteStart, noteEnd);
                } else {
                    // Try to shorten the overlapping note
                    uint32_t newNoteEndTick;
                    
                    if (note.startTick < noteStart) {
                        // Note starts before current note - shorten it to end 1 tick before current note starts
                        if (noteStart == 0) {
                            // Handle wrap-around case - if current note starts at 0, shortened note ends at loop end - 1
                            newNoteEndTick = loopLength - 1;
                        } else {
                            newNoteEndTick = noteStart - 1;
                        }
                    } else {
                        // Note starts after current note starts - this shouldn't happen in normal overlap cases
                        // but handle it by deleting the note
                        notesToDelete.push_back(note);
                        logger.log(CAT_MIDI, LOG_DEBUG, "Will delete overlapping note that starts after current note: pitch=%d, start=%lu, end=%lu", 
                                  note.note, note.startTick, note.endTick);
                        continue;
                    }
                    
                    uint32_t shortenedLength = NoteMovementUtils::calculateNoteLength(note.startTick, newNoteEndTick, loopLength);
                    
                    // Check if shortened length would be less than 49 ticks
                    if (shortenedLength < 49) {
                        notesToDelete.push_back(note);
                        logger.log(CAT_MIDI, LOG_DEBUG, "Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu->%lu, length=%lu < 49", 
                                  note.note, note.startTick, note.endTick, newNoteEndTick, shortenedLength);
                    } else {
                        notesToShorten.push_back({note, newNoteEndTick});
                        logger.log(CAT_MIDI, LOG_DEBUG, "Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu", 
                                  note.note, note.startTick, note.endTick, newNoteEndTick, shortenedLength);
                    }
                }
            }
            
            // Apply the overlap changes using the existing infrastructure
            if (!notesToShorten.empty() || !notesToDelete.empty()) {
                auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
                NoteMovementUtils::applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, editManager, loopLength, onIndex, offIndex);
                logger.log(CAT_MIDI, LOG_DEBUG, "Applied pitch change overlaps: %zu shortened, %zu deleted", 
                          notesToShorten.size(), notesToDelete.size());
            }
        }
        
        // Now update the pitch of the current note
        bool noteOnUpdated = false;
        bool noteOffUpdated = false;
        
        // Update note-on event
        for (auto& event : midiEvents) {
            if (event.type == midi::NoteOn && 
                event.data.noteData.note == currentNoteValue &&
                event.tick == notes[selectedIdx].startTick &&
                event.data.noteData.velocity > 0) {
                
                logger.log(CAT_MIDI, LOG_DEBUG, "Updating note-on: pitch %d -> %d at tick %lu", 
                           currentNoteValue, newNoteValue, event.tick);
                
                event.data.noteData.note = newNoteValue;
                noteOnUpdated = true;
                break;
            }
        }
        
        // Update note-off event (or note-on with velocity 0)
        for (auto& event : midiEvents) {
            if (((event.type == midi::NoteOff) || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == currentNoteValue &&
                event.tick == notes[selectedIdx].endTick) {
                
                logger.log(CAT_MIDI, LOG_DEBUG, "Updating note-off: pitch %d -> %d at tick %lu", 
                           currentNoteValue, newNoteValue, event.tick);
                
                event.data.noteData.note = newNoteValue;
                noteOffUpdated = true;
                break;
            }
        }
        
        if (noteOnUpdated && noteOffUpdated) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed successfully: %d -> %d", 
                       currentNoteValue, newNoteValue);
            
            // Update the moving note identity to reflect the new pitch
            if (editManager.movingNote.note == currentNoteValue) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Updating moving note identity: pitch %d -> %d", 
                          editManager.movingNote.note, newNoteValue);
                editManager.movingNote.note = newNoteValue;
            }
            
            // Update the selected note index to point to the updated note
            const auto& updatedNotes = track.getCachedNotes();
            int newSelectedIdx = -1;
            
            // Find the updated note in the new notes list
            for (int i = 0; i < (int)updatedNotes.size(); i++) {
                if (updatedNotes[i].note == newNoteValue && 
                    updatedNotes[i].startTick == notes[selectedIdx].startTick &&
                    updatedNotes[i].endTick == notes[selectedIdx].endTick) {
                    newSelectedIdx = i;
                    break;
                }
            }
            
            if (newSelectedIdx >= 0) {
                editManager.setSelectedNoteIdx(newSelectedIdx);
                logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note with new value)", 
                           selectedIdx, newSelectedIdx);
            }
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
            // Set up driver tracking for note value fader
            this->currentDriverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
            this->lastDriverFaderTime = millis();
            // NOTE: Fader 4 (NOTE_VALUE) does not update other faders to prevent erratic UX
            // Only fader 1 (SELECT) and fader 2 (COARSE) perform cross-updates
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Failed to update note value: noteOn=%s noteOff=%s", 
                       noteOnUpdated ? "OK" : "FAILED", noteOffUpdated ? "OK" : "FAILED");
        }
    }
}

// Helper function to check if two notes overlap (borrowed from EditStartNoteState)
bool NoteEditManager::notesOverlap(std::uint32_t start1, std::uint32_t end1, std::uint32_t start2, std::uint32_t end2, std::uint32_t loopLength) {
    // Handle wrapping cases
    bool note1Wraps = (end1 < start1);
    bool note2Wraps = (end2 < start2);
    
    bool overlap = false;
    if (!note1Wraps && !note2Wraps) {
        // Neither note wraps
        overlap = !(end1 <= start2 || end2 <= start1);
    } else if (note1Wraps && !note2Wraps) {
        // Note 1 wraps, note 2 doesn't
        overlap = !(start2 >= end1 && end2 <= start1);
    } else if (!note1Wraps && note2Wraps) {
        // Note 2 wraps, note 1 doesn't
        overlap = !(start1 >= end2 && end1 <= start2);
    } else {
        // Both notes wrap - they always overlap
        overlap = true;
    }
    
    return overlap;
}

// Helper function to calculate note length accounting for wrapping
std::uint32_t NoteEditManager::calculateNoteLength(std::uint32_t start, std::uint32_t end, std::uint32_t loopLength) {
    if (end >= start) return end - start;
    return (loopLength - start) + end;
}

// Find the corresponding note-off event for a given note-on event using LIFO pairing logic
MidiEvent* NoteEditManager::findCorrespondingNoteOff(std::vector<MidiEvent>& midiEvents, MidiEvent* noteOnEvent, uint8_t pitch, std::uint32_t startTick, std::uint32_t endTick) {
    // Use LIFO pairing logic similar to NoteUtils::reconstructNotes
    // We need to simulate the pairing process to find which note-off belongs to our note-on
    
    std::vector<MidiEvent*> activeNoteOnStack;
    
    for (auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 && evt.data.noteData.note == pitch);
        bool isNoteOff = ((evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && evt.data.noteData.note == pitch);
        
        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            if (!activeNoteOnStack.empty()) {
                MidiEvent* correspondingNoteOn = activeNoteOnStack.back();
                activeNoteOnStack.pop_back();
                
                // Check if this is the note-off for our target note-on
                if (correspondingNoteOn == noteOnEvent) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Found corresponding note-off: pitch=%d, noteOn@%lu -> noteOff@%lu", 
                              pitch, correspondingNoteOn->tick, evt.tick);
                    return &evt;
                }
            }
        }
    }
    
    // If we reach here, the note-on didn't have a corresponding note-off (shouldn't happen in well-formed MIDI)
    logger.log(CAT_MIDI, LOG_DEBUG, "No corresponding note-off found for pitch=%d, start=%lu", pitch, startTick);
    return nullptr;
}

// Find overlapping notes for movement (similar to EditStartNoteState::findOverlaps)
void NoteEditManager::findOverlapsForMovement(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                                               uint8_t movingNotePitch,
                                               std::uint32_t currentStart,
                                               std::uint32_t newStart,
                                               std::uint32_t newEnd,
                                               int delta,
                                               std::uint32_t loopLength,
                                               std::vector<std::pair<NoteUtils::DisplayNote, std::uint32_t>>& notesToShorten,
                                               std::vector<NoteUtils::DisplayNote>& notesToDelete) {
    for (const auto& note : currentNotes) {
        if (note.note != movingNotePitch) continue; // Only handle overlaps with same pitch
        if (note.startTick == currentStart) continue; // Skip the moving note itself
        
        bool overlaps = notesOverlap(newStart, newEnd, note.startTick, note.endTick, loopLength);
        if (!overlaps) continue;
        
        // Check if we can shorten the note instead of deleting it (same logic as button encoder)
        if (delta < 0 && note.startTick < newStart) {
            // Moving left, and the overlapping note starts before our new position
            // Try to shorten it to end at our new start
            std::uint32_t newNoteEnd = newStart;
            std::uint32_t shortenedLength = calculateNoteLength(note.startTick, newNoteEnd, loopLength);
            
            if (shortenedLength >= Config::TICKS_PER_16TH_STEP) {
                notesToShorten.push_back({note, newNoteEnd});
                logger.log(CAT_MIDI, LOG_DEBUG, "Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu", 
                          note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
            } else {
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG, "Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu", 
                          note.note, note.startTick, note.endTick);
            }
        } else {
            // Delete the overlapping note
            notesToDelete.push_back(note);
            logger.log(CAT_MIDI, LOG_DEBUG, "Will delete overlapping note: pitch=%d, start=%lu, end=%lu", 
                      note.note, note.startTick, note.endTick);
        }
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Found %zu notes to shorten and %zu notes to delete", 
              notesToShorten.size(), notesToDelete.size());
}

// Apply temporary overlap changes (similar to EditStartNoteState::applyShortenOrDelete)
void NoteEditManager::applyTemporaryOverlapChanges(std::vector<MidiEvent>& midiEvents,
                                                    const std::vector<std::pair<NoteUtils::DisplayNote, std::uint32_t>>& notesToShorten,
                                                    const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                                                    EditManager& manager,
                                                    std::uint32_t loopLength,
                                                    NoteUtils::EventIndexMap& onIndex,
                                                    NoteUtils::EventIndexMap& offIndex) {
    // Shorten overlapping notes using index
    for (const auto& [dn, newEnd] : notesToShorten) {
        // Check if we already have this note in our tracking (by start tick and pitch)
        EditManager::MovingNoteIdentity::DeletedNote* existingEntry = nullptr;
        for (auto& existing : manager.movingNote.deletedNotes) {
            if (existing.note == dn.note && existing.startTick == dn.startTick && existing.wasShortened) {
                existingEntry = &existing;
                break;
            }
        }
        
        if (!existingEntry) {
            // First time shortening this note - record the original
            EditManager::MovingNoteIdentity::DeletedNote original = MidiEventUtils::createDeletedNote(
                dn, loopLength, true, newEnd);
            
            manager.movingNote.deletedNotes.push_back(original);
            logger.log(CAT_MIDI, LOG_DEBUG, "Stored original note before shortening: pitch=%d, start=%lu, original_end=%lu, shortened_to=%lu, length=%lu",
                      original.note, original.startTick, original.endTick, newEnd, original.originalLength);
        } else {
            // Already tracking this note - just update the current shortened position
            existingEntry->shortenedToTick = newEnd;
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated shortened position for existing note: pitch=%d, start=%lu, original_end=%lu, new_shortened_to=%lu",
                      existingEntry->note, existingEntry->startTick, existingEntry->endTick, newEnd);
        }
        
        // Find and update the note-off event
        // We need to look for the note-off at its CURRENT position, not the target position
        // For first shortening: look at original position (dn.endTick from reconstructNotes)
        // For subsequent shortenings: look at current shortened position (from MIDI events, not target)
        
        // Find the note-off event directly in the MIDI events
        MidiEvent* noteOffEvent = nullptr;
        uint32_t currentEndTick = 0;
        
        // Search for any note-off event for this pitch that comes after the note-on
        for (auto& event : midiEvents) {
            if ((event.type == midi::NoteOff || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == dn.note && event.tick > dn.startTick) {
                // Take the first note-off we find for this pitch after the note-on
                noteOffEvent = &event;
                currentEndTick = event.tick;
                break;
            }
        }
        
        if (noteOffEvent) {
            noteOffEvent->tick = newEnd;
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      dn.note, currentEndTick, newEnd);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event for shortening: pitch=%d, start=%lu", 
                      dn.note, dn.startTick);
        }
    }
    
    // Delete overlapping notes entirely
    for (const auto& dn : notesToDelete) {
        int deletedCount = 0;
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            bool matchOn  = (it->type == midi::NoteOn && it->data.noteData.velocity > 0 && 
                           it->data.noteData.note == dn.note && it->tick == dn.startTick);
            bool matchOff = ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) && 
                           it->data.noteData.note == dn.note && it->tick == dn.endTick);
            
            if (matchOn || matchOff) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Temporarily deleting MIDI event: type=%s, pitch=%d, tick=%lu",
                          (matchOn ? "NoteOn" : "NoteOff"), dn.note, (matchOn ? dn.startTick : dn.endTick));
                it = midiEvents.erase(it);
                deletedCount++;
            } else {
                ++it;
            }
        }
        
        if (deletedCount != 2) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: deleted %d events for pitch %d (expected 2)", deletedCount, dn.note);
        }
        
        // Save deleted note for restoration
        EditManager::MovingNoteIdentity::DeletedNote deleted = MidiEventUtils::createDeletedNote(
            dn, loopLength, false, 0);
        
        // Check for duplicates before adding
        bool alreadyExists = false;
        for (const auto& existing : manager.movingNote.deletedNotes) {
            if (existing.note == deleted.note && existing.startTick == deleted.startTick && existing.endTick == deleted.endTick) {
                alreadyExists = true;
                logger.log(CAT_MIDI, LOG_DEBUG, "Deleted note already in deleted list: pitch=%d, start=%lu, end=%lu", 
                          deleted.note, deleted.startTick, deleted.endTick);
                break;
            }
        }
        
        if (!alreadyExists) {
            manager.movingNote.deletedNotes.push_back(deleted);
        }
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Stored deleted note: pitch=%d, start=%lu, end=%lu, length=%lu",
                  deleted.note, deleted.startTick, deleted.endTick, deleted.originalLength);
    }
}

// Restore temporarily removed notes (similar to restoreNotes in EditStartNoteState)
void NoteEditManager::restoreTemporaryNotes(std::vector<MidiEvent>& midiEvents,
                                             const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                                             EditManager& manager,
                                             std::uint32_t loopLength,
                                             NoteUtils::EventIndexMap& onIndex,
                                             NoteUtils::EventIndexMap& offIndex) {
    logger.log(CAT_MIDI, LOG_DEBUG, "=== RESTORING TEMPORARY NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to restore: %zu", notesToRestore.size());
    
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> restored;
    
    for (const auto& nr : notesToRestore) {
        bool didRestore = false;
        
        if (nr.wasShortened) {
            // This note was shortened - find and extend it back to original length
            logger.log(CAT_MIDI, LOG_DEBUG, "Restoring shortened note: pitch=%d, start=%lu, was shortened to %lu, restoring to %lu", 
                      nr.note, nr.startTick, nr.shortenedToTick, nr.endTick);
            
            // Find the note-on event at the start position
            MidiEvent* noteOnEvent = nullptr;
            for (auto& event : midiEvents) {
                if (event.type == midi::NoteOn && event.data.noteData.velocity > 0 &&
                    event.data.noteData.note == nr.note && event.tick == nr.startTick) {
                    noteOnEvent = &event;
                    break;
                }
            }
            
            if (noteOnEvent) {
                // Find the corresponding note-off using LIFO pairing
                // Use the original end tick for proper pairing context
                MidiEvent* correspondingNoteOff = findCorrespondingNoteOff(midiEvents, noteOnEvent, nr.note, nr.startTick, nr.endTick);
                if (correspondingNoteOff) {
                    uint32_t currentEndTick = correspondingNoteOff->tick;
                    correspondingNoteOff->tick = nr.endTick;  // Restore to original end position
                    logger.log(CAT_MIDI, LOG_DEBUG, "Extended shortened note: pitch=%d, start=%lu, current end=%lu, restored end=%lu", 
                              nr.note, nr.startTick, currentEndTick, nr.endTick);
                    didRestore = true;
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Failed to find corresponding note-off for shortened note: pitch=%d, start=%lu", 
                              nr.note, nr.startTick);
                }
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Failed to find note-on for shortened note: pitch=%d, start=%lu", 
                          nr.note, nr.startTick);
            }
        } else {
            // This note was completely deleted - recreate it
            logger.log(CAT_MIDI, LOG_DEBUG, "Restoring deleted note: pitch=%d, start=%lu, end=%lu", 
                      nr.note, nr.startTick, nr.endTick);
            
            MidiEvent onEvt;
            onEvt.tick = nr.startTick;
            onEvt.type = midi::NoteOn;
            onEvt.data.noteData.note = nr.note;
            onEvt.data.noteData.velocity = nr.velocity;
            midiEvents.push_back(onEvt);
            
            MidiEvent offEvt;
            offEvt.tick = nr.endTick;
            offEvt.type = midi::NoteOff;
            offEvt.data.noteData.note = nr.note;
            offEvt.data.noteData.velocity = 0;
            midiEvents.push_back(offEvt);
            
            didRestore = true;
        }
        
        if (didRestore) restored.push_back(nr);
    }
    
    // Instead of removing restored notes, mark them as restored but keep tracking them
    for (const auto& r : restored) {
        for (auto& deletedNote : manager.movingNote.deletedNotes) {
            if (deletedNote.note == r.note && deletedNote.startTick == r.startTick && deletedNote.endTick == r.endTick) {
                if (deletedNote.wasShortened) {
                    // Mark as restored but keep in list for potential re-shortening
                    deletedNote.shortenedToTick = deletedNote.endTick; // Reset to original end
                    logger.log(CAT_MIDI, LOG_DEBUG, "Marked shortened note as restored: pitch=%d, start=%lu, back to original end=%lu", 
                              deletedNote.note, deletedNote.startTick, deletedNote.endTick);
                } else {
                    // Completely deleted notes can be removed since they were recreated
                    deletedNote.note = 255; // Mark for removal
                }
                break;
            }
        }
    }
    
    // Remove only the completely deleted notes (marked with note=255)
    manager.movingNote.deletedNotes.erase(
        std::remove_if(manager.movingNote.deletedNotes.begin(), manager.movingNote.deletedNotes.end(),
            [](const auto& dn){ return dn.note == 255; }),
        manager.movingNote.deletedNotes.end());
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu notes, %zu notes still in deleted list", 
              restored.size(), manager.movingNote.deletedNotes.size());
    
    // Debug: Log remaining notes in deleted list
    if (!manager.movingNote.deletedNotes.empty()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Remaining notes in deleted list:");
        for (const auto& dn : manager.movingNote.deletedNotes) {
            logger.log(CAT_MIDI, LOG_DEBUG, "  - pitch=%d, start=%lu, end=%lu, %s", 
                      dn.note, dn.startTick, dn.endTick, 
                      dn.wasShortened ? "shortened" : "deleted");
        }
    }
}

// Extend shortened notes dynamically
void NoteEditManager::extendShortenedNotes(std::vector<MidiEvent>& midiEvents,
                                           const std::vector<std::pair<EditManager::MovingNoteIdentity::DeletedNote, std::uint32_t>>& notesToExtend,
                                           EditManager& manager,
                                           std::uint32_t loopLength) {
    logger.log(CAT_MIDI, LOG_DEBUG, "=== EXTENDING SHORTENED NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to extend: %zu", notesToExtend.size());
    
    for (const auto& [noteToExtend, newEndTick] : notesToExtend) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Extending shortened note: pitch=%d, start=%lu, from %lu to %lu", 
                  noteToExtend.note, noteToExtend.startTick, noteToExtend.shortenedToTick, newEndTick);
        
        // Find the note-off event at its current shortened position
        MidiEvent* noteOffEvent = nullptr;
        for (auto& event : midiEvents) {
            if ((event.type == midi::NoteOff || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == noteToExtend.note && 
                event.tick > noteToExtend.startTick) {
                // Take the first note-off we find for this pitch after the note-on
                noteOffEvent = &event;
                break;
            }
        }
        
        if (noteOffEvent) {
            uint32_t oldTick = noteOffEvent->tick;
            noteOffEvent->tick = newEndTick;
            
            // Update the tracking in the deleted notes list
            for (auto& deletedNote : manager.movingNote.deletedNotes) {
                if (deletedNote.note == noteToExtend.note && 
                    deletedNote.startTick == noteToExtend.startTick && 
                    deletedNote.wasShortened) {
                    deletedNote.shortenedToTick = newEndTick;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated tracking: shortened note now ends at %lu", newEndTick);
                    break;
                }
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Extended note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      noteToExtend.note, oldTick, newEndTick);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event to extend: pitch=%d, start=%lu", 
                      noteToExtend.note, noteToExtend.startTick);
        }
    }
}

void NoteEditManager::handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    // Check if we should ignore this input (feedback prevention)
    if (shouldIgnoreFaderInput(faderType, pitchbendValue, ccValue)) {
        return;
    }
    
    // Get the current track
    Track& track = trackManager.getSelectedTrack();
    
    // Route to the appropriate fader handler
    switch (faderType) {
        case MidiMapping::FaderType::FADER_SELECT:
            handleSelectFaderInput(pitchbendValue, track);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
            handleCoarseFaderInput(pitchbendValue, track);
            break;
        case MidiMapping::FaderType::FADER_FINE:
            handleFineFaderInput(ccValue, track);
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            handleNoteValueFaderInput(ccValue, track);
            break;
        default:
            logger.log(CAT_MIDI, LOG_DEBUG, "Unknown fader type: %d", (int)faderType);
            break;
    }
}

void NoteEditManager::toggleLengthEditingMode() {
    uint32_t now = millis();
    
    // Debounce protection
    if (now - lastLengthModeToggleTime < LENGTH_MODE_DEBOUNCE_TIME) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Length mode toggle ignored (debounce protection)");
        return;
    }
    lastLengthModeToggleTime = now;
    
    // Toggle the mode
    lengthEditingMode = !lengthEditingMode;
    
    if (lengthEditingMode) {
        logger.info("[MIDI] Length editing mode ENABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE END position (length editing)");
    } else {
        logger.info("[MIDI] Length editing mode DISABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE START position (position editing)");
    }
    
    // Send fader updates to reflect the new mode (like select note does)
    Track& track = trackManager.getSelectedTrack();
    
    // Only update if we have notes to edit
    const auto& notes = track.getCachedNotes();
    if (!notes.empty() && editManager.getSelectedNoteIdx() >= 0 && editManager.getSelectedNoteIdx() < (int)notes.size()) {
        // Schedule fader updates with staggered delays (like enableStartEditing does)
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_SELECT); // This will update faders 1, 2, 3
    }
}

void NoteEditManager::handleLoopLengthInput(uint8_t ccValue, Track& track) {
    // Only process loop length input when in LOOP_EDIT mode
    if (currentMainEditMode != MAIN_MODE_LOOP_EDIT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length input ignored: not in LOOP_EDIT mode (current mode: %s)", 
                   (currentMainEditMode == MAIN_MODE_NOTE_EDIT) ? "NOTE_EDIT" : "UNKNOWN");
        return;
    }
    
    // Convert CC value (0-127) to bars (1-128)
    // CC 0 = 1 bar, CC 127 = 128 bars
    uint8_t bars = map(ccValue, 0, 127, 1, 128);
    
    // Convert bars to ticks
    uint32_t newLoopLengthTicks = bars * Config::TICKS_PER_BAR;
    
    // Get current loop length for comparison
    uint32_t currentLoopLength = track.getLoopLength();
    uint32_t currentBars = (currentLoopLength > 0) ? (currentLoopLength / Config::TICKS_PER_BAR) : 0;
    
    // Only update if the length actually changes
    if (newLoopLengthTicks != currentLoopLength) {
        logger.log(CAT_MIDI, LOG_INFO, "LOOP EDIT: Changing loop length from %lu bars (%lu ticks) to %u bars (%lu ticks)", 
                   currentBars, currentLoopLength, bars, newLoopLengthTicks);
        
        // Set the new loop length with proper note wrapping
        track.setLoopLengthWithWrapping(newLoopLengthTicks);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length updated successfully: CC=%d -> %u bars (%lu ticks)", 
                   ccValue, bars, newLoopLengthTicks);
        
        // Save state to SD card after loop length change
        StorageManager::saveState(looperState.getLooperState());
        logger.log(CAT_MIDI, LOG_DEBUG, "State saved to SD card after loop length change");
        
        // Send MIDI feedback to confirm the change
        sendMainEditModeChange(currentMainEditMode);  // Re-send current mode to confirm
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length unchanged: CC=%d maps to current length (%u bars)", 
                   ccValue, bars);
    }
}

void NoteEditManager::sendCurrentLoopLengthCC(Track& track) {
    // Convert current loop length back to CC 101 value for fader feedback
    uint32_t currentLoopLength = track.getLoopLength();
    
    if (currentLoopLength == 0) {
        // No loop set yet, send CC for 1 bar
        midiHandler.sendControlChange(15, 101, 0);  // CC 0 = 1 bar
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)");
        return;
    }
    
    // Convert ticks back to bars
    uint32_t currentBars = currentLoopLength / Config::TICKS_PER_BAR;
    
    // Clamp to valid range (1-128 bars)
    if (currentBars < 1) currentBars = 1;
    if (currentBars > 128) currentBars = 128;
    
    // Convert bars back to CC value (1-128 bars -> 0-127 CC)
    uint8_t ccValue = map(currentBars, 1, 128, 0, 127);
    
    // Send CC 101 on channel 16 as feedback
    midiHandler.sendControlChange(15, 101, ccValue);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC feedback: %lu bars (%lu ticks) -> CC=%d", 
               currentBars, currentLoopLength, ccValue);
}

void NoteEditManager::onTrackChanged(Track& newTrack) {
    // If we're in loop edit mode, send the new track's loop length as CC feedback
    if (currentMainEditMode == MAIN_MODE_LOOP_EDIT) {
        sendCurrentLoopLengthCC(newTrack);
        logger.log(CAT_MIDI, LOG_DEBUG, "Track changed while in loop edit mode, updating loop length CC");
    }
}

// -------------------------
// Loop start point editing (for loop edit mode)
// -------------------------

void NoteEditManager::handleLoopStartFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in LOOP_EDIT mode
    if (currentMainEditMode != MAIN_MODE_LOOP_EDIT) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: not in LOOP_EDIT mode (current mode: %s)", 
                   (currentMainEditMode == MAIN_MODE_NOTE_EDIT) ? "NOTE_EDIT" : "UNKNOWN");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader input ignored: no loop length set");
        return;
    }
    
    // Use the same logic as select note fader for 16th step navigation
    const int16_t PITCHBEND_MIN = -8192;
    const int16_t PITCHBEND_MAX = 8191;
    
    // Calculate total 16th steps in the loop
    uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    if (numSteps == 0) numSteps = 1;
    
    // Collect ALL possible positions (16th steps AND note positions)
    const auto& notes = track.getCachedNotes();
    std::vector<uint32_t> allPositions;
    
    // First, add all note start positions (these are already absolute positions within the loop)
    for (const auto& note : notes) {
        allPositions.push_back(note.startTick);
    }
    
    // Then, add all 16th step positions
    for (uint32_t step = 0; step < numSteps; step++) {
        uint32_t stepTick = step * Config::TICKS_PER_16TH_STEP;
        allPositions.push_back(stepTick);
    }
    
    // Sort and remove duplicates
    std::sort(allPositions.begin(), allPositions.end());
    allPositions.erase(std::unique(allPositions.begin(), allPositions.end()), allPositions.end());
    
    // Map pitchbend value to position index
    if (allPositions.empty()) {
        allPositions.push_back(0); // Fallback to start of loop
    }
    
    uint32_t targetIndex = map(pitchValue, PITCHBEND_MIN, PITCHBEND_MAX, 0, allPositions.size() - 1);
    uint32_t newLoopStartTick = allPositions[targetIndex];
    
    // Movement filtering to reduce jitter - only process significant changes
    uint32_t currentStart = track.getLoopStartTick();
    uint32_t movementDelta = (newLoopStartTick > currentStart) ? 
        (newLoopStartTick - currentStart) : (currentStart - newLoopStartTick);
    
    // Only process if this is a significant change (different position)
    if (newLoopStartTick != currentStart && movementDelta >= Config::TICKS_PER_16TH_STEP / 4) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop start fader: pitchbend=%d -> index=%lu/%lu -> tick=%lu (significant change)", 
                   pitchValue, targetIndex, allPositions.size(), newLoopStartTick);
        
        // Push undo snapshot before making the change
        TrackUndo::pushLoopStartSnapshot(track);
        
        // Set the new loop start point
        track.setLoopStartTick(newLoopStartTick);
        
        logger.log(CAT_MIDI, LOG_INFO, "LOOP START EDIT: Loop start moved from tick %lu to %lu", 
                   currentStart, newLoopStartTick);
        
        // Save state to SD card after loop start change
        StorageManager::saveState(looperState.getLooperState());
        logger.log(CAT_MIDI, LOG_DEBUG, "State saved to SD card after loop start change");
        
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

void NoteEditManager::refreshLoopStartEditingActivity() {
    lastLoopStartEditingActivityTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Loop start editing activity refreshed");
}

void NoteEditManager::updateLoopEndpointAfterGracePeriod(Track& track) {
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
            
            // Save state to SD card after loop endpoint update
            StorageManager::saveState(looperState.getLooperState());
            logger.log(CAT_MIDI, LOG_DEBUG, "State saved to SD card after loop endpoint update");
        }
        
        logger.log(CAT_MIDI, LOG_INFO, "LOOP ENDPOINT UPDATE: Grace period ended, loop end=%lu (start=%lu + %lu bars)", 
                   newLoopEndTick, loopStartTick, loopLengthBars);
        
        // Reset grace period state
        loopStartEditingTime = 0;
        loopStartEditingEnabled = false;
    }
}