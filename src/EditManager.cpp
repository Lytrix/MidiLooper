#include "EditManager.h"
#include "EditNoteState.h"
#include "Track.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"
#include "MidiEvent.h"
#include <map>
#include "TrackUndo.h"
#include "Logger.h"
#include "NoteUtils.h"

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
    if (currentState == &startNoteState || currentState == &pitchNoteState) {
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
    const auto& midiEvents = track.getEvents();
    // Reconstruct note list using shared helper
    auto notes = NoteUtils::reconstructNotes(midiEvents, track.getLength());
    
    // If no notes, just place bracket at exact tick
    if (notes.empty()) {
        bracketTick = startTick % track.getLength();
        selectedNoteIdx = -1;
        hasMovedBracket = true;
        return;
    }
    // Find nearest by tick distance
    uint32_t modStart = startTick % track.getLength();
    uint32_t bestDist = track.getLength();
    int bestIdx = 0;
    for (int i = 0; i < (int)notes.size(); ++i) {
        uint32_t noteTick = notes[i].startTick % track.getLength();
        uint32_t dist = std::min((noteTick + track.getLength() - modStart) % track.getLength(),
                                 (modStart + track.getLength() - noteTick) % track.getLength());
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    // Update selection and bracket
    selectedNoteIdx = bestIdx;
    bracketTick = notes[bestIdx].startTick % track.getLength();
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
    selectedNoteIdx = -1;
    hasMovedBracket = false;
    if (currentState) currentState->onExit(*this, track);
    currentState = nullptr;
}

void EditManager::moveBracket(int delta, const Track& track, uint32_t ticksPerStep) {
    const auto& midiEvents = track.getEvents();
    uint32_t loopLength = track.getLength();
    if (loopLength == 0) return;
    // Reconstruct note list using shared helper
    auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    
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
    previousState = currentState;
    setState(&pitchNoteState, track, bracketTick);
}

void EditManager::exitPitchEditMode(Track& track) {
    if (previousState) setState(previousState, track, bracketTick);
    previousState = nullptr;
}

size_t EditManager::getDisplayUndoCount(const Track& track) const {
    // During active edit states, show the frozen count
    if (currentState == &startNoteState || currentState == &pitchNoteState) {
        return undoCountOnStateEnter;
    }
    // Otherwise show actual undo count
    return TrackUndo::getUndoCount(track);
}

