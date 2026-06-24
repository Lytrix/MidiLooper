//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstdint>
#include <algorithm>
#include "Globals.h"
#include "Utils/SelectNavigation.h"
#include "NoteEditManager.h"

#include "ClockManager.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "EditManager.h"
#include "NoteEditFocus.h"
#include "EditStates/EditLengthNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Utils/NoteUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/ValidationUtils.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/MidiMapping.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"

NoteEditManager noteEditManager;

NoteEditManager::NoteEditManager() 
    : loopEditManager(midiHandler) {
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
        performSelectnoteFaderUpdate(track);
    }
    
    // Handle start editing grace period - re-enable note editing faders after grace period
    if (!startEditingEnabled && noteSelectionTime > 0) {
        enableStartEditing();
    }
    
    // Loop edit: grace-period endpoint updates + debounced SD writes (must run every frame
    // so a pending save still flushes after leaving LOOP_EDIT).
    loopEditManager.update();
    
    // Delegate to V2 managers for their update cycles
    faderHandler.update();
    buttonHandler.update();
}

void NoteEditManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    // Log all pitchbend messages for debugging
    logger.log(CAT_MIDI, LOG_DEBUG, "Received pitchbend: ch=%d value=%d", channel, pitchValue);
    
    // Route channel 16 based on current edit mode
    if (channel == PITCHBEND_SELECT_CHANNEL) {  // Channel 16
        if (editManager.getEditSessionType() == EditSessionType::Loop) {
            // In loop edit mode: Route to loop start fader
            loopEditManager.handleLoopStartFaderInput(pitchValue, trackManager.getSelectedTrack());
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ch=%d routed to loop start fader (LOOP_EDIT mode)", channel);
            return;
        } else {
            // In note edit mode: Route to select fader
            if (currentDriverFader == MidiMapping::FaderType::FADER_NOTE_VALUE &&
                lastDriverFaderTime > 0 &&
                (millis() - lastDriverFaderTime) < SELECTNOTE_UPDATE_DELAY) {
                auto& selectState =
                    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
                const int16_t echoDiff = abs(pitchValue - selectState.lastSentPitchbend);
                static constexpr int16_t kSelectEchoTolerance = 100;
                if (echoDiff <= kSelectEchoTolerance) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                               "Pitchbend ch=%d ignored (ch16 echo after note-value edit, diff=%d)",
                               channel, echoDiff);
                    return;
                }
            }
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
    
    // Check for loop length control first
    if (channel == MidiConfig::LoopEdit::LENGTH_CC_CHANNEL && ccNumber == MidiConfig::LoopEdit::LENGTH_CC_NUMBER) {
        loopEditManager.handleLoopLengthInput(value, trackManager.getSelectedTrack());
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
    
    logger.log(CAT_MIDI, LOG_DEBUG, "CC ignored: not on monitored channels/CC (%d/%d, %d/%d, or loop length)", 
               FINE_CC_CHANNEL, FINE_CC_NUMBER, NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER);
}

void NoteEditManager::moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick) {
    editManager.beginGeometryMutation(track, NoteEditKind::Move, true);
    uint32_t fromStart = currentNote.startTick;
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (focus.active && focus.last.pitch == currentNote.note &&
        focus.last.startTick == currentNote.startTick) {
        fromStart = focus.last.startTick;
    }
    const int32_t tickDifference =
        static_cast<int32_t>(targetTick) - static_cast<int32_t>(fromStart);

    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note movement with overlap handling: from=%lu to=%lu difference=%ld overlapNotes=%zu",
               fromStart, targetTick, tickDifference,
               editManager.getEditSession().focus.overlapNotes.size());

    editManager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    if (focus.active) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Overlap move bridge: pitch=%d, start=%lu, end=%lu",
                   focus.last.pitch,
                   static_cast<unsigned long>(focus.last.startTick),
                   static_cast<unsigned long>(focus.last.endTick));
    }

    uint32_t dummyStart = currentNote.startTick;
    uint32_t dummyEnd = currentNote.endTick;
    NoteMovementUtils::applyNoteEditChange(track, editManager, NoteMovementUtils::NoteEditChangeKind::Move,
                                           currentNote, targetTick, static_cast<int>(tickDifference),
                                           0, 0, 0, dummyStart, dummyEnd);
}

