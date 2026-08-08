//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <utility>

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

#include "ControlSurfaceManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "NoteEditSessionUndo.h"
#include "TrackManager.h"
#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

using DisplayNote = NoteUtils::DisplayNote;

EDIT_MANAGER_IMPL_MEM bool EditManager::pushSessionUndoOnKindChange(Track& track, NoteEditKind kind) {
    if (kind == NoteEditKind::Pitch && isLengthEditingMode()) {
        return true;
    }
    if (!shouldPushGeometryKindUndo(lastPushedGeometryKind_, kind)) {
        return true;
    }
    if (!editSession.active) {
        return true;
    }
    SessionUndoEntry entry;
    if (kindBoundaryUndoCacheValid_ &&
        kindBoundaryUndoCacheRevision_ == sessionPreviewRevision_) {
        kindBoundaryUndoCacheValid_ = false;
#if defined(SESSION_CAPTURE)
        logger.info("#CAP,%lu,UNDO_WARM,push,cache_hit,0,%zu,%u",
                    static_cast<unsigned long>(micros()),
                    editSession.focus.baselineMap.size(), static_cast<unsigned>(kind));
#endif
        entry = std::move(kindBoundaryUndoCache_);
    } else {
#if defined(SESSION_CAPTURE)
        const uint32_t readStartUs = micros();
#endif
        const auto& sessionFlat = editSession.store.readEvents();
#if defined(SESSION_CAPTURE)
        logger.info("#CAP,%lu,UNDO_WARM,phase,read_events,%lu,%zu,%zu,0",
                    static_cast<unsigned long>(micros()),
                    static_cast<unsigned long>(micros() - readStartUs),
                    editSession.focus.baselineMap.size(), sessionFlat.size());
        const uint32_t buildStartUs = micros();
#endif
        entry = buildSessionUndoEntry(editSession.focus, sessionState.selection, sessionFlat,
                                      track.getMidiChannel(), noteEditLoopLengthTicks(track),
                                      editSession.editPassIds,
                                      &editSession.noteEditCurrentState);
#if defined(SESSION_CAPTURE)
        logger.info("#CAP,%lu,UNDO_WARM,push,cache_miss,%lu,%zu,%u",
                    static_cast<unsigned long>(micros()),
                    static_cast<unsigned long>(micros() - buildStartUs),
                    editSession.focus.baselineMap.size(), static_cast<unsigned>(kind));
#endif
    }
    if (!editSession.undoStack.pushEntry(std::move(entry))) {
        editSession.store.discardEventsCache();
        trackManager.reclaimUnreferencedDisabledPasses();
        if (!editSession.undoStack.pushEntry(std::move(entry))) {
            logger.log(CAT_TRACK, LOG_WARNING,
                       "Session undo push rejected: heap below reserve (need=%u free=%u)",
                       static_cast<unsigned>(Config::HEAP_RESERVE_BYTES +
                                             estimatedSessionUndoEntryBytes(entry)),
                       static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
            DIAG_COUNTER_INC(AllocatorFailure);
            return false;
        }
    }
    lastPushedGeometryKind_ = kind;
    kindBoundaryUndoCacheValid_ = false;
    (void)track;
    return true;
}

EDIT_MANAGER_IMPL_MEM void EditManager::scheduleKindBoundaryUndoWarm() {
    kindBoundaryUndoWarmPending_ = true;
    kindBoundaryUndoCacheValid_ = false;
}

EDIT_MANAGER_IMPL_MEM void EditManager::processKindBoundaryUndoWarm(Track& track) {
    if (!kindBoundaryUndoWarmPending_) {
        return;
    }
    kindBoundaryUndoWarmPending_ = false;
    if (!editSession.active || lastPushedGeometryKind_ != NoteEditKind::Select) {
        return;
    }
    if (!editorSelectionHasNote(sessionState.selection)) {
        return;
    }
#if defined(SESSION_CAPTURE)
    const uint32_t warmStartUs = micros();
    const uint32_t readStartUs = micros();
#endif
    const auto& sessionFlat = editSession.store.readEvents();
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,UNDO_WARM,phase,read_events,%lu,%zu,%zu,0",
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - readStartUs),
                editSession.focus.baselineMap.size(), sessionFlat.size());
#endif
    kindBoundaryUndoCache_ =
        buildSessionUndoEntry(editSession.focus, sessionState.selection, sessionFlat,
                              track.getMidiChannel(), noteEditLoopLengthTicks(track),
                              editSession.editPassIds, &editSession.noteEditCurrentState);
    kindBoundaryUndoCacheValid_ = true;
    kindBoundaryUndoCacheRevision_ = sessionPreviewRevision_;
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,UNDO_WARM,warm,complete,%lu,%zu,%zu,%u",
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - warmStartUs),
                editSession.focus.baselineMap.size(), sessionFlat.size(),
                static_cast<unsigned>(kindBoundaryUndoCache_.editRows.size()));
#endif
}

EDIT_MANAGER_IMPL_MEM void EditManager::restoreSessionUndoEntry(Track& track,
                                                                const SessionUndoEntry& entry) {
    const uint8_t channel = track.getMidiChannel();
    if (entry.hasUndoCurrentState) {
        editSession.noteEditCurrentState.assignFrom(entry.undoCurrentState);
        refreshNoteEditSessionProjection(channel);
    } else {
        Loop& loop = trackManager.getSelectedLoop(track);
        applySessionUndoEntry(loop, editSession.store, entry, noteEditLoopLengthTicks(track),
                              channel, editSession.editPassIds);
    }
    editSession.focus = entry.focus;
    sessionState.selection = entry.selection;
    syncNoteEditSessionStateToUi(track);
    syncSelectedNoteIdxToFilteredInventory(track);
}

