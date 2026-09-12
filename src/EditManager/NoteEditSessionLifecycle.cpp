//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "ClockManager.h"
#include "Track.h"
#include "ControlSurfaceManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "Loop.h"
#include "LoopEditManager.h"
#include "LoopEventStore.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "NoteEditSessionUndo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

#ifndef NOTE_EDIT_OPEN_BISECT_STAGE
#define NOTE_EDIT_OPEN_BISECT_STAGE 4
#endif

#if defined(SESSION_CAPTURE)
#if defined(__IMXRT1062__)
#include <Arduino.h>
#define NOTE_EDIT_OPEN_Q3_FMT(literal) PSTR(literal)
#define NOTE_EDIT_OPEN_Q3_SNPRINTF snprintf_P
#else
#define NOTE_EDIT_OPEN_Q3_FMT(literal) literal
#define NOTE_EDIT_OPEN_Q3_SNPRINTF snprintf
#endif
namespace {

constexpr size_t kNoteEditOpenQ3IdSample = 8;
constexpr size_t kNoteEditOpenQ3LineBytes = 192;

NOTE_EDIT_MEM bool displayNoteIdSeenEarlier(const NoteUtils::DisplayNoteVec& notes, size_t index,
                                            NoteId noteId) {
    for (size_t i = 0; i < index; ++i) {
        if (notes[i].noteId == noteId) {
            return true;
        }
    }
    return false;
}

NOTE_EDIT_MEM bool storeNoteOnIdSeenEarlier(const LoopEventStore& store, size_t index,
                                            NoteId noteId) {
    for (size_t i = 0; i < index; ++i) {
        const MidiEvent& evt = store.at(i);
        if (evt.isNoteOn() && evt.noteId == noteId) {
            return true;
        }
    }
    return false;
}

NOTE_EDIT_MEM bool storeHasNoteOnId(const LoopEventStore& store, NoteId noteId) {
    for (size_t i = 0; i < store.size(); ++i) {
        const MidiEvent& evt = store.at(i);
        if (evt.isNoteOn() && evt.noteId == noteId) {
            return true;
        }
    }
    return false;
}

NOTE_EDIT_MEM bool displayHasNoteId(const NoteUtils::DisplayNoteVec& notes, NoteId noteId) {
    for (const NoteUtils::DisplayNote& note : notes) {
        if (note.noteId == noteId) {
            return true;
        }
    }
    return false;
}

NOTE_EDIT_MEM void appendNoteIdSample(char* line, size_t lineBytes, size_t& used,
                                      const NoteId* ids, size_t idCount) {
    if (line == nullptr || ids == nullptr || used >= lineBytes) {
        return;
    }
    for (size_t i = 0; i < idCount && used + 12 < lineBytes; ++i) {
        const int wrote = NOTE_EDIT_OPEN_Q3_SNPRINTF(line + used, lineBytes - used,
                                                     NOTE_EDIT_OPEN_Q3_FMT(",%lu"),
                                                     static_cast<unsigned long>(ids[i]));
        if (wrote < 0) {
            return;
        }
        used += static_cast<size_t>(wrote);
    }
}

NOTE_EDIT_MEM void logNoteEditOpenQ3(const Loop& loop, const LoopEventStore& store) {
    const NoteUtils::DisplayNoteVec& notes = loop.visualCache.notes;
    NoteId onlyVisualSample[kNoteEditOpenQ3IdSample] = {};
    NoteId onlyStoreSample[kNoteEditOpenQ3IdSample] = {};
    uint32_t visualIds = 0;
    uint32_t storeIds = 0;
    uint32_t shared = 0;
    uint32_t onlyVisual = 0;
    uint32_t onlyStore = 0;
    uint32_t storeInvalid = 0;
    size_t onlyVisualSampleCount = 0;
    size_t onlyStoreSampleCount = 0;

    for (size_t i = 0; i < notes.size(); ++i) {
        const NoteId noteId = notes[i].noteId;
        if (noteId == kInvalidNoteId || displayNoteIdSeenEarlier(notes, i, noteId)) {
            continue;
        }
        ++visualIds;
        if (storeHasNoteOnId(store, noteId)) {
            ++shared;
        } else {
            ++onlyVisual;
            if (onlyVisualSampleCount < kNoteEditOpenQ3IdSample) {
                onlyVisualSample[onlyVisualSampleCount++] = noteId;
            }
        }
    }
    for (size_t i = 0; i < store.size(); ++i) {
        const MidiEvent& evt = store.at(i);
        if (!evt.isNoteOn()) {
            continue;
        }
        if (evt.noteId == kInvalidNoteId) {
            ++storeInvalid;
            continue;
        }
        if (storeNoteOnIdSeenEarlier(store, i, evt.noteId)) {
            continue;
        }
        ++storeIds;
        if (!displayHasNoteId(notes, evt.noteId)) {
            ++onlyStore;
            if (onlyStoreSampleCount < kNoteEditOpenQ3IdSample) {
                onlyStoreSample[onlyStoreSampleCount++] = evt.noteId;
            }
        }
    }

    char summary[kNoteEditOpenQ3LineBytes];
    NOTE_EDIT_OPEN_Q3_SNPRINTF(
        summary, sizeof(summary),
        NOTE_EDIT_OPEN_Q3_FMT(
            "#CAP,NOTE_EDIT_OPEN,q3,current=%u,root_rev=%lu,visual_ids=%u,store_ids=%u,"
            "shared=%u,only_visual=%u,only_store=%u,store_invalid=%u"),
        loop.publishedSnapshotIsCurrent() ? 1u : 0u,
        static_cast<unsigned long>(loop.publishedSnapshotRevision()),
        static_cast<unsigned>(visualIds), static_cast<unsigned>(storeIds),
        static_cast<unsigned>(shared), static_cast<unsigned>(onlyVisual),
        static_cast<unsigned>(onlyStore), static_cast<unsigned>(storeInvalid));
    logger.info("%s", summary);
    if (onlyVisual > 0) {
        char line[kNoteEditOpenQ3LineBytes];
        size_t used = 0;
        const int prefix = NOTE_EDIT_OPEN_Q3_SNPRINTF(
            line, sizeof(line), NOTE_EDIT_OPEN_Q3_FMT("#CAP,NOTE_EDIT_OPEN,q3,only_visual"));
        if (prefix > 0) {
            used = static_cast<size_t>(prefix);
            appendNoteIdSample(line, sizeof(line), used, onlyVisualSample, onlyVisualSampleCount);
            logger.info("%s", line);
        }
    }
    if (onlyStore > 0) {
        char line[kNoteEditOpenQ3LineBytes];
        size_t used = 0;
        const int prefix = NOTE_EDIT_OPEN_Q3_SNPRINTF(
            line, sizeof(line), NOTE_EDIT_OPEN_Q3_FMT("#CAP,NOTE_EDIT_OPEN,q3,only_store"));
        if (prefix > 0) {
            used = static_cast<size_t>(prefix);
            appendNoteIdSample(line, sizeof(line), used, onlyStoreSample, onlyStoreSampleCount);
            logger.info("%s", line);
        }
    }
}

}  // namespace
#endif

