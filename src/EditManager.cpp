//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManager.h"
#include "EditApply.h"
#include "EditNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Track.h"
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"
#include "MidiEvent.h"
#include "TrackUndo.h"
#include "StorageManager.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "MidiHandler.h"
#include "NoteEditManager.h"
#include <algorithm>
#include <map>
#include <vector>
#include <cmath>

using DisplayNote = NoteUtils::DisplayNote;

#include "Utils/MidiEventVecFnvHash.h"
#include "NoteEditFocus.h"

namespace {

uint32_t computeEditAwareHash(const Track& track) {
    return midiEventVecFnv1aHash(track.editAwareMidiEvents());
}

EditChangeList buildMoveCommitChanges(const EditManager& manager, const Track& track) {
    EditChangeList changes;
    if (!manager.movingNote.active) {
        return changes;
    }
    const uint8_t channel = track.getMidiChannel();
    EditChange move;
    move.type = EditChangeType::MoveNote;
    move.target = {channel, manager.movingNote.origPitch, manager.movingNote.origStart,
                   manager.movingNote.origEnd};
    move.newStartTick = manager.movingNote.lastStart;
    move.newEndTick = manager.movingNote.lastEnd;
    changes.push_back(move);
    for (const auto& [ref, entry] : manager.getNoteEditSession().focus.overlapNotes) {
        (void)ref;
        if (entry.state != OverlapNoteStoreState::Hidden) {
            continue;
        }
        EditChange del;
        del.type = EditChangeType::DeleteNote;
        del.target = entry.ref;
        changes.push_back(del);
    }
    return changes;
}

void EditManager::commitPendingMoveAction(Track& track) {
    if (!noteEditSession.active || !movingNote.active) {
        return;
    }
    const bool positionChanged = movingNote.lastStart != movingNote.origStart;
    bool hasOverlapDeletes = false;
    for (const auto& [ref, entry] : noteEditSession.focus.overlapNotes) {
        (void)ref;
        if (entry.state == OverlapNoteStoreState::Hidden) {
            hasOverlapDeletes = true;
            break;
        }
    }
    if (!positionChanged && !hasOverlapDeletes) {
        return;
    }
    const EditId id = commitEditAction(track, buildMoveCommitChanges(*this, track));
    if (id == kInvalidEditId) {
        return;
    }
    movingNote.origStart = movingNote.lastStart;
    movingNote.origEnd = movingNote.lastEnd;
    movingNote.origPitch = movingNote.note;
    for (auto it = noteEditSession.focus.overlapNotes.begin();
         it != noteEditSession.focus.overlapNotes.end();) {
        if (it->second.state == OverlapNoteStoreState::Hidden) {
            it = noteEditSession.focus.overlapNotes.erase(it);
        } else {
            ++it;
        }
    }
    sessionDeletedNotes.clear();
    sessionShortenedVictims.clear();
}

void EditManager::commitPendingLengthAction(Track& track) {
    if (!noteEditSession.active || !movingNote.active) {
        return;
    }
    const uint32_t baselineEnd = noteEditSession.focus.active
                                     ? noteEditSession.focus.commitBaseline.endTick
                                     : movingNote.origEnd;
    const uint32_t baselineStart = noteEditSession.focus.active
                                       ? noteEditSession.focus.commitBaseline.startTick
                                       : movingNote.origStart;
    const uint8_t baselinePitch = noteEditSession.focus.active
                                      ? noteEditSession.focus.commitBaseline.pitch
                                      : movingNote.origPitch;
    if (movingNote.lastEnd == baselineEnd) {
        return;
    }
    EditChange ch;
    ch.type = EditChangeType::ChangeLength;
    ch.target = {track.getMidiChannel(), baselinePitch, baselineStart, baselineEnd};
    ch.newEndTick = movingNote.lastEnd;
    const EditId id = commitEditAction(track, EditChangeList{ch});
    if (id == kInvalidEditId) {
        return;
    }
    if (noteEditSession.focus.active) {
        noteEditSession.focus.commitBaseline.endTick = movingNote.lastEnd;
        noteEditSession.focus.last.endTick = movingNote.lastEnd;
    }
    movingNote.origEnd = movingNote.lastEnd;
    logger.log(CAT_TRACK, LOG_INFO, "Edit committed ChangeLength end=%lu",
               static_cast<unsigned long>(movingNote.lastEnd));
}

void EditManager::commitPendingPitchAction(Track& track) {
    if (!noteEditSession.active || !movingNote.active) {
        return;
    }
    if (movingNote.note == movingNote.origPitch) {
        return;
    }
    EditChange ch;
    ch.type = EditChangeType::ChangePitch;
    ch.target = {track.getMidiChannel(), movingNote.origPitch, movingNote.origStart,
                 movingNote.origEnd};
    ch.newPitch = movingNote.note;
    const EditId id = commitEditAction(track, EditChangeList{ch});
    if (id == kInvalidEditId) {
        return;
    }
    movingNote.origPitch = movingNote.note;
}

void EditManager::commitAllPendingNoteEditActions(Track& track) {
    commitPendingMoveAction(track);
    commitPendingLengthAction(track);
    commitPendingPitchAction(track);
}

void EditManager::rebuildNoteEditFocusAtSelect(Track& track, int selectedNoteIdx) {
    if (!noteEditSession.active) {
        noteEditSession.focus.clear();
        return;
    }
    rebuildNoteEditFocusFromStore(noteEditSession.focus, sessionMidiEvents(),
                                track.getMidiChannel(), track.getLoopLength(),
                                selectedNoteIdx);
}

}  // namespace