EDIT_MANAGER_IMPL_MEM void EditManager::applyUndoRedoLanding(Track& track) {
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;

    uint32_t bracket = getSelectedTick();
    NoteId primaryNote = kInvalidNoteId;

    const NoteEditFocus& focus = editSession.focus;
    const auto notes = selectableDisplayNotesAtEditSelect(track);
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        const uint32_t loopLength = noteEditLoopLengthTicks(track);
        const uint32_t loopStartTick = noteEditLoopStartTick(track);
        const uint32_t bracketDisplay =
            displayStartTickFromStorageNote(focus.last.startTick, loopStartTick, loopLength);
        const int displayIdx = filteredDisplayNoteIndexForNoteIdAndStart(
            notes, focus.movingNoteId, bracketDisplay, loopStartTick, loopLength);
        if (displayIdx >= 0 && displayIdx < static_cast<int>(notes.size())) {
            const DisplayNote& dn = notes[static_cast<size_t>(displayIdx)];
            primaryNote = dn.noteId;
            bracket = displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength);
        }
    } else if (!notes.empty()) {
        selectClosestNote(track, getSelectedTick());
        if (selectedNoteIdx >= 0) {
            const DisplayNote& dn = notes[static_cast<size_t>(selectedNoteIdx)];
            primaryNote = dn.noteId;
            bracket = displayStartTickFromStorageNote(dn.startTick, noteEditLoopStartTick(track),
                                                      noteEditLoopLengthTicks(track));
        }
    }

    controlSurfaceManager.resetLengthEditingModeOnNoteSelect();
    applySelectNav(track, bracket, primaryNote, primaryNote != kInvalidNoteId);
}

EDIT_MANAGER_IMPL_MEM bool EditManager::sessionUndo(Track& track) {
    if (!editSession.active || !editSession.undoStack.canUndo()) {
        return false;
    }
    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }
    SessionUndoEntry redoPayload =
        buildSessionUndoEntry(editSession.focus, sessionState.selection,
                              editSession.store.readEvents(), track.getMidiChannel(),
                              noteEditLoopLengthTicks(track), editSession.editPassIds,
                              &editSession.noteEditCurrentState);
    SessionUndoEntry* entry = editSession.undoStack.popUndoTarget();
    if (entry == nullptr) {
        return false;
    }
    const bool liveCaptureEntry =
        entry->hasRedoPayload && entry->editRows.empty() && !entry->redoEditRows.empty();
    if (liveCaptureEntry) {
        if (!entry->hasRedoCurrentState && !editSession.noteEditCurrentState.empty()) {
            entry->redoCurrentState = editSession.noteEditCurrentState.clone();
            entry->hasRedoCurrentState = true;
        }
        entry->redoFocus = editSession.focus;
        entry->redoSelection = sessionState.selection;
        entry->redoEditPassIds = editSession.editPassIds;
    } else {
        entry->redoEditPassIds = editSession.editPassIds;
        if (!editSession.noteEditCurrentState.empty()) {
            entry->redoCurrentState = editSession.noteEditCurrentState.clone();
            entry->hasRedoCurrentState = true;
        }
        entry->redoEditRows = std::move(redoPayload.editRows);
        entry->redoFocus = std::move(redoPayload.focus);
        entry->redoSelection = redoPayload.selection;
        entry->hasRedoPayload = true;
    }
    editSession.replaceEditPassOnClose = true;

    Loop& loop = trackManager.getSelectedLoop(track);
    restoreSessionUndoEntry(track, *entry);
    EditPassIdList passesToDisable;
    for (const EditPassId id : editSession.editPassIds) {
        bool keptAtPush = false;
        for (const EditPassId pushId : entry->editPassIdsAtPush) {
            if (pushId == id) {
                keptAtPush = true;
                break;
            }
        }
        if (!keptAtPush) {
            passesToDisable.push_back(id);
        }
    }
    if (!passesToDisable.empty()) {
        loop.disableEditPasses(passesToDisable);
        editSession.editPassIds = entry->editPassIdsAtPush;
    }

    track.invalidateCaches();
    applyUndoRedoLanding(track);
    return true;
}

EDIT_MANAGER_IMPL_MEM bool EditManager::sessionRedo(Track& track) {
    if (!editSession.active || !editSession.undoStack.canRedo()) {
        return false;
    }
    SessionUndoEntry* entry = editSession.undoStack.peekRedoTarget();
    if (entry == nullptr) {
        return false;
    }
    if (!entry->hasRedoPayload) {
        return false;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    loop.enableEditPasses(entry->redoEditPassIds);
    editSession.editPassIds = entry->redoEditPassIds;
    const uint8_t channel = track.getMidiChannel();
    if (entry->hasRedoCurrentState) {
        editSession.noteEditCurrentState.assignFrom(entry->redoCurrentState);
        refreshNoteEditSessionProjection(channel);
    } else {
        applySessionRedoEntry(loop, editSession.store, *entry, noteEditLoopLengthTicks(track),
                              channel, editSession.editPassIds);
    }
    editSession.focus = entry->redoFocus;
    sessionState.selection = entry->redoSelection;
    editSession.undoStack.advanceRedoCursor();
    editSession.replaceEditPassOnClose = true;
    track.invalidateCaches();
    applyUndoRedoLanding(track);
    return true;
}
