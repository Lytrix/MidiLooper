//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <cmath>
#include <vector>

#include "ControlSurfaceManager.h"
#include "EditManager.h"
#include "EditNoteState.h"
#include "EditSession.h"
#include "Globals.h"
#include "Logger.h"
#include "MidiConfig.h"
#include "NoteEditSessionState.h"
#include "StorageManager.h"
#include "TrackManager.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/SelectNavigation.h"
#include "Utils/ValidationUtils.h"

using DisplayNote = NoteUtils::DisplayNote;

EDIT_MANAGER_IMPL_MEM void EditManager::setState(EditNoteState* newState, Track& track, uint32_t startTick) {
    if (currentState) {
        if (currentState == &startNoteState || currentState == &lengthNoteState ||
            currentState == &pitchNoteState) {
            commitAllPendingNoteEditActions(track);
        }
        currentState->onExit(*this, track);
    }
    currentState = newState;
    if (currentState) currentState->onEnter(*this, track, startTick);
}

EDIT_MANAGER_IMPL_MEM void EditManager::onEncoderTurn(Track& track, int delta) {
    if (!isNoteEditActive() || currentState == nullptr) {
        return;
    }
    if (isGeometryEditKind(sessionState.kind)) {
        pushSessionUndoOnKindChange(track, sessionState.kind);
    }
    int step = (delta > 0) ? 1 : -1;
    for (int i = 0; i < abs(delta); ++i) {
        currentState->onEncoderTurn(*this, track, step);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::onButtonPress(Track& track) {
    if (!isNoteEditActive() || currentState == nullptr) {
        return;
    }
    currentState->onButtonPress(*this, track);
}

EDIT_MANAGER_IMPL_MEM void EditManager::selectClosestNote(Track& track, uint32_t startTick) {
    const auto notes = selectableDisplayNotesForEditUi(track);
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const uint32_t loopStartTick = noteEditLoopStartTick(track);
    if (notes.empty() || loopLength == 0) {
        selectedTick = loopLength > 0
                          ? SelectNavigation::displayPhaseTick(startTick, loopLength)
                          : 0;
        applySelectNav(track, selectedTick, kInvalidNoteId);
        hasMovedBracket = true;
        return;
    }
    const uint32_t modStart = SelectNavigation::displayPhaseTick(startTick, loopLength);
    uint32_t bestDist = loopLength;
    int bestIdx = 0;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const uint32_t noteTick = displayStartTickFromStorageNote(notes[static_cast<size_t>(i)].startTick,
                                                                  loopStartTick, loopLength);
        const uint32_t dist =
            std::min((noteTick + loopLength - modStart) % loopLength,
                     (modStart + loopLength - noteTick) % loopLength);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    const DisplayNote& dn = notes[static_cast<size_t>(bestIdx)];
    selectedTick = displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength);
    applySelectNav(track, selectedTick, dn.noteId);
    hasMovedBracket = true;
}

EDIT_MANAGER_IMPL_MEM void EditManager::selectNoteAtBracket(Track& track, uint32_t startTick) {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        selectClosestNote(track, startTick);
        return;
    }
    const uint32_t loopStartTick = noteEditLoopStartTick(track);
    const uint32_t bracket = SelectNavigation::displayPhaseTick(startTick, loopLength);
    const auto notes = selectableDisplayNotesForEditUi(track);
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const uint32_t noteDisplay =
            displayStartTickFromStorageNote(notes[static_cast<size_t>(i)].startTick, loopStartTick,
                                            loopLength);
        if (noteDisplay == bracket) {
            const DisplayNote& dn = notes[static_cast<size_t>(i)];
            selectedTick = bracket;
            applySelectNav(track, selectedTick, dn.noteId);
            hasMovedBracket = true;
            return;
        }
    }
    selectClosestNote(track, startTick);
}

EDIT_MANAGER_IMPL_MEM void EditManager::moveBracket(Track& track, int delta) {
    moveBracket(delta, track, Config::TICKS_PER_16TH_STEP);
}

EDIT_MANAGER_IMPL_MEM void EditManager::stepSelectNavSlot(Track& track, int delta) {
    if (!ValidationUtils::validateLoopLength(noteEditLoopLengthTicks(track)) || delta == 0) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, selectedTick, true);
    if (slots.empty()) {
        return;
    }

    const EditorSelection& sel = sessionState.selection;
    int slotIdx = SelectNavigation::findSlotIndexForNoteId(
        slots, selectableDisplayNotesForEditUi(track), sel.primaryNote,
        selectedTick, loopLength);
    if (slotIdx < 0) {
        slotIdx = 0;
    }

    const int count = static_cast<int>(slots.size());
    slotIdx = (slotIdx + delta) % count;
    if (slotIdx < 0) {
        slotIdx += count;
    }

    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(slotIdx)];
    const uint32_t absoluteBracket = slot.relativeTick;
    const auto notes = selectableDisplayNotesForEditUi(track);
    const int noteIdx = SelectNavigation::resolveNoteIdxAtSlot(slot);
    if (noteIdx >= 0 && noteIdx < static_cast<int>(notes.size())) {
        commitAllPendingNoteEditActions(track);
        rebuildNoteEditFocusForDisplayNote(track, notes[static_cast<size_t>(noteIdx)]);
        const NoteId noteId = noteIdFromFilteredDisplayNote(notes, noteIdx);
        applySelectNav(track, absoluteBracket, noteId);
    } else {
        commitAllPendingNoteEditActions(track);
        rebuildNoteEditFocusAtSelect(track, -1);
        applySelectNav(track, absoluteBracket, kInvalidNoteId);
    }
    hasMovedBracket = true;
}