EditManager editManager;

void EditManager::openNoteEditSession(Track& track) {
    if (noteEditSession.active) {
        return;
    }
    Loop& loop = track.getActiveLoop();
    noteEditSession.active = true;
    noteEditSession.spanIndex = 0;
    noteEditSession.spanEditIds.clear();
    noteEditSession.pendingChanges.clear();
    noteEditSession.undoStack.clear();
    loop.rematerializeEditView(noteEditSession.store.mutStore());
    noteEditSession.store.discardFlatCache();
    logger.debug("NoteEditSession opened span=0");
}

void EditManager::closeNoteEditSpan(Track& track) {
    if (!noteEditSession.active) {
        return;
    }
    if (!noteEditSession.spanEditIds.empty()) {
        TrackUndo::pushNoteEditSessionCommitted(track, noteEditSession.spanIndex,
                                                noteEditSession.spanEditIds);
        logger.log(CAT_TRACK, LOG_INFO, "NoteEditSessionCommitted span=%u edits=%u",
                   static_cast<unsigned>(noteEditSession.spanIndex),
                   static_cast<unsigned>(noteEditSession.spanEditIds.size()));
    }
    noteEditSession.spanEditIds.clear();
    noteEditSession.undoStack.clear();
    ++noteEditSession.spanIndex;
}

void EditManager::closeNoteEditSession(Track& track) {
    if (!noteEditSession.active) {
        return;
    }
    closeNoteEditSpan(track);
    noteEditSession.store.mutStore().clear();
    noteEditSession.store.discardFlatCache();
    noteEditSession.active = false;
    noteEditSession.spanIndex = 0;
    noteEditSession.pendingChanges.clear();
}

EditId EditManager::commitEditAction(Track& track, EditChangeList changes) {
    if (!noteEditSession.active || changes.empty()) {
        return kInvalidEditId;
    }
    Loop& loop = track.getActiveLoop();
    const EditId id = loop.saveEdit(noteEditSession.spanIndex, std::move(changes));
    if (id != kInvalidEditId) {
        noteEditSession.spanEditIds.push_back(id);
        // NoteEditSession.store is the single ledger view (m8-edit Decision 7): rebuild it
        // from applyEdits(takes, edits) so the committed working buffer equals the immutable
        // takes + Edit ledger. Live fader edits only mutate the flat preview cache; without
        // this rebuild the store froze at session-open state and the preview cache drifted
        // from the ledger (stale-note-off / loop-end stretch on reselect).
        loop.rematerializeEditView(noteEditSession.store.mutStore());
        noteEditSession.store.discardFlatCache();
        // Keep loop.editFlat_ aligned with the same ledger (display / getCachedNotesForSlot).
        (void)loop.midiEvents();
    }
    track.invalidateCaches();
    return id;
}

