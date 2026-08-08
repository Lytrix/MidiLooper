//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <unordered_set>
#include <utility>

#include "EditApply.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "NoteEditFocus.h"
#include "NoteEditCurrentState.h"
#include "NoteEditSessionState.h"
#include "NoteEditSessionUndo.h"
#include "TrackManager.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/NoteEditDisplaySnapshot.h"

EDIT_MANAGER_IMPL_MEM void EditManager::commitAllPendingNoteEditActions(Track& track) {
    flushDeferredNoteEditDisplayRefresh(track);
    if (!editSession.active || !editSession.focus.active) {
        return;
    }
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    // Projection flat for overlap pre-commit + validation; normalize via projection owner.
    MidiEventVec& sessionStoreEvents = sessionMidiEvents();
    const bool hasPendingMoverCommit = noteEditFocusHasPendingCommit(editSession.focus);
    const bool hasPendingApplyOwnedRows = !editSession.applyOwnedEditPassRows.empty();
    const bool hasPendingOverlapCommit =
        hasPendingApplyOwnedRows ||
        noteEditFocusHasPendingBaselineMapDiff(
            editSession.focus, sessionStoreEvents, channel, loopLength,
            editSession.noteEditCurrentState.empty() ? nullptr
                                                     : &editSession.noteEditCurrentState);
    if (!hasPendingMoverCommit && !hasPendingOverlapCommit) {
        return;
    }
    pruneOverlapNotesBeforePreCommit(editSession.focus, sessionStoreEvents, channel);
    resolveOverlapNotesForPreCommit(sessionStoreEvents, editSession.focus, channel, loopLength);

    normalizeNoteEditSessionProjectionForCommit(track);
    syncNoteEditFocusLinearFromSessionStore(editSession.focus, sessionStoreEvents, channel,
                                            loopLength);

    if (!isLiveEditDriverValid(sessionState.selection, editSession.focus, sessionStoreEvents,
                               channel, loopLength)) {
#if defined(SESSION_CAPTURE)
        logger.log(CAT_TRACK, LOG_WARNING,
                   "NOTE_EDIT macro commit skipped: driver invalid (moving=%lu primary=%lu "
                   "focus_last=%lu-%lu)",
                   static_cast<unsigned long>(editSession.focus.movingNoteId),
                   static_cast<unsigned long>(sessionState.selection.primaryNote),
                   static_cast<unsigned long>(editSession.focus.last.startTick),
                   static_cast<unsigned long>(editSession.focus.last.endTick));
#endif
        return;
    }

    const LoopEventValidation::LoopEventValidationResult macroInvariantResult =
        LoopEventValidation::validateLoopEvents(sessionStoreEvents, loopLength,
                                                LoopEventValidation::kCanonicalInvariantMask);
    if (!macroInvariantResult.passed) {
        logger.log(CAT_TRACK, LOG_WARNING,
                   "NOTE_EDIT macro commit: non-canonical store (check=%u)",
                   static_cast<unsigned>(macroInvariantResult.firstFailure));
    }

#if defined(SESSION_CAPTURE)
    const EditPassVec applyOwnedRows = editSession.applyOwnedEditPassRows;
#endif
    EditPassVec rows;
    if (!editSession.noteEditCurrentState.empty()) {
        rows = buildCommitRowsFromCurrentState(editSession.focus, editSession.noteEditCurrentState,
                                               channel, loopLength);
#if defined(SESSION_CAPTURE)
        const EditPassVec parityRows = buildPreCommitEditPasses(
            editSession.focus, channel, &sessionStoreEvents, loopLength,
            &editSession.noteEditCurrentState);
        logApplyOwnedCommitParity(rows, parityRows);
#endif
    } else {
        rows = buildPreCommitEditPasses(editSession.focus, channel, &sessionStoreEvents, loopLength,
                                        nullptr);
    }
    editSession.applyOwnedEditPassRows.clear();
#if defined(SESSION_CAPTURE)
    if (!applyOwnedRows.empty()) {
        logApplyOwnedCommitParity(rows, applyOwnedRows);
    }
#endif
    if (rows.empty()) {
        return;
    }

    logPreCommitEditPassRows(editSession.focus, rows, false);

    markOverlapDeleteRowsEmitted(editSession.focus, rows);
    const NoteIdList committedOverlapDeleteIds =
        collectCommittedOverlapDeleteIds(editSession.focus, rows);
    const std::vector<std::pair<NoteId, NoteBaseline>> committedOverlapUpdateBaselines =
        collectCommittedOverlapUpdateBaselines(editSession.focus, rows);
    const EditPassId id = commitEditAction(track, std::move(rows), false);
    if (id == kInvalidEditPassId) {
        return;
    }
    lastPushedGeometryKind_ = NoteEditKind::Select;

    track.invalidateCaches();

    editSession.focus.commitBaseline = editSession.focus.last;
    editSession.focus.movingNoteRange.start = editSession.focus.last.startTick;
    editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
    if (!editSession.noteEditCurrentState.empty()) {
        for (const auto& [noteId, baseline] : committedOverlapUpdateBaselines) {
            editSession.noteEditCurrentState.syncCommittedSpan(noteId, baseline);
        }
        if (editSession.focus.movingNoteId != kInvalidNoteId) {
            editSession.noteEditCurrentState.syncCommittedSpan(editSession.focus.movingNoteId,
                                                               editSession.focus.commitBaseline);
        }
        refreshNoteEditSessionProjection(channel);
        bumpSessionPreviewRevision();
    }
    for (const auto& [noteId, baseline] : committedOverlapUpdateBaselines) {
        applyCommittedOverlapUpdateToFocus(editSession.focus, noteId, baseline);
    }
    // Stage 7.5.E2 (022849): seal committed overlap Deletes in currentState so reselect cannot
    // reinsert/RestoreNote sealed-hidden participants when the mover passes again.
    for (NoteId noteId : committedOverlapDeleteIds) {
        editSession.noteEditCurrentState.markRowDeleted(noteId);
    }
    clearCommittedOverlapDeleteIdsFromFocus(editSession.focus, committedOverlapDeleteIds);
    clearCommittedOverlapScratchExceptHidden(editSession.focus);

    invalidateNoteEditDerivedCaches();

    // Commit rebuilds projection / filtered ordering; resync index from NoteId + focus.last.
    if (editorSelectionHasNote(sessionState.selection) && editSession.focus.active &&
        editSession.focus.movingNoteId == sessionState.selection.primaryNote) {
        const bool lengthBracket =
            sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
        const uint32_t storageBracketTick =
            lengthBracket ? editSession.focus.last.endTick : editSession.focus.last.startTick;
        const uint32_t displayBracket = NoteEditDisplaySnapshot::displayStartTickFromStorage(
            storageBracketTick, noteEditLoopStartTick(track), loopLength);
        sessionState.selection.selectedTick = displayBracket;
        selectedTick = displayBracket;
        syncSelectedNoteIdxToFilteredInventory(track);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::commitPendingOverlapNoteEdits(Track& track) {
    // Overlap rows now come from baselineMap vs live store (same as macro commit).
    commitAllPendingNoteEditActions(track);
}

EDIT_MANAGER_IMPL_MEM size_t EditManager::bakeNoteEditSessionStoreToPasses(Track& track) {
    if (!editSession.active) {
        return 0;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }

    MidiEventVec baselineStoreEvents;
    materializePassesExcludingEditPasses(loop, editSession.editPassIds, baselineStoreEvents);
    EditPassVec replacementRows =
        buildSessionStoreEditPasses(baselineStoreEvents, editSession.store.readEvents(),
                                    track.getMidiChannel(), noteEditLoopLengthTicks(track));
    if (replacementRows.empty()) {
        return 0;
    }

    trackManager.reclaimUnreferencedDisabledPasses();
    size_t savedRows = 0;

    if (editSession.editPassIds.empty()) {
        const EditPassType passType = passTypeForSession(editSession.sessionType);
        for (EditPass& row : replacementRows) {
            const EditPassId id =
                loop.saveNoteEditPass(editSession.editPassIndex, std::move(row), passType);
            if (id != kInvalidEditPassId) {
                editSession.editPassIds.push_back(id);
                ++savedRows;
            }
        }
        if (savedRows > 0) {
            track.invalidateCaches();
            logger.log(CAT_TRACK, LOG_INFO,
                       "NoteEditPass live capture baked editPass=%u rows=%u saved=%u",
                       static_cast<unsigned>(editSession.editPassIndex),
                       static_cast<unsigned>(replacementRows.size()),
                       static_cast<unsigned>(savedRows));
        } else {
            logger.log(CAT_TRACK, LOG_WARNING,
                       "NoteEditPass bake failed editPass=%u rows=%u (heap or admission)",
                       static_cast<unsigned>(editSession.editPassIndex),
                       static_cast<unsigned>(replacementRows.size()));
        }
        return savedRows;
    }

    const EditPassIdList staleEditPassIds = editSession.editPassIds;
    editSession.editPassIds.clear();

    const unsigned replacementRowCount = static_cast<unsigned>(replacementRows.size());
    EditPassIdList replacementIds =
        loop.replaceNoteEditPass(editSession.editPassIndex, staleEditPassIds,
                                 std::move(replacementRows));
    if (replacementIds.empty()) {
        trackManager.reclaimUnreferencedDisabledPasses();
        replacementRows = buildSessionStoreEditPasses(
            baselineStoreEvents, editSession.store.readEvents(), track.getMidiChannel(),
            noteEditLoopLengthTicks(track));
        replacementIds = loop.replaceNoteEditPass(editSession.editPassIndex, staleEditPassIds,
                                                  std::move(replacementRows));
    }

    if (!replacementIds.empty()) {
        for (const EditPassId id : replacementIds) {
            editSession.editPassIds.push_back(id);
        }
        savedRows = replacementIds.size();
        track.invalidateCaches();
        logger.log(CAT_TRACK, LOG_INFO,
                   "NoteEditPass replaced editPass=%u stale=%u replacement=%u rows=%u saved=%u",
                   static_cast<unsigned>(editSession.editPassIndex),
                   static_cast<unsigned>(staleEditPassIds.size()),
                   static_cast<unsigned>(replacementIds.front()), replacementRowCount,
                   static_cast<unsigned>(savedRows));
    } else {
        editSession.editPassIds = staleEditPassIds;
        logger.log(CAT_TRACK, LOG_WARNING,
                   "NoteEditPass replace rejected editPass=%u stale=%u rows=%u",
                   static_cast<unsigned>(editSession.editPassIndex),
                   static_cast<unsigned>(staleEditPassIds.size()), replacementRowCount);
    }
    return savedRows;
}

EDIT_MANAGER_IMPL_MEM EditPassId EditManager::commitEditAction(Track& track, EditPassVec rows,
                                                              bool applySessionOverlay) {
    if (!editSession.active || rows.empty()) {
        return kInvalidEditPassId;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint8_t homePitch = editSession.focus.commitBaseline.pitch;
    const uint32_t homeStart = editSession.focus.commitBaseline.startTick;
    const uint32_t loopLength = loop.loopLengthTicks;

    for (const EditPass& row : rows) {
        if (row.actionType == EditActionType::Update &&
            row.propertyType == EditPropertyType::Length) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "commitEditAction incoming ChangeLength targetNoteId=%lu "
                       "start=%lu moverBaselineEnd=%lu newEnd=%lu",
                       static_cast<unsigned long>(row.targetNoteId),
                       static_cast<unsigned long>(row.startTick),
                       static_cast<unsigned long>(editSession.focus.commitBaseline.endTick),
                       static_cast<unsigned long>(row.endTick));
        }
    }

    const EditPassType passType = passTypeForSession(editSession.sessionType);
    EditPassId lastId = kInvalidEditPassId;
    for (EditPass row : rows) {
        EditPassId id = loop.saveNoteEditPass(editSession.editPassIndex, row, passType);
        if (id == kInvalidEditPassId) {
            trackManager.reclaimUnreferencedDisabledPasses();
            id = loop.saveNoteEditPass(editSession.editPassIndex, std::move(row), passType);
        }
        if (id != kInvalidEditPassId) {
            lastId = id;
            editSession.editPassIds.push_back(id);
            editSession.durableCheckpointCreated = false;
        }
    }
    if (lastId == kInvalidEditPassId) {
        return kInvalidEditPassId;
    }

    unsigned activeEditPasses = 0;
    unsigned activeCapturePasses = 0;
    for (const EditPass& editPass : loop.passes.editPasses) {
        if (editPass.state == EditPassState::Active) {
            ++activeEditPasses;
        }
    }
    activeCapturePasses = static_cast<unsigned>(loop.activeCapturePassCount());
    logger.log(CAT_TRACK, LOG_INFO,
               "commitEditAction trace: editId=%u activeEditPasses=%u activeCapturePasses=%u editPass=%u",
               static_cast<unsigned>(lastId), activeEditPasses, activeCapturePasses,
               static_cast<unsigned>(editSession.editPassIndex));

    for (const EditPass& editPass : loop.passes.editPasses) {
        if (editPass.id != lastId) {
            continue;
        }
        if (editPass.actionType == EditActionType::Update &&
            editPass.propertyType == EditPropertyType::Length) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "commitEditAction saved ChangeLength targetNoteId=%lu "
                       "start=%lu moverBaselineEnd=%lu newEnd=%lu",
                       static_cast<unsigned long>(editPass.targetNoteId),
                       static_cast<unsigned long>(editPass.startTick),
                       static_cast<unsigned long>(editSession.focus.commitBaseline.endTick),
                       static_cast<unsigned long>(editPass.endTick));
        }
    }

    // Drop live session flat before replay — takes + edits[] is canonical after saveNoteEditPass.
    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }
    const MidiEventVec sessionSnapshot = editSession.store.readEvents();
    editSession.store.discardEventsCache();
    SessionMidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector(loopMidiEventsFromPasses, loopLength);
    logChangeLengthCommitTrace("replay_flat", loopMidiEventsFromPasses, loopLength, homePitch,
                               homeStart);

    MidiEventVec takeOnlyFlat;
    loop.mergeActiveCapturePasses(takeOnlyFlat);
    logChangeLengthCommitTrace("take_only", takeOnlyFlat, loopLength, homePitch, homeStart);

    if (applySessionOverlay) {
        const EditPassVec sessionOverlay =
            buildSessionStoreEditPasses(loopMidiEventsFromPasses, sessionSnapshot,
                                        track.getMidiChannel(), loopLength);
        if (!sessionOverlay.empty()) {
            applyNoteEditPassSequence(loopMidiEventsFromPasses, sessionOverlay, loopLength);
        }
    }

    editSession.store.mutStore().loadFromEvents(loopMidiEventsFromPasses);
    editSession.store.discardEventsCache();
    logChangeLengthCommitTrace("session_store", editSession.store.readEvents(), loopLength,
                               homePitch, homeStart);

    logChangeLengthCommitTrace("loop_materialized", loop.midiEvents(), loopLength, homePitch,
                               homeStart);
    track.invalidateCaches();
    return lastId;
}