void NoteEditManager::changeNoteEndWithOverlapHandling(Track& track,
                                                       const NoteUtils::DisplayNote& currentNote,
                                                       std::uint32_t targetEndTick) {
    editManager.beginGeometryMutation(track, NoteEditKind::Length, true);
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note length change with overlap handling: pitch=%d, start=%lu, end %lu->%lu",
               currentNote.note, currentNote.startTick, currentNote.endTick, targetEndTick);
    uint32_t dummyStart = currentNote.startTick;
    uint32_t dummyEnd = currentNote.endTick;
    NoteMovementUtils::applyNoteEditChange(track, editManager,
                                           NoteMovementUtils::NoteEditChangeKind::Length,
                                           currentNote, 0, 0, targetEndTick, 0, 0, dummyStart,
                                           dummyEnd);
}

void NoteEditManager::processEncoderMovement(int rawDelta) {
    if (rawDelta == 0) {
        return;
    }

    static uint32_t lastEncoderTime = 0;
    const uint32_t now = millis();
    const uint32_t interval = now - lastEncoderTime;
    lastEncoderTime = now;

    int accel = 1;
    switch (editManager.getNoteEditSessionState().kind) {
        case NoteEditKind::Move:
            if (interval < 25) {
                accel = 24;
            } else if (interval < 50) {
                accel = 8;
            } else if (interval < 100) {
                accel = 4;
            }
            break;
        case NoteEditKind::Length:
            if (interval < 25) {
                accel = 8;
            } else if (interval < 50) {
                accel = 4;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
        case NoteEditKind::Pitch:
            if (interval < 50) {
                accel = 4;
            } else if (interval < 75) {
                accel = 3;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
        default:
            if (interval < 50) {
                accel = 4;
            } else if (interval < 75) {
                accel = 3;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
    }

    const int finalDelta = rawDelta * accel;
    if (editManager.getCurrentState() != nullptr) {
        editManager.onEncoderTurn(trackManager.getSelectedTrack(), finalDelta);
    }
}

void NoteEditManager::cycleEditMode(Track& track) {
    editManager.cycleEditSession(track);
}

void NoteEditManager::cycleEditSession(Track& track) {
    editManager.cycleEditSession(track);
}

void NoteEditManager::deleteSelectedNote(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0 && !editManager.hasLastFader1SelectRef()) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    const std::vector<NoteUtils::DisplayNote> filteredNotes =
        selectableDisplayNotesForEditUi(track);
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const uint8_t channel = track.getMidiChannel();

    int selectedIdx = editManager.getSelectedNoteIdx();
    NoteRef deleteTargetRef{};
    if (editManager.hasLastFader1SelectRef()) {
        deleteTargetRef = editManager.getLastFader1SelectRef();
        selectedIdx = filteredDisplayNoteIndexForNoteRef(channel, focus, filteredNotes, deleteTargetRef);
    }
    if (selectedIdx < 0 || selectedIdx >= static_cast<int>(filteredNotes.size())) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return;
    }
    if (deleteTargetRef.channel == 0) {
        deleteTargetRef = noteRefFromFilteredDisplayNote(channel, focus, filteredNotes, selectedIdx);
        if (deleteTargetRef.channel == 0) {
            deleteTargetRef = noteRefFromDisplay(channel, filteredNotes[static_cast<size_t>(selectedIdx)]);
        }
    }

    const NoteUtils::DisplayNote selectedNote = filteredNotes[static_cast<size_t>(selectedIdx)];

    const bool noteEditActive = editManager.isNoteEditActive();
    editManager.beginGeometryMutation(track, NoteEditKind::Delete, false);
    const bool deleteTargetDiffersFromFocus =
        noteEditActive && focus.active && !noteRefEquals(focus.moving, deleteTargetRef);
    if (deleteTargetDiffersFromFocus) {
        editManager.commitPendingOverlapNoteEdits(track);
        editManager.rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    } else if (noteEditActive && !focus.active) {
        editManager.rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    }
    editManager.commitAllPendingNoteEditActions(track);

    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t noteEnd = selectedNote.endTick;
    const std::vector<NoteUtils::DisplayNote> notesAfter =
        selectableDisplayNotesForEditUi(track);
    const int refreshedIdx = filteredDisplayNoteIndexForNoteRef(
        track.getMidiChannel(), editManager.getEditSession().focus, notesAfter, deleteTargetRef);
    if (refreshedIdx >= 0 && refreshedIdx < static_cast<int>(notesAfter.size())) {
        const NoteUtils::DisplayNote& refreshed = notesAfter[static_cast<size_t>(refreshedIdx)];
        notePitch = refreshed.note;
        noteStart = refreshed.startTick;
        noteEnd = refreshed.endTick;
    } else {
        for (const NoteUtils::DisplayNote& n : notesAfter) {
            if (n.note == deleteTargetRef.note && n.startTick == deleteTargetRef.startTick) {
                notePitch = n.note;
                noteStart = n.startTick;
                noteEnd = n.endTick;
                break;
            }
        }
    }

    logger.info("MIDI Encoder: Deleting note pitch=%d, start=%lu, end=%lu",
                notePitch, noteStart, noteEnd);

    auto& midiEvents = track.editAwareMidiEvents();
    MidiEvent* noteOnEvent = nullptr;
    for (MidiEvent& e : midiEvents) {
        if (e.type == midi::NoteOn && e.data.noteData.velocity > 0 &&
            e.data.noteData.note == notePitch && e.tick == noteStart) {
            noteOnEvent = &e;
            break;
        }
    }

    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent != nullptr) {
        noteOffEvent = NoteMovementUtils::findCorrespondingNoteOff(
            midiEvents, noteOnEvent, notePitch, noteStart, noteEnd);
    }

    int deletedCount = 0;
    auto eraseByPointer = [&](MidiEvent* needle) {
        if (needle == nullptr) {
            return;
        }
        for (auto it = midiEvents.begin(); it != midiEvents.end(); ++it) {
            if (&(*it) == needle) {
                midiEvents.erase(it);
                ++deletedCount;
                return;
            }
        }
    };
    if (noteOnEvent != nullptr || noteOffEvent != nullptr) {
        eraseByPointer(noteOffEvent);
        eraseByPointer(noteOnEvent);
    } else {
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            const bool matchOn =
                (it->type == midi::NoteOn && it->data.noteData.velocity > 0 &&
                 it->data.noteData.note == notePitch && it->tick == noteStart);
            const bool matchOff =
                ((it->type == midi::NoteOff ||
                  (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                 it->data.noteData.note == notePitch && it->tick == noteEnd);
            if (matchOn || matchOff) {
                it = midiEvents.erase(it);
                ++deletedCount;
            } else {
                ++it;
            }
        }
    }

    logger.info("MIDI Encoder: Deleted %d MIDI events for note", deletedCount);

    EditPass del{};
    del.passType = EditPassType::Note;
    del.actionType = EditActionType::Delete;
    del.propertyType = EditPropertyType::None;
    del.target = {track.getMidiChannel(), notePitch, noteStart, noteEnd};
    editManager.commitEditAction(track, EditPassVec{del});
    track.invalidateCaches();

    editManager.setSelectedNoteIdx(-1);
    editManager.rebuildNoteEditFocusAtSelect(track, -1);
    resetLengthEditingModeOnNoteSelect();

    // Since we're using dedicated faders now, we don't need to manage complex edit modes
    // Just send the current main edit mode to keep the system synchronized
    editManager.sendEditSessionChange(editManager.getEditSessionType());
    
    logger.info("MIDI Encoder: Note deleted, maintaining current edit mode");
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
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
            float normalizedPos = (float)nearestStep / (float)(numSteps - 1);  // 0.0 to 1.0
            int16_t coarseMidiPitchbend = (int16_t)(MidiConfig::Pitchbend::MIN + normalizedPos * (MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
            coarseMidiPitchbend = constrain(coarseMidiPitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
            
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
    uint32_t sentAt = millis();
    lastSelectnoteSentTime = sentAt;
    auto& selectState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
    selectState.lastSentTime = sentAt;
    lastUserSelectFaderValue = selectState.lastSentPitchbend;
    lastSelectFaderTime = sentAt;
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

// Unified Fader State Machine Implementation - now delegated to MidiFaderProcessor

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
    MidiFaderProcessor::FaderState& state = midiFaderManager.getFaderStateMutable(faderType);
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
    Track& track = trackManager.getSelectedTrack();

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            // After position/pitch edits, sync fader 1 (select) to the moved note once the motor settles.
            sendSelectnoteFaderUpdate(track);
            break;
        case MidiMapping::FaderType::FADER_SELECT:
            // Faders 2–4 are refreshed by enableStartEditing() after NOTE_SELECTION_GRACE_PERIOD.
            break;
        default:
            break;
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
        if (faderProcessor) {
            const auto& state = faderProcessor->getFaderState(faderType);
            scheduledBySelect = (state.scheduledByDriver == MidiMapping::FaderType::FADER_SELECT);
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
        midiHandler.sendProgramChange(midiFaderManager.getFaderStateMutable(faderType).channel, program);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent Program Change: ch=%d program=%d (fader %d update)", 
                   midiFaderManager.getFaderStateMutable(faderType).channel, program, faderType);
    } else if (faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Skipped program change for fader %d (CC fader) - only uses CC messages", faderType);
    }
    
    // Send position update
    sendFaderPosition(faderType, track);
    
    // Record when we sent this update and set ignore periods
    uint32_t now = millis();
    midiFaderManager.getFaderStateMutable(faderType).lastSentTime = now;
    
    // IMPORTANT: If updating any channel 15 fader, all channel 15 faders get updated together.
    // Set ignore periods for all to prevent feedback from any MIDI message causing unwanted processing.
    if (faderType == MidiMapping::FaderType::FADER_COARSE || faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = now;
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = now;
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = now;
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t targetTick, currentSixteenthStep;
        
        if (lengthEditingMode) {
            // LENGTH EDIT mode: Use note END position
            uint32_t noteEndTick = notes[static_cast<size_t>(selectedIdx)].endTick;
            // Convert to relative position
            targetTick = (noteEndTick >= loopStartTick) ? 
                (noteEndTick - loopStartTick) : (noteEndTick + loopLength - loopStartTick);
            targetTick = targetTick % loopLength;
            currentSixteenthStep = targetTick / Config::TICKS_PER_16TH_STEP;
            logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader position (LENGTH EDIT): step %lu/80 -> pitchbend %d + note 1 trigger", 
                       currentSixteenthStep, targetTick);
        } else {
            // POSITION EDIT mode: Use note START position  
            uint32_t noteStartTick = notes[static_cast<size_t>(selectedIdx)].startTick;
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
            float normalizedPos = (float)currentSixteenthStep / (float)(numSteps - 1);
            int16_t coarseMidiPitchbend = (int16_t)(MidiConfig::Pitchbend::MIN + normalizedPos * (MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
            coarseMidiPitchbend = constrain(coarseMidiPitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
            
            midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);
            
            // Send note 1 trigger on channel 15 to help motorized fader 2 update
            midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, 1, 127);
            midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, 1, 0);
            
            // Record the value we sent for smart feedback detection
            midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentPitchbend = coarseMidiPitchbend;
            
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    uint32_t loopStartTick = track.getLoopStartTick();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        uint32_t targetTick;
        
        if (lengthEditingMode) {
            // LENGTH EDIT mode: Use note END position
            uint32_t noteEndTick = notes[static_cast<size_t>(selectedIdx)].endTick;
            // Convert to relative position
            targetTick = (noteEndTick >= loopStartTick) ? 
                (noteEndTick - loopStartTick) : (noteEndTick + loopLength - loopStartTick);
            targetTick = targetTick % loopLength;
        } else {
            // POSITION EDIT mode: Use note START position
            uint32_t noteStartTick = notes[static_cast<size_t>(selectedIdx)].startTick;
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
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentCC = fineCCValue;
        
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // Note value doesn't need relative positioning - it's just the MIDI note number
        uint8_t noteValue = notes[static_cast<size_t>(selectedIdx)].note;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader position: note %d -> CC=%d + note 3 trigger", 
                   noteValue, noteValue);
        
        midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER, noteValue);
        
        // Send note 3 trigger on channel 15 to help motorized fader 4 update
        midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, 3, 127);
        midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, 3, 0);
        
        // Record the value we sent for smart feedback detection
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentCC = noteValue;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Sent note value CC=%d + note 3 trigger (note value %d)", 
                   noteValue, noteValue);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value: Invalid selectedIdx=%d, notes.size()=%lu", 
                   selectedIdx, notes.size());
    }
}