void EditManager::pushSessionUndoBeforeMutation(Track& track) {
    if (!noteEditSession.active) {
        return;
    }
    if (noteEditSession.store.isFlatDirty()) {
        noteEditSession.store.syncFlatToStore();
    }
    noteEditSession.undoStack.pushBeforeMutation(noteEditSession.store.readStore());
    (void)track;
}

bool EditManager::sessionUndo(Track& track) {
    if (!noteEditSession.active || !noteEditSession.undoStack.canUndo()) {
        return false;
    }
    const auto snap = noteEditSession.undoStack.popUndoSnapshot();
    if (!snap) {
        return false;
    }
    noteEditSession.store.restoreFromSnapshot(snap->cloneShared());
    track.invalidateCaches();
    return true;
}

bool EditManager::sessionRedo(Track& track) {
    if (!noteEditSession.active || !noteEditSession.undoStack.canRedo()) {
        return false;
    }
    const auto snap = noteEditSession.undoStack.popRedoSnapshot();
    if (!snap) {
        return false;
    }
    noteEditSession.store.restoreFromSnapshot(snap->cloneShared());
    track.invalidateCaches();
    return true;
}

MidiEventVec& EditManager::sessionMidiEvents() {
    return noteEditSession.store.mutFlat();
}

const MidiEventVec& EditManager::sessionMidiEvents() const {
    return noteEditSession.store.readFlat();
}

MidiEventVec& EditManager::editMidiEvents(Track& track) {
    if (noteEditSession.active) {
        return sessionMidiEvents();
    }
    return track.getMidiEvents();
}

const MidiEventVec& EditManager::editMidiEvents(const Track& track) const {
    if (noteEditSession.active) {
        return sessionMidiEvents();
    }
    return track.getMidiEvents();
}

EditManager::EditManager() {
    currentState = nullptr; // Start with no state
}

