//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManager.h"
#include "EditNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Track.h"
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"
#include "MidiEvent.h"
#include "TrackUndo.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "MidiHandler.h"
#include "NoteEditManager.h"
#include <algorithm>
#include <map>
#include <vector>
#include <cmath>

using DisplayNote = NoteUtils::DisplayNote;

EditManager editManager;

EditManager::EditManager() {
    currentState = nullptr; // Start with no state
}

void EditManager::setState(EditState* newState, Track& track, uint32_t startTick) {
    // If we're switching away from an edit state, commit or discard its undo snapshot
    if (currentState) {
        // Handle hash-based commit-on-exit for start-note edits
        if (currentState == &startNoteState) {
            auto* s = static_cast<EditStartNoteState*>(currentState);
            if (TrackUndo::computeMidiHash(track) == s->getInitialHash()) {
                TrackUndo::popLastUndo(track);
                logger.debug("No net change in start-note edit, popped undo snapshot");
            }
        }
        // Handle hash-based commit-on-exit for length-note edits
        else if (currentState == &lengthNoteState) {
            auto* l = static_cast<EditLengthNoteState*>(currentState);
            if (TrackUndo::computeMidiHash(track) == l->getInitialHash()) {
                TrackUndo::popLastUndo(track);
                logger.debug("No net change in length-note edit, popped undo snapshot");
            }
        }
        // Handle hash-based commit-on-exit for pitch-note edits
        else if (currentState == &pitchNoteState) {
            auto* p = static_cast<EditPitchNoteState*>(currentState);
            if (TrackUndo::computeMidiHash(track) == p->getInitialHash()) {
                TrackUndo::popLastUndo(track);
                logger.debug("No net change in pitch edit, popped undo snapshot");
            }
        }
        currentState->onExit(*this, track);
    }
    currentState = newState;
    // If entering a note-edit or pitch-edit state, record undo count to freeze display
    if (currentState == &startNoteState || currentState == &lengthNoteState || currentState == &pitchNoteState) {
        undoCountOnStateEnter = TrackUndo::getUndoCount(track);
    }
    if (currentState) currentState->onEnter(*this, track, startTick);
}

void EditManager::onEncoderTurn(Track& track, int delta) {
    if (currentState) {
        int step = (delta > 0) ? 1 : -1;
        for (int i = 0; i < abs(delta); ++i) {
            currentState->onEncoderTurn(*this, track, step);
        }
    }
}

void EditManager::onButtonPress(Track& track) {
    if (currentState) currentState->onButtonPress(*this, track);
}