namespace {

/// When several notes share a nav slot tick, keep the active moving note selected (**focus.last**).
int resolveNoteIdxAtSlot(const std::vector<SelectNavigation::SelectNavSlot>& slots,
                         const SelectNavigation::SelectNavSlot& slot,
                         const EditManager& editManager,
                         const std::vector<NoteUtils::DisplayNote>& notes,
                         bool selectingNewTick) {
    if (slot.noteIdx < 0) {
        return slot.noteIdx;
    }
    std::vector<int> candidates;
    for (const SelectNavigation::SelectNavSlot& candidate : slots) {
        if (candidate.noteIdx >= 0 && candidate.relativeTick == slot.relativeTick &&
            candidate.noteIdx < static_cast<int>(notes.size())) {
            candidates.push_back(candidate.noteIdx);
        }
    }
    if (candidates.empty()) {
        return slot.noteIdx;
    }

    if (selectingNewTick) {
        // When jumping to a new step, keep selection deterministic: first slot at that step.
        return candidates.front();
    }

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (editManager.isNoteEditActive() && focus.active) {
        const uint8_t movingPitch = focus.last.pitch;
        const uint32_t movingStart = focus.last.startTick;
        for (int candidateIdx : candidates) {
            const NoteUtils::DisplayNote& n = notes[static_cast<size_t>(candidateIdx)];
            if (n.note == movingPitch && n.startTick == movingStart) {
                return candidateIdx;
            }
        }
        const int selectedIdx = editManager.getSelectedNoteIdx();
        if (selectedIdx >= 0 && selectedIdx < static_cast<int>(notes.size())) {
            const NoteUtils::DisplayNote& sel = notes[static_cast<size_t>(selectedIdx)];
            if (sel.note == movingPitch && sel.startTick == movingStart) {
                return selectedIdx;
            }
        }
        return slot.noteIdx;
    }
    return candidates.front();
}

}  // namespace

