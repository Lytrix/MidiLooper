//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include "ControlSurfaceManager.h"
#include "EditEvent.h"
#include "EditManager.h"
#include "EditSession.h"
#include "Globals.h"
#include "Logger.h"
#include "Loop.h"
#include "LoopEditManager.h"
#include "MidiConfig.h"
#include "NoteEditSessionState.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/NoteEditDisplaySnapshot.h"

EDIT_MANAGER_IMPL_MEM bool EditManager::isSessionUndoDisplayActive() const {
    return editSession.active && editSession.sessionType == EditSessionType::Note;
}

EDIT_MANAGER_IMPL_MEM void EditManager::cycleEditSession(Track& track) {
    if (editSession.sessionType == EditSessionType::Note) {
        controlSurfaceManager.resetLengthEditingModeOnSessionBoundary();
        syncNoteEditFocusLastFromSessionStore(track);
        trackManager.reclaimUnreferencedDisabledPasses();
        commitAllPendingNoteEditActions(track);
        editSession.replaceEditPassOnClose = true;
        if (currentState) {
            currentState->onExit(*this, track);
            currentState = nullptr;
        }
        currentEditMode = EDIT_MODE_NONE;
        selectedNoteIdx = -1;
        hasMovedBracket = false;
        resetNoteEditSessionState();
        closeNoteEditSession(track);
        track.invalidateCaches();
    } else {
        sendEditSessionChange(EditSessionType::Note);
        logger.log(CAT_TRACK, LOG_DEBUG, "Edit session cycled to: %d",
                   static_cast<int>(EditSessionType::Note));
        return;
    }
    sendEditSessionChange(EditSessionType::Loop, true);
    logger.log(CAT_TRACK, LOG_DEBUG, "Edit session cycled to: %d",
               static_cast<int>(editSession.sessionType));
}

EDIT_MANAGER_IMPL_MEM void EditManager::emitSessionOpenedToSurface(bool includeMidi, bool includeNoteFaderFeedback) {
    sessionOpenedIncludesMidi_ = includeMidi;
    sessionOpenedIncludesFaderFeedback_ = includeNoteFaderFeedback;
    emitEditEvent(EditEvent::SessionOpened);
}