void EditManager::selectClosestNote(Track& track, uint32_t startTick) {
    // Use cached note list for optimal performance
    const auto& notes = track.getCachedNotes();
    
    // If no notes, just place bracket at exact tick
    if (notes.empty()) {
        bracketTick = startTick % track.getLoopLength();
        selectedNoteIdx = -1;
        hasMovedBracket = true;
        return;
    }
    // Find nearest by tick distance
    uint32_t modStart = startTick % track.getLoopLength();
    uint32_t bestDist = track.getLoopLength();
    int bestIdx = 0;
    for (int i = 0; i < (int)notes.size(); ++i) {
        uint32_t noteTick = notes[i].startTick % track.getLoopLength();
        uint32_t dist = std::min((noteTick + track.getLoopLength() - modStart) % track.getLoopLength(),
                                 (modStart + track.getLoopLength() - noteTick) % track.getLoopLength());
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    // Update selection and bracket
    selectedNoteIdx = bestIdx;
    bracketTick = notes[bestIdx].startTick % track.getLoopLength();
    hasMovedBracket = true;
}

void EditManager::moveBracket(Track& track, int delta) {
    if (!notesAtBracketTick.empty() && notesAtBracketTick.size() > 1) {
        if (delta > 0) {
            notesAtBracketIdx++;
            if (notesAtBracketIdx >= (int)notesAtBracketTick.size()) {
                // Move to next tick group
                moveBracket(1, track, Config::TICKS_PER_16TH_STEP);
                return;
            }
        } else if (delta < 0) {
            notesAtBracketIdx--;
            if (notesAtBracketIdx < 0) {
                // Move to previous tick group
                moveBracket(-1, track, Config::TICKS_PER_16TH_STEP);
                return;
            }
        }
        selectedNoteIdx = notesAtBracketTick[notesAtBracketIdx];
        return;
    }
    // Otherwise, move bracket as before
    moveBracket(delta, track, Config::TICKS_PER_16TH_STEP);
}

void EditManager::switchToNextState(Track& track) {
    // Example: cycle between noteState and startNoteState
    if (currentState == &noteState) {
        setState(&startNoteState, track, bracketTick);
    } else {
        setState(&noteState, track, bracketTick);
    }
}

void EditManager::enterEditMode(EditState* newState, uint32_t startTick) {
    auto& track = trackManager.getSelectedTrack();
    setState(newState, track, startTick);
}

void EditManager::exitEditMode(Track& track) {
    // Check if we need to pop undo due to no net changes before exiting
    if (currentState == &startNoteState) {
        auto* s = static_cast<EditStartNoteState*>(currentState);
        if (TrackUndo::computeMidiHash(track) == s->getInitialHash()) {
            TrackUndo::popLastUndo(track);
            logger.debug("No net change in start edit, popped undo snapshot on exit");
        }
    }
    else if (currentState == &lengthNoteState) {
        auto* l = static_cast<EditLengthNoteState*>(currentState);
        if (TrackUndo::computeMidiHash(track) == l->getInitialHash()) {
            TrackUndo::popLastUndo(track);
            logger.debug("No net change in length edit, popped undo snapshot on exit");
        }
    }
    else if (currentState == &pitchNoteState) {
        auto* p = static_cast<EditPitchNoteState*>(currentState);
        if (TrackUndo::computeMidiHash(track) == p->getInitialHash()) {
            TrackUndo::popLastUndo(track);
            logger.debug("No net change in pitch edit, popped undo snapshot on exit");
        }
    }
    
    selectedNoteIdx = -1;
    hasMovedBracket = false;
    if (currentState) currentState->onExit(*this, track);
    currentState = nullptr;
}

void EditManager::moveBracket(int delta, const Track& track, uint32_t ticksPerStep) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    // Use cached note list for optimal performance
    const auto& notes = track.getCachedNotes();
    
    const uint32_t SNAP_WINDOW = 24;
    if (delta > 0) {
        uint32_t targetTick = (bracketTick + ticksPerStep) % loopLength;
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - targetTick) % loopLength,
                                     (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startTick % loopLength;
            selectedNoteIdx = snapIdx;
        } else {
            bracketTick = targetTick;
            selectedNoteIdx = -1;
        }
    } else if (delta < 0) {
        uint32_t targetTick = (bracketTick + loopLength - (ticksPerStep % loopLength)) % loopLength;
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - targetTick) % loopLength,
                                     (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startTick % loopLength;
            selectedNoteIdx = snapIdx;
        } else {
            bracketTick = targetTick;
            selectedNoteIdx = -1;
        }
    }
    bracketTick = bracketTick % loopLength;
}

void EditManager::selectNextNote(const Track& track) {
    moveBracket(1, track, 1);
}

void EditManager::selectPrevNote(const Track& track) {
    moveBracket(-1, track, 1);
}

void EditManager::enterPitchEditMode(Track& track) {
    setState(&pitchNoteState, track, bracketTick);
}

void EditManager::exitPitchEditMode(Track& track) {
    exitEditMode(track);
}