std::vector<NoteUtils::DisplayNote> NoteEditManager::selectableDisplayNotesForEditUi(
    const Track& track) {
    const uint32_t loopLength = track.getLoopLength();
    if (!editManager.isNoteEditActive() || loopLength == 0) {
        const auto& cachedNotes = track.getCachedNotes();
        return std::vector<NoteUtils::DisplayNote>(cachedNotes.begin(), cachedNotes.end());
    }
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    return filterSelectableDisplayNotes(track.editAwareMidiEvents(), focus,
                                        track.getMidiChannel(), loopLength);
}

std::vector<SelectNavigation::SelectNavSlot> NoteEditManager::buildSelectNavigationSlots(
    const Track& track, uint32_t bracketTick, bool includeBracketIfMissing) {
    // NOTE_EDIT uses loop storage ticks (0 origin); loopStartTick is display/loop-edit only.
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    return SelectNavigation::buildSelectNavigationSlots(
        track.getLoopLength(),
        0,
        notes,
        bracketTick,
        includeBracketIfMissing);
}

void NoteEditManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // With dedicated faders, we don't need to check edit mode - the fader is always active
    uint32_t now = millis();
    
    // SMART SELECTION STABILITY: Only process significant movements that indicate user intent
    // This prevents motorized fader feedback from being interpreted as user input
    bool isSignificantMovement = false;
    const int16_t priorSelectFaderValue = lastUserSelectFaderValue;
    
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

    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getBracketTick(), true);

    if (!slots.empty()) {
        int posIndex = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
                           (int)slots.size() - 1);
        const SelectNavigation::SelectNavSlot& slot = slots[posIndex];
        const uint32_t absoluteTargetTick = slot.relativeTick % loopLength;
        const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
        const bool selectingNewTick = absoluteTargetTick != editManager.getBracketTick();
        int noteIdx = resolveNoteIdxAtSlot(slots, slot, editManager, notes, selectingNewTick);

        // Failsafe: reject stale fader 1 echo (motor still at old slot) after position/pitch edit.
        const bool positionEditLockout =
            currentDriverFader != MidiMapping::FaderType::FADER_SELECT &&
            lastDriverFaderTime > 0 &&
            (now - lastDriverFaderTime) < (SELECTNOTE_UPDATE_DELAY + 200);
        if (positionEditLockout && absoluteTargetTick != editManager.getBracketTick()) {
            const int16_t deltaFromPrior = abs(pitchValue - priorSelectFaderValue);
            if (deltaFromPrior < SELECT_MOVEMENT_THRESHOLD) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Select fader: ignoring stale echo during edit sync (fader1 tick %lu, bracket %lu, delta=%d)",
                           absoluteTargetTick, editManager.getBracketTick(), deltaFromPrior);
                return;
            }
        }

        if (absoluteTargetTick != editManager.getBracketTick() || noteIdx != editManager.getSelectedNoteIdx()) {
            if (pendingSelectnoteUpdate) {
                pendingSelectnoteUpdate = false;
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Select fader: canceled pending fader 1 sync (user moved select fader)");
            }
            editManager.setBracketTick(absoluteTargetTick);

            if (noteIdx >= 0) {
                int notesAtPosition = 0;
                int notePosition = 0;
                for (const SelectNavigation::SelectNavSlot& s : slots) {
                    if (s.relativeTick == slot.relativeTick && s.noteIdx >= 0) {
                        notesAtPosition++;
                        if (s.noteIdx == noteIdx) {
                            notePosition = notesAtPosition;
                        }
                    }
                }

                if (notesAtPosition > 1) {
                    logger.log(CAT_MIDI, LOG_INFO,
                               "Select fader: selected note %d at tick %lu (%d/%d notes at this position)",
                               noteIdx, absoluteTargetTick, notePosition, notesAtPosition);
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected note %d at tick %lu", noteIdx,
                               absoluteTargetTick);
                }

                editManager.commitAllPendingNoteEditActions(track);
                editManager.rebuildNoteEditFocusForDisplayNote(track, notes[static_cast<size_t>(noteIdx)]);
                const NoteRef selectRef = noteRefFromFilteredDisplayNote(
                    track.getMidiChannel(), editManager.getEditSession().focus, notes, noteIdx);
                editManager.applySelectNav(track, noteIdx, absoluteTargetTick, selectRef, true);
                resetLengthEditingModeOnNoteSelect();

                referenceStep = absoluteTargetTick / Config::TICKS_PER_16TH_STEP;
                scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_SELECT);
                noteSelectionTime = millis();
                startEditingEnabled = false;
            } else {
                // Preserve pending mover edits before clearing focus on an empty-step select.
                // Without this, exiting NOTE_EDIT can drop the current editPass when the last
                // fader-1 selection is empty (focus becomes inactive before commit).
                editManager.commitAllPendingNoteEditActions(track);
                editManager.rebuildNoteEditFocusAtSelect(track, -1);
                editManager.applySelectNav(track, -1, absoluteTargetTick, {}, false);
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Select fader: selected empty step at tick %lu (no note)", absoluteTargetTick);
            }
        }
    }
}

void NoteEditManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
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
    if (!lengthEditingMode &&
        (now - noteSelectionTime) < static_cast<uint32_t>(FEEDBACK_IGNORE_PERIOD)) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Coarse fader: ignoring input during post-select routing settle");
        return;
    }
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        NoteUtils::DisplayNote currentNote = editManager.liveEditDisplayNoteAtSelect(track);
        uint32_t currentNoteStartTick = currentNote.startTick;
        uint32_t loopStartTick = track.getLoopStartTick();
        
        if (editManager.getEditSession().focus.active) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader using focus.last: pitch=%d, start=%lu",
                       currentNote.note,
                       static_cast<unsigned long>(currentNote.startTick));
        }
        
        if (lengthEditingMode) {
            // LENGTH EDIT MODE: Move the note END position in 16th step increments
            uint32_t currentNoteEndTick = currentNote.endTick;

            // NOTE_EDIT storage ticks are loop-relative (0 origin); ignore loopStartTick offset.
            uint32_t relativeEndTick = currentNoteEndTick % loopLength;
            
            // Calculate how many 16th steps are in the loop
            uint32_t totalSixteenthSteps = loopLength / Config::TICKS_PER_16TH_STEP;
            
            // Calculate current end step and offset (using relative position)
            uint32_t currentSixteenthStep = relativeEndTick / Config::TICKS_PER_16TH_STEP;
            uint32_t offsetWithinSixteenth = relativeEndTick % Config::TICKS_PER_16TH_STEP;
            
            // Map pitchbend to 16th step across entire loop
            uint32_t targetSixteenthStep = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0, totalSixteenthSteps - 1);
            
            // Calculate target end tick: new 16th step + preserved offset (relative)
            uint32_t relativeTargetEndTick = (targetSixteenthStep * Config::TICKS_PER_16TH_STEP) + offsetWithinSixteenth;
            
            // Constrain to valid range within the loop
            if (relativeTargetEndTick >= loopLength) {
                relativeTargetEndTick = loopLength - 1;
            }
            
            uint32_t targetEndTick = relativeTargetEndTick;
            
            // Calculate new note length and enforce minimum
            uint32_t newNoteDuration = NoteMovementUtils::calculateNoteLength(currentNote.startTick, targetEndTick, loopLength);
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
            
            changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
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
            uint32_t targetSixteenthStep = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0, totalSixteenthSteps - 1);
            
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
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        NoteUtils::DisplayNote currentNote = editManager.liveEditDisplayNoteAtSelect(track);
        uint32_t loopStartTick = track.getLoopStartTick();
        
        if (editManager.getEditSession().focus.active) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader using focus.last: pitch=%d, start=%lu",
                       currentNote.note,
                       static_cast<unsigned long>(currentNote.startTick));
        }
        
        if (lengthEditingMode) {
            // LENGTH EDIT MODE: Adjust note END position with fine control
            uint32_t currentNoteEndTick = currentNote.endTick;
            
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
            
            uint32_t targetEndTick = relativeTargetEndTick;
            
            // Calculate new note length and enforce minimum
            uint32_t newNoteDuration = NoteMovementUtils::calculateNoteLength(currentNote.startTick, targetEndTick, loopLength);
            uint32_t minNoteDuration = Config::TICKS_PER_16TH_STEP; // Minimum 1/16th step
            
            if (newNoteDuration < minNoteDuration) {
                // Enforce minimum note length
                targetEndTick = (currentNote.startTick + minNoteDuration) % loopLength;
                logger.log(CAT_MIDI, LOG_DEBUG, "Enforced minimum note length: %lu -> %lu ticks", newNoteDuration, minNoteDuration);
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "LENGTH EDIT (fine): Note end adjusted: offset %ld -> %ld (tick %lu -> %lu)", 
                       (int32_t)currentNoteEndTick - (int32_t)sixteenthStepStartTick, offset, currentNoteEndTick, targetEndTick);
            
            changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
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
        
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_FINE);
    }
}

void NoteEditManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
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
    
    std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
        const uint8_t currentNoteValue = liveNote.note;
        uint32_t noteStart = liveNote.startTick;
        uint32_t noteEnd = liveNote.endTick;
        if (editManager.getEditSession().focus.active) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Pitch edit using focus.last: pitch=%d, start=%lu, end=%lu",
                       currentNoteValue,
                       static_cast<unsigned long>(noteStart),
                       static_cast<unsigned long>(noteEnd));
        }
        uint8_t newNoteValue = ccValue;  // Direct 1:1 mapping from CC to MIDI note value
        
        // Constrain to valid MIDI note range (0-127)
        newNoteValue = constrain(newNoteValue, 0, 127);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: currentNote=%d newNote=%d (cc=%d)", 
                   currentNoteValue, newNoteValue, ccValue);
        
        // If the pitch isn't changing, no need to do anything
        if (currentNoteValue == newNoteValue) {
            return;
        }

        editManager.beginGeometryMutation(track, NoteEditKind::Pitch, true);

        NoteUtils::DisplayNote pitchTarget{currentNoteValue, notes[static_cast<size_t>(selectedIdx)].velocity,
                                           noteStart, noteEnd};
        const bool pitchUpdated = NoteMovementUtils::applyNoteEditChange(
            track, editManager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget,
            0, 0, 0, currentNoteValue, newNoteValue, noteStart, noteEnd);
        if (!pitchUpdated) {
            return;
        }

        const uint32_t displayEnd =
            (noteEnd >= loopLength) ? (noteEnd % loopLength) : noteEnd;
        const std::vector<NoteUtils::DisplayNote> updatedNotes = selectableDisplayNotesForEditUi(track);
        int newSelectedIdx = -1;

        // Find the updated note in the new notes list
        for (int i = 0; i < (int)updatedNotes.size(); i++) {
            if (updatedNotes[i].note == newNoteValue &&
                updatedNotes[i].startTick == noteStart &&
                updatedNotes[i].endTick == displayEnd) {
                newSelectedIdx = i;
                break;
            }
        }

        if (newSelectedIdx >= 0) {
            editManager.setSelectedNoteIdx(newSelectedIdx);
            editManager.setBracketTick(noteStart);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Updated selectedNoteIdx: %d -> %d (note with new value)",
                       selectedIdx, newSelectedIdx);
        } else {
            editManager.selectClosestNote(track, editManager.getBracketTick());
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Resynced selection after pitch (merged note idx not found)");
        }

        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        // Set up driver tracking for note value fader
        this->currentDriverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
        this->lastDriverFaderTime = millis();
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_NOTE_VALUE);
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

