//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <algorithm>
#include <utility>

#include "ClockManager.h"
#include "ControlSurfaceManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionUndo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
#include "Utils/MemoryMonitor.h"

#ifndef NOTE_EDIT_OPEN_BISECT_STAGE
#define NOTE_EDIT_OPEN_BISECT_STAGE 4
#endif

EDIT_MANAGER_IMPL_MEM void EditManager::openNoteEditSession(Track& track) {
    if (editSession.active) {
        return;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    editSession.sessionType = EditSessionType::Note;
    editSession.active = true;
    editSession.editPassIndex = 0;
    editSession.editPassIds.clear();
    editSession.durableCheckpointedEditPassIds.clear();
    editSession.durableCheckpointCreated = false;
    editSession.applyOwnedEditPassRows.clear();
    editSession.replaceEditPassOnClose = false;
    editSession.undoStack.clear();
    DIAG_EVENT(Diagnostics::Edit::NoteEditOpenEnter);
    loop.rematerializeEditView(editSession.store.mutStore());
    DIAG_COUNTER_INC(Materialize);
    DIAG_EVENT(Diagnostics::Edit::AfterRematerializeEditView);
#if NOTE_EDIT_OPEN_BISECT_STAGE <= 0
    return;
#endif
    loop.assignMissingNoteIdsInStore(editSession.store.mutStore());
    loop.assignMissingNoteIds(loop.midiEvents());
    loop.assignMissingNoteIds(editSession.store.mutEvents());
    stampNoteIdsOntoPairedNoteOffs(editSession.store.mutEvents());
    DIAG_EVENT(Diagnostics::Edit::AfterAssignNoteIds);
#if NOTE_EDIT_OPEN_BISECT_STAGE <= 1
    return;
#endif
    editSession.store.discardEventsCache();
    resetNoteEditSessionState();
    DIAG_EVENT(Diagnostics::Edit::AfterDiscardFlatCache);
#if NOTE_EDIT_OPEN_BISECT_STAGE <= 2
    return;
#endif
    enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
    DIAG_EVENT(Diagnostics::Edit::AfterEnterDefaultState);
#if NOTE_EDIT_OPEN_BISECT_STAGE <= 3
    return;
#endif
    bumpSessionPreviewRevision();
    bumpSessionPlaybackPreviewRevision();
    DIAG_EVENT(Diagnostics::Edit::NoteEditOpenExit);
    logger.debug("EditSession opened editPass=0");
}

EDIT_MANAGER_IMPL_MEM void EditManager::reopenNoteEditSession(Track& track) {
    if (editSession.active) {
        editSession.store.mutStore().clear();
        editSession.store.discardEventsCache();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.applyOwnedEditPassRows.clear();
        editSession.active = false;
        resetNoteEditSessionState();
        selectedNoteIdx = -1;
        hasMovedBracket = false;
    }
    editSession.sessionType = EditSessionType::Note;
    deferSelectionSurfaceEvents_ = true;
    openNoteEditSession(track);
    deferSelectionSurfaceEvents_ = false;
    DIAG_COUNTER_INC(CacheInvalidateBroad);
    track.invalidateCaches();
}

EDIT_MANAGER_IMPL_MEM void EditManager::closeNoteEditPass(Track& track) {
    if (!editSession.active) {
        return;
    }
    if (editSession.replaceEditPassOnClose) {
        bakeNoteEditSessionStoreToPasses(track);
        editSession.replaceEditPassOnClose = false;
    }
    const EditPassIdList idsToPush =
        collectEditPassIdsPendingDurableCheckpoint(editSession.editPassIds,
                                                   editSession.durableCheckpointedEditPassIds);
    if (!idsToPush.empty()) {
        TrackUndo::pushNoteEditPassClosed(track, editSession.editPassIndex, idsToPush);
        logger.log(CAT_TRACK, LOG_INFO, "NoteEditPassClosed editPass=%u edits=%u",
                   static_cast<unsigned>(editSession.editPassIndex),
                   static_cast<unsigned>(idsToPush.size()));
    }
    editSession.editPassIds.clear();
    editSession.durableCheckpointedEditPassIds.clear();
    editSession.durableCheckpointCreated = false;
    editSession.undoStack.clear();
    ++editSession.editPassIndex;
}

EDIT_MANAGER_IMPL_MEM void EditManager::markCurrentEditBatchDurable(Track& track) {
    if (!editSession.active || editSession.editPassIds.empty()) {
        return;
    }
    const EditPassIdList idsToPush =
        collectEditPassIdsPendingDurableCheckpoint(editSession.editPassIds,
                                                   editSession.durableCheckpointedEditPassIds);
    if (idsToPush.empty()) {
        editSession.durableCheckpointCreated = true;
        return;
    }
    TrackUndo::pushNoteEditPassClosed(track, editSession.editPassIndex, idsToPush);
    for (const EditPassId id : idsToPush) {
        editSession.durableCheckpointedEditPassIds.push_back(id);
    }
    editSession.durableCheckpointCreated = true;
    logger.log(CAT_TRACK, LOG_INFO,
               "NoteEditPassClosed durable checkpoint editPass=%u edits=%u (session active)",
               static_cast<unsigned>(editSession.editPassIndex),
               static_cast<unsigned>(idsToPush.size()));
}

EDIT_MANAGER_IMPL_MEM void EditManager::closeNoteEditSession(Track& track) {
    if (!editSession.active) {
        return;
    }
    flushDeferredNoteEditDisplayRefresh(track);
    closeNoteEditPass(track);
    editSession.store.mutStore().clear();
    editSession.store.discardEventsCache();
    editSession.focus.clear();
    editSession.applyOwnedEditPassRows.clear();
    editSession.active = false;
    editSession.sessionType = EditSessionType::Loop;
    editSession.editPassIndex = 0;
    editSession.replaceEditPassOnClose = false;
    clearLastFader1SelectNoteId();
    (void)track;
}

EDIT_MANAGER_IMPL_MEM void EditManager::revertNoteEditSessionForLoopClear(Track& track) {
    if (editSession.sessionType != EditSessionType::Note && !editSession.active) {
        return;
    }

    controlSurfaceManager.resetLengthEditingModeOnSessionBoundary();
    editSession.replaceEditPassOnClose = false;
    editSession.editPassIds.clear();
    editSession.durableCheckpointedEditPassIds.clear();
    editSession.durableCheckpointCreated = false;
    editSession.undoStack.clear();
    editSession.applyOwnedEditPassRows.clear();
    editSession.store.mutStore().clear();
    editSession.store.discardEventsCache();
    editSession.focus.clear();
    editSession.active = false;
    editSession.editPassIndex = 0;

    selectedNoteIdx = -1;
    hasMovedBracket = false;
    clearLastFader1SelectNoteId();
    resetNoteEditSessionState();
    currentEditMode = EDIT_MODE_NONE;

    if (currentState) {
        currentState->onExit(*this, track);
        currentState = nullptr;
    }

    editSession.sessionType = EditSessionType::Loop;
    sendEditSessionChange(EditSessionType::Loop);
    track.invalidateCaches();
    logger.log(CAT_TRACK, LOG_INFO,
               "Note edit session discarded after loop clear; reverted to loop edit");
}

EDIT_MANAGER_IMPL_MEM void EditManager::rematerializeNoteEditSessionAfterWorkspaceReload(Track& track) {
    if (editSession.sessionType == EditSessionType::Note) {
        reopenNoteEditSession(track);
        return;
    }
    if (editSession.active) {
        editSession.store.mutStore().clear();
        editSession.store.discardEventsCache();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.durableCheckpointedEditPassIds.clear();
        editSession.durableCheckpointCreated = false;
        editSession.active = false;
        resetNoteEditSessionState();
        selectedNoteIdx = -1;
        hasMovedBracket = false;
    }
    // Loaded workspace data is authoritative; live display cache already invalidated by caller.
}

EDIT_MANAGER_IMPL_MEM void EditManager::foldLiveCaptureIntoNoteEditSession(Track& track) {
    if (!editSession.active) {
        return;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loop.capture.store.empty()) {
        loop.discardCapture();
        loop.discardPendingCapturePass();
        return;
    }

    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }
    const MidiEventVec baselineStoreEvents = editSession.store.readEvents();

    MidiEventVec captureFlat;
    loop.capture.store.copyEventsTo(captureFlat);
    loop.assignMissingNoteIds(captureFlat);
    MidiEventVec& sessionFlat = editSession.store.mutEvents();
    if (sessionFlat.empty()) {
        sessionFlat = std::move(captureFlat);
    } else if (!captureFlat.empty()) {
        MidiEventVec merged;
        merged.reserve(sessionFlat.size() + captureFlat.size());
        std::merge(sessionFlat.begin(), sessionFlat.end(), captureFlat.begin(), captureFlat.end(),
                   std::back_inserter(merged),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
        sessionFlat = std::move(merged);
    }
    editSession.store.syncEventsToStore();
    loop.discardCapture();
    loop.discardPendingCapturePass();

    const EditPassVec redoRows = buildSessionStoreEditPasses(
        baselineStoreEvents, editSession.store.readEvents(), track.getMidiChannel(), loopLength);
    if (redoRows.empty()) {
        return;
    }

    SessionUndoEntry entry;
    entry.selection = sessionState.selection;
    entry.focus = snapshotFocusForSessionUndo(editSession.focus);
    entry.editPassIdsAtPush = editSession.editPassIds;
    entry.redoEditRows = redoRows;
    entry.redoFocus = snapshotFocusForSessionUndo(editSession.focus);
    entry.redoSelection = sessionState.selection;
    entry.redoEditPassIds = editSession.editPassIds;
    entry.hasRedoPayload = true;

    if (!editSession.undoStack.pushEntry(std::move(entry))) {
        editSession.store.discardEventsCache();
        trackManager.reclaimUnreferencedDisabledPasses();
        if (!editSession.undoStack.pushEntry(std::move(entry))) {
            logger.log(CAT_TRACK, LOG_WARNING,
                       "Session live capture undo push rejected: heap below reserve (need=%u free=%u)",
                       static_cast<unsigned>(Config::HEAP_RESERVE_BYTES +
                                             estimatedSessionUndoEntryBytes(entry)),
                       static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
            return;
        }
    }
    editSession.replaceEditPassOnClose = true;
    track.invalidateCaches();
    logger.log(CAT_TRACK, LOG_INFO, "EditSession live capture folded rows=%u",
               static_cast<unsigned>(redoRows.size()));
}

EDIT_MANAGER_IMPL_MEM void EditManager::persistActiveNoteEditSession(Track& track) {
    if (!editSession.active) {
        return;
    }
    syncNoteEditFocusLastFromSessionStore(track);
    commitAllPendingNoteEditActions(track);
    bakeNoteEditSessionStoreToPasses(track);
    markCurrentEditBatchDurable(track);
}
