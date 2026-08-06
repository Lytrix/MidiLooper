//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManager.h"
#include "EditManagerInternal.h"
#include "EditNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Loop.h"
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"
#include "MidiEvent.h"
#include "TrackUndo.h"
#include "StorageManager.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "MidiConfig.h"
#include "ControlSurfaceManager.h"
#include "LoopEditManager.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionUndo.h"
#include "EditApply.h"
#include "EditSession.h"
#include "NoteEditSessionState.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/LoopEventValidation.h"
#include "ClockManager.h"
#include "DisplayManager.h"
#include "Utils/NoteMovementUtils.h"
#include "ApplyOwnedEditPassRows.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SelectNavigation.h"
#include "TickPhase.h"
#include "Utils/ValidationUtils.h"
#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
#include "Utils/NoteEditMem.h"
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include <unordered_set>
#include <cmath>

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

using DisplayNote = NoteUtils::DisplayNote;

#include "NoteEditFocus.h"
#include "EditPass.h"
#include "LoopPasses.h"

#if defined(__IMXRT1062__)
DMAMEM EditManager editManager;
#else
EditManager editManager;
#endif

EDIT_MANAGER_IMPL_MEM bool EditManager::isLengthBracketEditActive() const {
    return sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
}

EDIT_MANAGER_IMPL_MEM uint32_t EditManager::noteEditLoopLengthTicks(const Track& track) const {
    if (isNoteEditActive()) {
        return trackManager.getSelectedLoop(track).loopLengthTicks;
    }
    return track.getLoopLength();
}

EDIT_MANAGER_IMPL_MEM uint32_t EditManager::noteEditLoopStartTick(const Track& track) const {
    if (isNoteEditActive()) {
        const Loop& loop = trackManager.getSelectedLoop(track);
        const uint32_t loopLength = loop.loopLengthTicks;
        return loopLength > 0 ? loop.loopStartTick % loopLength : 0;
    }
    const uint32_t loopLength = track.getLoopLength();
    return loopLength > 0 ? track.getLoopStartTick() % loopLength : 0;
}

EDIT_MANAGER_IMPL_MEM bool EditManager::isSessionUndoDisplayActive() const {
    return editSession.active && editSession.sessionType == EditSessionType::Note;
}

EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::sessionMidiEvents() {
    return editSession.store.mutEvents();
}

EDIT_MANAGER_IMPL_MEM const MidiEventVec& EditManager::sessionMidiEvents() const {
    return editSession.store.readEvents();
}

EDIT_MANAGER_IMPL_MEM void EditManager::bumpSessionPreviewRevision() {
    ++sessionPreviewRevision_;
    invalidateProjectedNoteEditDisplayCache();
}

EDIT_MANAGER_IMPL_MEM void EditManager::bumpSessionPlaybackPreviewRevision() {
    ++sessionPlaybackPreviewRevision_;
}

EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::editMidiEvents(Track& track) {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.legacyMidiEventsFromCommitted();
}

EDIT_MANAGER_IMPL_MEM const MidiEventVec& EditManager::editMidiEvents(const Track& track) const {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.legacyMidiEventsFromCommitted();
}

EDIT_MANAGER_IMPL_MEM EditManager::EditManager() {
    currentState = nullptr; // Start with no state
}

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

// EditModeManager functionality
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

EDIT_MANAGER_IMPL_MEM void EditManager::setSelectedNoteIdx(int idx) {
    if (selectedNoteIdx != idx) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection changed: %d -> %d", selectedNoteIdx, idx);
    }
    selectedNoteIdx = idx;
}

EDIT_MANAGER_IMPL_MEM void EditManager::setLastFader1SelectNoteId(NoteId noteId) {
    lastFader1SelectNoteId = noteId;
}

EDIT_MANAGER_IMPL_MEM void EditManager::clearLastFader1SelectNoteId() {
    lastFader1SelectNoteId = kInvalidNoteId;
}

EDIT_MANAGER_IMPL_MEM void EditManager::resetSelection() {
    if (selectedNoteIdx != -1) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection reset: %d -> -1", selectedNoteIdx);
    }
    selectedNoteIdx = -1;
    clearLastFader1SelectNoteId();
}

EDIT_MANAGER_IMPL_MEM void EditManager::emitEditEvent(EditEvent event) {
    if (editEventListener_ != nullptr) {
        editEventListener_->onEditEvent(event);
    }
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