EDIT_MANAGER_IMPL_MEM void EditManager::openNoteEditSession(Track& track) {
    if (editSession.active) {
        return;
    }
    const bool committedLiveOverdub = track.isOverdubbing();
    if (committedLiveOverdub) {
        track.stopOverdubbing();
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    // Capture chunks are the rematerialize source for commitEditAction. Assign here so
    // session rematerialize and pass rematerialize share NoteId (145518 Layer B).
    loop.assignMissingNoteIdsInCommittedCapturePasses();
    const bool snapshotCurrent = loop.publishedSnapshotIsCurrent();
    // Hydrate 4a: current-root open does not rematerialize to analyze or paint.
    // Miss keeps VCACHE,full + rematerialize + full current-state build.
    if (noteEditOpenRematerializesLoop(snapshotCurrent)) {
        loop.rebuildVisualCacheFromPasses();
    }
    invalidateNoteEditDerivedCaches();
    editSession.sessionType = EditSessionType::Note;
    editSession.active = true;
    editSession.editPassIndex = 0;
    editSession.editPassIds.clear();
    editSession.durableCheckpointedEditPassIds.clear();
    editSession.durableCheckpointCreated = false;
    editSession.applyOwnedEditPassRows.clear();
    editSession.replaceEditPassOnClose = false;
    editSession.undoStack.clear();
    editSession.noteEditCurrentState.clear();
    editSession.openedOnCurrentSnapshot = false;
    editSession.openedSnapshotRevision = 0;
    DIAG_EVENT(Diagnostics::Edit::NoteEditOpenEnter);
    if (noteEditOpenRematerializesLoop(snapshotCurrent)) {
        loop.rematerializeEditView(editSession.store.mutStore());
        DIAG_COUNTER_INC(Materialize);
        DIAG_EVENT(Diagnostics::Edit::AfterRematerializeEditView);
    } else {
        editSession.openedOnCurrentSnapshot = true;
        editSession.openedSnapshotRevision = loop.publishedSnapshotRevision();
    }
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,NOTE_EDIT_OPEN,sync,visual_notes,%u,session_events,%zu,current_state,%u",
                static_cast<unsigned>(loop.visualCache.notes.size()),
                editSession.store.readStore().size(),
                static_cast<unsigned>(editSession.noteEditCurrentState.size()));
#endif
#if NOTE_EDIT_OPEN_BISECT_STAGE <= 0
    return;
#endif
    if (noteEditOpenRematerializesLoop(snapshotCurrent)) {
        loop.assignMissingNoteIdsInStore(editSession.store.mutStore());
#if defined(SESSION_CAPTURE)
        logNoteEditOpenQ3(loop, editSession.store.readStore());
#endif
        SessionMidiEventVec committedFlat;
        loop.copyEffectiveCommittedEvents(committedFlat);
        loop.assignMissingNoteIds(committedFlat);
        loop.assignMissingNoteIds(editSession.store.mutEvents());
        stampNoteIdsOntoPairedNoteOffs(editSession.store.mutEvents());
        editSession.noteEditCurrentState =
            NoteEditCurrentState::buildFromSessionStore(editSession.store.readEvents(),
                                                        track.getMidiChannel());
        ensureCurrentStateVisibleRowsFromVisualCache(track);
        editSession.noteEditCurrentState.sealRematerializeBaseline();
        DIAG_EVENT(Diagnostics::Edit::AfterAssignNoteIds);
    } else {
#if defined(SESSION_CAPTURE)
        logNoteEditOpenQ3(loop, editSession.store.readStore());
#endif
    }
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
        editSession.noteEditCurrentState.clear();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.applyOwnedEditPassRows.clear();
        editSession.openedOnCurrentSnapshot = false;
        editSession.openedSnapshotRevision = 0;
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
    if (!editSession.editPassIds.empty()) {
        Loop& loop = trackManager.getSelectedLoop(track);
        // DEC-056: do not publish NOTE_EDIT rows onto a predecessor-stale parent.
        // Opened-current (or now-current) close keeps today's synchronous publish.
        const ImmutableLoopSnapshotRef beforePublished = loop.publishedSnapshot();
        if (editSession.openedOnCurrentSnapshot || loop.publishedSnapshotIsCurrent()) {
            loop.tryPublishNoteEditSnapshot(editSession.editPassIds);
        } else {
            logger.log(CAT_TRACK, LOG_INFO,
                       "NoteEditPass close skip stale-parent publish rows=%u",
                       static_cast<unsigned>(editSession.editPassIds.size()));
        }
        if (loop.publishedSnapshotIsCurrent()) {
            editSession.openedOnCurrentSnapshot = true;
            editSession.openedSnapshotRevision = loop.publishedSnapshotRevision();
        } else {
            editSession.openedOnCurrentSnapshot = false;
            editSession.openedSnapshotRevision = 0;
        }
        if (!idsToPush.empty()) {
            TrackUndo::pushNoteEditPassClosed(track, editSession.editPassIndex, idsToPush,
                                              beforePublished);
            logger.log(CAT_TRACK, LOG_INFO, "NoteEditPassClosed editPass=%u edits=%u",
                       static_cast<unsigned>(editSession.editPassIndex),
                       static_cast<unsigned>(idsToPush.size()));
        }
    } else if (!idsToPush.empty()) {
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
    Loop& loop = trackManager.getSelectedLoop(track);
    if (loop.captureActive() || loop.hasOverdubSession()) {
        const NoteUtils::DisplayNoteVec projected = projectedNoteEditDisplayNotes(track);
        if (!projected.empty() && projected.size() >= loop.visualCache.notes.size()) {
            loop.adoptComposedDisplayNotesFromViewport(projected);
        }
    }
    editSession.store.mutStore().clear();
    editSession.store.discardEventsCache();
    editSession.noteEditCurrentState.clear();
    editSession.focus.clear();
    editSession.applyOwnedEditPassRows.clear();
    editSession.openedOnCurrentSnapshot = false;
    editSession.openedSnapshotRevision = 0;
    loopEditManager.armLoopFaderFeedbackIgnore();
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
    editSession.openedOnCurrentSnapshot = false;
    editSession.openedSnapshotRevision = 0;
    editSession.store.mutStore().clear();
    editSession.store.discardEventsCache();
    editSession.noteEditCurrentState.clear();
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

    loopEditManager.armLoopFaderFeedbackIgnore();
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
        editSession.noteEditCurrentState.clear();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.durableCheckpointedEditPassIds.clear();
        editSession.durableCheckpointCreated = false;
        editSession.openedOnCurrentSnapshot = false;
        editSession.openedSnapshotRevision = 0;
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
    const uint8_t channel = track.getMidiChannel();
    if (editSession.noteEditCurrentState.empty()) {
        editSession.noteEditCurrentState =
            NoteEditCurrentState::buildFromSessionStore(editSession.store.readEvents(), channel);
        ensureCurrentStateVisibleRowsFromVisualCache(track);
    }
    const NoteEditCurrentState undoCurrentState = editSession.noteEditCurrentState.clone();

    MidiEventVec captureFlat;
    loop.capture.store.copyEventsTo(captureFlat);
    loop.assignMissingNoteIds(captureFlat);
    editSession.noteEditCurrentState.mergeCaptureNotesAsAdded(captureFlat, channel, loopLength);
    refreshNoteEditSessionProjection(channel);
    loop.discardCapture();
    loop.discardPendingCapturePass();

    MidiEventVec baselineProjected;
    undoCurrentState.projectToSessionStore(baselineProjected, channel);
    const EditPassVec redoRows =
        buildSessionStoreEditPasses(baselineProjected, editSession.store.readEvents(), channel,
                                    loopLength);
    if (redoRows.empty()) {
        return;
    }

    SessionUndoEntry entry;
    entry.selection = sessionState.selection;
    entry.focus = snapshotFocusForSessionUndo(editSession.focus);
    entry.editPassIdsAtPush = editSession.editPassIds;
    entry.undoCurrentState = undoCurrentState;
    entry.hasUndoCurrentState = true;
    entry.redoEditRows = redoRows;
    entry.redoCurrentState = editSession.noteEditCurrentState.clone();
    entry.hasRedoCurrentState = true;
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