void NoteEditManager::resetLengthEditingModeOnSessionBoundary() {
    if (!lengthEditingMode) {
        return;
    }
    lengthEditingMode = false;
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    logger.info("[MIDI] Length editing mode DISABLED (edit session boundary)");
}

void NoteEditManager::resetLengthEditingModeOnNoteSelect() {
    if (lengthEditingMode) {
        lengthEditingMode = false;
        logger.info("[MIDI] Length editing mode DISABLED (note select)");
    }
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    lastUserCoarseFaderValue = 0;
    lastCoarseFaderTime = 0;
}

void NoteEditManager::toggleLengthEditingMode() {
    uint32_t now = millis();
    
    // Debounce protection
    if (now - lastLengthModeToggleTime < LENGTH_MODE_DEBOUNCE_TIME) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Length mode toggle ignored (debounce protection)");
        return;
    }
    lastLengthModeToggleTime = now;
    
    const bool enabling = !lengthEditingMode;
    lengthEditingMode = enabling;
    
    if (lengthEditingMode) {
        logger.info("[MIDI] Length editing mode ENABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE END position (length editing)");
    } else {
        currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
        lastDriverFaderTime = now;
        lastUserCoarseFaderValue = 0;
        lastCoarseFaderTime = 0;
        logger.info("[MIDI] Length editing mode DISABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE START position (position editing)");
        Track& track = trackManager.getSelectedTrack();
        editManager.commitAllPendingNoteEditActions(track);
    }

    // Send fader updates to reflect the new mode (like select note does)
    Track& track = trackManager.getSelectedTrack();
    
    // Only update if we have notes to edit
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    if (!notes.empty() && editManager.getSelectedNoteIdx() >= 0 && editManager.getSelectedNoteIdx() < (int)notes.size()) {
        const int selectedIdx = editManager.getSelectedNoteIdx();
        referenceStep = (lengthEditingMode ? notes[static_cast<size_t>(selectedIdx)].endTick
                                           : notes[static_cast<size_t>(selectedIdx)].startTick) /
                        Config::TICKS_PER_16TH_STEP;
        // Schedule fader updates with staggered delays (like enableStartEditing does).
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_SELECT);
    }
}

void NoteEditManager::onTrackChanged(Track& newTrack) {
    // If we're in loop edit mode, send the new track's loop length as CC feedback
    if (editManager.getEditSessionType() == EditSessionType::Loop) {
        loopEditManager.onTrackChanged(newTrack);
    }
}