EDIT_MANAGER_IMPL_MEM void EditManager::switchToNextState(Track& track) {
    // Example: cycle between noteState and startNoteState
    if (currentState == &noteHomeState) {
        setState(&startNoteState, track, selectedTick);
    } else {
        setState(&noteHomeState, track, selectedTick);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::enterEditMode(EditNoteState* newState, uint32_t startTick) {
    auto& track = trackManager.getSelectedTrack();
    controlSurfaceManager.resetLengthEditingModeOnSessionBoundary();
    if (!editSession.active) {
        openNoteEditSession(track);
        emitSessionOpenedToSurface(false, true);
    }
    setState(newState, track, startTick);
}

EDIT_MANAGER_IMPL_MEM void EditManager::exitEditMode(Track& track) {
    controlSurfaceManager.resetLengthEditingModeOnSessionBoundary();
    syncNoteEditFocusLastFromSessionStore(track);
    commitAllPendingNoteEditActions(track);

    closeNoteEditPass(track);
    if (trackManager.getSelectedLoop(track).isEditStateDirty()) {
        StorageManager::requestUrgentEditSave();
        // Queue urgent deferred save request for NOTE_EDIT exit boundary.
        StorageManager::processEditAutosave(looperState.getLooperState());
    }
    track.invalidateCaches();

    selectedNoteIdx = -1;
    hasMovedBracket = false;
    if (currentState) currentState->onExit(*this, track);
    currentState = nullptr;
    resetNoteEditSessionState();
    closeNoteEditSession(track);
    sendEditSessionChange(EditSessionType::Loop);
}

EDIT_MANAGER_IMPL_MEM void EditManager::moveBracket(int delta, const Track& track, uint32_t ticksPerStep) {
    Track& mutableTrack = const_cast<Track&>(track);
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }
    const auto notes = selectableDisplayNotesForEditUi(track);
    const uint32_t loopStartTick = noteEditLoopStartTick(track);

    const uint32_t SNAP_WINDOW = 24;
    if (delta > 0) {
        const uint32_t targetTick = (selectedTick + ticksPerStep) % loopLength;
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            const uint32_t noteTick = displayStartTickFromStorageNote(
                notes[static_cast<size_t>(i)].startTick, loopStartTick, loopLength);
            const uint32_t dist =
                std::min((noteTick + loopLength - targetTick) % loopLength,
                         (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            const DisplayNote& dn = notes[static_cast<size_t>(snapIdx)];
            applySelectNav(mutableTrack,
                           displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength),
                           dn.noteId);
        } else {
            applySelectNav(mutableTrack, targetTick, kInvalidNoteId);
        }
    } else if (delta < 0) {
        const uint32_t targetTick =
            (selectedTick + loopLength - (ticksPerStep % loopLength)) % loopLength;
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            const uint32_t noteTick = displayStartTickFromStorageNote(
                notes[static_cast<size_t>(i)].startTick, loopStartTick, loopLength);
            const uint32_t dist =
                std::min((noteTick + loopLength - targetTick) % loopLength,
                         (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            const DisplayNote& dn = notes[static_cast<size_t>(snapIdx)];
            applySelectNav(mutableTrack,
                           displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength),
                           dn.noteId);
        } else {
            applySelectNav(mutableTrack, targetTick, kInvalidNoteId);
        }
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::selectNextNote(const Track& track) {
    moveBracket(1, track, 1);
}

EDIT_MANAGER_IMPL_MEM void EditManager::selectPrevNote(const Track& track) {
    moveBracket(-1, track, 1);
}

EDIT_MANAGER_IMPL_MEM void EditManager::enterPitchEditMode(Track& track) {
    setState(&pitchNoteState, track, selectedTick);
}

EDIT_MANAGER_IMPL_MEM void EditManager::exitPitchEditMode(Track& track) {
    exitEditMode(track);
}

EDIT_MANAGER_IMPL_MEM void EditManager::cycleNoteEditType(Track& track) {
    if (!editSession.active) {
        openNoteEditSession(track);
        emitSessionOpenedToSurface(false, true);
    }
    applyCycleEditKind(track);
    logger.log(CAT_TRACK, LOG_DEBUG, "Note edit type cycled to kind=%d",
               static_cast<int>(sessionState.kind));
}

EDIT_MANAGER_IMPL_MEM void EditManager::sendEditModeProgram(EditModeState mode) {
    // Note edit kind (select/move/length/pitch) does not use ch16 program change.
    // Session type (loop vs note) is sendEditSessionChange only: PC 0 = loop, PC 1 = note.
    logger.log(CAT_MIDI, LOG_DEBUG, "Note edit kind=%d (no program change)", mode);
}