// EditModeManager functionality
void EditManager::cycleEditMode(Track& track) {
    switch (currentEditMode) {
        case EDIT_MODE_NONE:
        case EDIT_MODE_SELECT:
            currentEditMode = EDIT_MODE_START;
            setState(&startNoteState, track, bracketTick);
            break;
        case EDIT_MODE_START:
            currentEditMode = EDIT_MODE_LENGTH;
            setState(&lengthNoteState, track, bracketTick);
            break;
        case EDIT_MODE_LENGTH:
            currentEditMode = EDIT_MODE_PITCH;
            setState(&pitchNoteState, track, bracketTick);
            break;
        case EDIT_MODE_PITCH:
            currentEditMode = EDIT_MODE_SELECT;
            setState(&selectNoteState, track, bracketTick);
            break;
    }
    
    sendEditModeProgram(currentEditMode);
    logger.log(CAT_TRACK, LOG_DEBUG, "Edit mode cycled to: %d", currentEditMode);
}

void EditManager::enterNextEditMode(Track& track) {
    cycleEditMode(track);
}

void EditManager::sendEditModeProgram(EditModeState mode) {
    // Send program change to indicate current edit mode
    midiHandler.sendProgramChange(PROGRAM_CHANGE_CHANNEL, mode);
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent edit mode program: %d", mode);
}

// LoopManager functionality
void EditManager::cycleMainEditMode(Track& track) {
    switch (currentMainEditMode) {
        case MAIN_MODE_NOTE_EDIT:
            currentMainEditMode = MAIN_MODE_LOOP_EDIT;
            break;
        case MAIN_MODE_LOOP_EDIT:
            currentMainEditMode = MAIN_MODE_NOTE_EDIT;
            break;
    }
    
    sendMainEditModeChange(currentMainEditMode);
    logger.log(CAT_TRACK, LOG_DEBUG, "Main edit mode cycled to: %d", currentMainEditMode);
}

void EditManager::sendMainEditModeChange(uint8_t mode) {
    // Send program change to indicate main edit mode
    midiHandler.sendProgramChange(PROGRAM_CHANGE_CHANNEL, mode);
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent main edit mode program: %d", mode);
}

void EditManager::sendCurrentLoopLengthCC(Track& track) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    // Convert loop length to bars (1-8 bars)
    uint8_t bars = loopLength / Config::TICKS_PER_BAR;
    if (bars == 0) bars = 1;
    if (bars > 8) bars = 8;
    
    // Convert to CC value (0-127)
    uint8_t ccValue = ((bars - 1) * 127) / 7; // Map 1-8 bars to 0-127
    
    midiHandler.sendControlChange(LOOP_LENGTH_CC_CHANNEL, LOOP_LENGTH_CC_NUMBER, ccValue);
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent loop length CC: bars=%d cc=%d", bars, ccValue);
}

void EditManager::onTrackChanged(Track& newTrack) {
    // Reset edit state when track changes
    currentEditMode = EDIT_MODE_NONE;
    currentState = nullptr;
    selectedNoteIdx = -1;
    hasMovedBracket = false;
    
    // Send current loop length for new track
    sendCurrentLoopLengthCC(newTrack);
    
    logger.log(CAT_TRACK, LOG_DEBUG, "Edit state reset for new track");
}

void EditManager::setMainEditMode(MainEditMode mode) {
    currentMainEditMode = mode;
    sendMainEditModeChange(mode);
    logger.log(CAT_TRACK, LOG_DEBUG, "Main edit mode set to: %d", mode);
}

size_t EditManager::getDisplayUndoCount(const Track& track) const {
    // During active edit states, show the frozen count
    if (currentState == &startNoteState || currentState == &lengthNoteState || currentState == &pitchNoteState) {
        return undoCountOnStateEnter;
    }
    // Otherwise show actual undo count
    return TrackUndo::getUndoCount(track);
}

void EditManager::setSelectedNoteIdx(int idx) {
    if (selectedNoteIdx != idx) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection changed: %d -> %d", selectedNoteIdx, idx);
    }
    selectedNoteIdx = idx;
}

void EditManager::resetSelection() {
    if (selectedNoteIdx != -1) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection reset: %d -> -1", selectedNoteIdx);
    }
    selectedNoteIdx = -1;
}