EDIT_MANAGER_IMPL_MEM void EditManager::sendEditSessionChange(EditSessionType sessionType, bool notifySurfaceMidi) {
    const EditSessionType priorSession = editSession.sessionType;
    if (sessionType == EditSessionType::ControlChange) {
        return;
    }
    editSession.sessionType = sessionType;

    bool reopenedNoteSession = false;
    if (priorSession == EditSessionType::Loop && sessionType != EditSessionType::Loop) {
        loopEditManager.commitLoopEditOnDepart(trackManager.getSelectedTrack());
    }

    if (sessionType == EditSessionType::Note) {
        loopEditManager.onLeaveLoopEditSession();
        if (priorSession != EditSessionType::Note || !editSession.active) {
            Track& track = trackManager.getSelectedTrack();
            reopenNoteEditSession(track);
            reopenedNoteSession = true;
        }
    }
    if (sessionType == EditSessionType::Loop) {
        loopEditManager.onEnterLoopEditSession(trackManager.getSelectedTrack());
    }

    const bool sessionTypeChanged = priorSession != sessionType;
    if (sessionTypeChanged &&
        (priorSession == EditSessionType::Note || priorSession == EditSessionType::Loop)) {
        emitEditEvent(EditEvent::SessionClosed);
    }
    if (reopenedNoteSession && sessionTypeChanged) {
        emitSessionOpenedToSurface(true, true);
    } else if (sessionTypeChanged || notifySurfaceMidi) {
        emitSessionOpenedToSurface(sessionTypeChanged || notifySurfaceMidi, false);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::commitEditSessionOnDepart(Track& track) {
    flushDeferredNoteEditDisplayRefresh(track);
    if (isLoopEditSession()) {
        loopEditManager.commitLoopEditOnDepart(track);
    }
    if (editSession.active) {
        persistActiveNoteEditSession(track);
    }
    if (currentState) {
        currentState->onExit(*this, track);
        currentState = nullptr;
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::reenterEditSessionForFocusChange(Track& track, uint8_t /*previousSlot*/) {
    switch (editSession.sessionType) {
        case EditSessionType::Loop:
            loopEditManager.reopenLoopEditSession(track);
            logger.log(CAT_TRACK, LOG_DEBUG, "LOOP_EDIT session refreshed for focus change");
            break;
        case EditSessionType::Note:
            if (editSession.active) {
                reopenNoteEditSession(track);
                emitSessionOpenedToSurface(false, true);
                logger.log(CAT_TRACK, LOG_DEBUG, "NOTE_EDIT session reopened for focus change");
            }
            break;
        case EditSessionType::ControlChange:
            logger.log(CAT_TRACK, LOG_DEBUG, "ControlChange edit focus change (stub)");
            break;
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::beforeSelectedTrackChange(Track& departingTrack) {
    commitEditSessionOnDepart(departingTrack);
}

EDIT_MANAGER_IMPL_MEM void EditManager::onTrackChanged(Track& newTrack) {
    currentEditMode = EDIT_MODE_NONE;
    selectedNoteIdx = -1;
    hasMovedBracket = false;

    reenterEditSessionForFocusChange(newTrack, 255);

    logger.log(CAT_TRACK, LOG_DEBUG, "Edit state reset for new track");
}

EDIT_MANAGER_IMPL_MEM void EditManager::beforeSelectedSlotChange(Track& track) {
    commitEditSessionOnDepart(track);
}

EDIT_MANAGER_IMPL_MEM void EditManager::onSelectedSlotChanged(Track& track, uint8_t previousSlot) {
    reenterEditSessionForFocusChange(track, previousSlot);
}

EDIT_MANAGER_IMPL_MEM size_t EditManager::getDisplayUndoCount(const Track& track, const Loop& loop) const {
    if (isSessionUndoDisplayActive()) {
        return editSession.undoStack.undoCount();
    }
    return TrackUndo::undoDepthForLoop(track, loop);
}

EDIT_MANAGER_IMPL_MEM void EditManager::clearLengthEditingMode(bool emitEvent) {
    if (!lengthEditingMode_) {
        return;
    }
    lengthEditingMode_ = false;
    if (emitEvent) {
        emitEditEvent(EditEvent::LengthModeChanged);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::clearLengthEditingModeOnNoteSelect() {
    clearLengthEditingMode(false);
}

EDIT_MANAGER_IMPL_MEM void EditManager::toggleLengthEditMode(Track& track) {
    const bool enabling = !lengthEditingMode_;
    lengthEditingMode_ = enabling;

    if (lengthEditingMode_) {
        logger.info("[MIDI] Length editing mode ENABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE END position (length editing)");
        syncNoteEditFocusLastFromSessionStore(track);
        const uint32_t loopLength = track.getLoopLength();
        if (getSelectedNoteIdx() >= 0 && loopLength > 0) {
            const NoteUtils::DisplayNote liveNote = liveEditDisplayNoteAtSelect(track);
            const uint32_t loopStartTick = noteEditLoopStartTick(track);
            const uint32_t relEnd = liveNote.endTick % loopLength;
            setSelectedTick(relEnd);
            lengthFineAnchorEndTick_ = relEnd;
            setReferenceStep(relEnd / Config::TICKS_PER_16TH_STEP);
            beginGeometryMutation(track, NoteEditKind::Length, false);
            if (editorSelectionHasNote(sessionState.selection)) {
                const uint32_t displayEndBracket =
                    NoteEditDisplaySnapshot::displayStartTickFromStorage(liveNote.endTick,
                                                                         loopStartTick, loopLength);
                applySelectionFromGeometryEdit(track, displayEndBracket,
                                               sessionState.selection.primaryNote);
            }
        }
    } else {
        logger.info("[MIDI] Length editing mode DISABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE START position (position editing)");
        commitAllPendingNoteEditActions(track);
        syncNoteEditFocusLastFromSessionStore(track);
        const NoteUtils::DisplayNote liveNote = liveEditDisplayNoteAtSelect(track);
        if (getSelectedNoteIdx() >= 0) {
            // Switch kind before bracket/display sync so isLengthBracketEditActive() is false.
            beginGeometryMutation(track, NoteEditKind::Move, false);
        }
        const uint32_t loopLength = track.getLoopLength();
        if (loopLength > 0) {
            const uint32_t loopStartTick = noteEditLoopStartTick(track);
            const uint32_t relStart = liveNote.startTick % loopLength;
            setSelectedTick(relStart);
            setReferenceStep(relStart / Config::TICKS_PER_16TH_STEP);
            if (editorSelectionHasNote(sessionState.selection)) {
                const uint32_t displayStartBracket =
                    NoteEditDisplaySnapshot::displayStartTickFromStorage(liveNote.startTick,
                                                                           loopStartTick, loopLength);
                applySelectionFromGeometryEdit(track, displayStartBracket,
                                               sessionState.selection.primaryNote);
            }
        }
        if (getSelectedNoteIdx() >= 0) {
            syncGeometrySelectionToUi(track);
        }
    }
    emitEditEvent(EditEvent::LengthModeChanged);
}