void EditManager::setState(EditNoteState* newState, Track& track, uint32_t startTick) {
    if (currentState) {
        if (currentState == &startNoteState) {
            auto* s = static_cast<EditStartNoteState*>(currentState);
            if (computeEditAwareHash(track) != s->getInitialHash()) {
                commitEditAction(track, buildMoveCommitChanges(*this, track));
            }
        } else if (currentState == &lengthNoteState) {
            auto* l = static_cast<EditLengthNoteState*>(currentState);
            if (computeEditAwareHash(track) != l->getInitialHash()) {
                const NoteRef target = l->getTargetRef();
                const auto& notes = track.getCachedNotes();
                EditChange ch;
                ch.type = EditChangeType::ChangeLength;
                ch.target = target;
                if (getSelectedNoteIdx() >= 0 &&
                    getSelectedNoteIdx() < static_cast<int>(notes.size())) {
                    ch.newEndTick = notes[getSelectedNoteIdx()].endTick;
                } else {
                    ch.newEndTick = target.endTick;
                }
                commitEditAction(track, EditChangeList{ch});
            }
        } else if (currentState == &pitchNoteState) {
            auto* p = static_cast<EditPitchNoteState*>(currentState);
            if (computeEditAwareHash(track) != p->getInitialHash()) {
                const NoteRef target = p->getTargetRef();
                const auto& notes = track.getCachedNotes();
                EditChange ch;
                ch.type = EditChangeType::ChangePitch;
                ch.target = target;
                if (getSelectedNoteIdx() >= 0 &&
                    getSelectedNoteIdx() < static_cast<int>(notes.size())) {
                    ch.newPitch = notes[getSelectedNoteIdx()].note;
                } else {
                    ch.newPitch = target.note;
                }
                commitEditAction(track, EditChangeList{ch});
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

void EditManager::selectNoteAtBracket(Track& track, uint32_t startTick) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        selectClosestNote(track, startTick);
        return;
    }
    const uint32_t bracket = startTick % loopLength;
    const auto& notes = track.getCachedNotes();
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        if (notes[i].startTick % loopLength == bracket) {
            selectedNoteIdx = i;
            bracketTick = bracket;
            hasMovedBracket = true;
            return;
        }
    }
    selectClosestNote(track, startTick);
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
    if (currentState == &noteHomeState) {
        setState(&startNoteState, track, bracketTick);
    } else {
        setState(&noteHomeState, track, bracketTick);
    }
}

void EditManager::enterEditMode(EditNoteState* newState, uint32_t startTick) {
    auto& track = trackManager.getSelectedTrack();
    noteEditManager.resetLengthEditingModeOnSessionBoundary();
    if (!noteEditSession.active) {
        openNoteEditSession(track);
    }
    setState(newState, track, startTick);
}

void EditManager::exitEditMode(Track& track) {
    noteEditManager.resetLengthEditingModeOnSessionBoundary();
    commitAllPendingNoteEditActions(track);
    if (currentState == &startNoteState) {
        auto* s = static_cast<EditStartNoteState*>(currentState);
        if (computeEditAwareHash(track) != s->getInitialHash()) {
            commitEditAction(track, buildMoveCommitChanges(*this, track));
        }
    } else if (currentState == &lengthNoteState) {
        auto* l = static_cast<EditLengthNoteState*>(currentState);
        if (computeEditAwareHash(track) != l->getInitialHash()) {
            const NoteRef target = l->getTargetRef();
            const auto& notes = track.getCachedNotes();
            EditChange ch;
            ch.type = EditChangeType::ChangeLength;
            ch.target = target;
            if (getSelectedNoteIdx() >= 0 &&
                getSelectedNoteIdx() < static_cast<int>(notes.size())) {
                ch.newEndTick = notes[getSelectedNoteIdx()].endTick;
            } else {
                ch.newEndTick = target.endTick;
            }
            commitEditAction(track, EditChangeList{ch});
        }
    } else if (currentState == &pitchNoteState) {
        auto* p = static_cast<EditPitchNoteState*>(currentState);
        if (computeEditAwareHash(track) != p->getInitialHash()) {
            const NoteRef target = p->getTargetRef();
            const auto& notes = track.getCachedNotes();
            EditChange ch;
            ch.type = EditChangeType::ChangePitch;
            ch.target = target;
            if (getSelectedNoteIdx() >= 0 &&
                getSelectedNoteIdx() < static_cast<int>(notes.size())) {
                ch.newPitch = notes[getSelectedNoteIdx()].note;
            } else {
                ch.newPitch = target.note;
            }
            commitEditAction(track, EditChangeList{ch});
        }
    }

    closeNoteEditSpan(track);
    if (track.getActiveLoop().isEditStateDirty()) {
        StorageManager::requestUrgentEditSave();
    }
    track.invalidateCaches();

    selectedNoteIdx = -1;
    hasMovedBracket = false;
    if (currentState) currentState->onExit(*this, track);
    currentState = nullptr;
    closeNoteEditSession(track);
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
    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL, mode);
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
    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL, mode);
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
    
    midiHandler.sendControlChange(MidiConfig::LoopEdit::LENGTH_CC_CHANNEL, MidiConfig::LoopEdit::LENGTH_CC_NUMBER, ccValue);
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
    if (noteEditSession.active &&
        (currentState == &startNoteState || currentState == &lengthNoteState ||
         currentState == &pitchNoteState)) {
        return noteEditSession.undoStack.undoCount();
    }
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

