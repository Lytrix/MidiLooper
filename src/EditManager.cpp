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
#include "StorageManager.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "MidiConfig.h"
#include "NoteEditManager.h"
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
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/SelectNavigation.h"
#include "Utils/ValidationUtils.h"
#include <map>
#include <vector>
#include <unordered_set>
#include <cmath>

using DisplayNote = NoteUtils::DisplayNote;

namespace {

void markOverlapDeleteRowsEmitted(NoteEditFocus& focus, const EditPassVec& rows) {
    for (const EditPass& row : rows) {
        if (row.actionType != EditActionType::Delete) {
            continue;
        }
        if (OverlapNote* entry = findOverlapNoteEntry(focus, row.targetNoteId)) {
            entry->preCommitEmitted = true;
        }
    }
}

void clearCommittedOverlapScratchExceptHidden(NoteEditFocus& focus) {
    for (auto it = focus.overlapNotes.begin(); it != focus.overlapNotes.end();) {
        if (it->second.state == OverlapNoteStoreState::Hidden) {
            ++it;
        } else {
            it = focus.overlapNotes.erase(it);
        }
    }
}

}  // namespace

#include "NoteEditFocus.h"
#include "EditPass.h"
#include "LoopPasses.h"

namespace {

void logChangeLengthCommitTrace(const char* stage,
                                const MidiEventVec& flat,
                                uint32_t loopLength,
                                uint8_t homePitch,
                                uint32_t homeStart) {
    const std::vector<DisplayNote> notes =
        NoteUtils::reconstructNotes(flat, loopLength, false);
    for (const DisplayNote& n : notes) {
        if (n.note == homePitch && n.startTick == homeStart) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "commitEditAction %s: M%d start=%lu end=%lu flatEvents=%u",
                       stage, static_cast<unsigned>(homePitch),
                       static_cast<unsigned long>(n.startTick),
                       static_cast<unsigned long>(n.endTick),
                       static_cast<unsigned>(flat.size()));
            return;
        }
    }
    logger.log(CAT_TRACK, LOG_INFO,
               "commitEditAction %s: M%d@%lu missing in recon flatEvents=%u",
               stage, static_cast<unsigned>(homePitch),
               static_cast<unsigned long>(homeStart),
               static_cast<unsigned>(flat.size()));
}

void materializePassesExcludingEditPasses(const Loop& loop, const EditPassIdList& editPassIds,
                                          MidiEventVec& out) {
    loop.materializeExcludingEditPassIds(editPassIds, out);
}

}  // namespace

void EditManager::ensureNoteEditFocusForLiveEdit(Track& track,
                                                  const DisplayNote& fallbackWhenNoFocus) {
    if (!editSession.active || editSession.focus.active) {
        return;
    }
    if (getSelectedNoteIdx() < 0) {
        return;
    }
    rebuildNoteEditFocusForDisplayNote(track, fallbackWhenNoFocus);
}

void EditManager::commitAllPendingNoteEditActions(Track& track) {
    if (!editSession.active || !editSession.focus.active) {
        return;
    }

    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    MidiEventVec& sessionStoreEvents = sessionMidiEvents();
    pruneOverlapNotesBeforePreCommit(editSession.focus, sessionStoreEvents, channel);
    resolveOverlapNotesForPreCommit(sessionStoreEvents, editSession.focus, channel, loopLength);

    const std::unordered_set<NoteId> closure =
        buildEditClosureNoteIds(editSession.focus, sessionStoreEvents, channel, loopLength);
    if (!closure.empty()) {
        LoopTickNormalize::NormalizeOptions microOptions;
        microOptions.closeOpenTails = false;
        LoopTickNormalize::normalize(sessionStoreEvents, loopLength,
                                     LoopTickNormalize::NormalizeScope::noteIds(closure),
                                     microOptions);
    }
    syncNoteEditFocusLinearFromSessionStore(editSession.focus, sessionStoreEvents, channel,
                                            loopLength);
    LoopTickNormalize::normalizeAll(sessionStoreEvents, loopLength);
    syncNoteEditFocusLinearFromSessionStore(editSession.focus, sessionStoreEvents, channel,
                                            loopLength);

    const LoopEventValidation::LoopEventValidationResult macroInvariantResult =
        LoopEventValidation::validateLoopEvents(sessionStoreEvents, loopLength,
                                                LoopEventValidation::kCanonicalInvariantMask);
    if (!macroInvariantResult.passed) {
        logger.log(CAT_TRACK, LOG_WARNING,
                   "NOTE_EDIT macro commit: non-canonical store (check=%u)",
                   static_cast<unsigned>(macroInvariantResult.firstFailure));
    }

    EditPassVec rows = buildPreCommitEditPasses(editSession.focus, channel);
    if (rows.empty()) {
        return;
    }

    for (const EditPass& row : rows) {
        if (row.actionType == EditActionType::Update &&
            row.propertyType == EditPropertyType::Length) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "Edit committed ChangeLength start=%lu baselineEnd=%lu newEnd=%lu",
                       static_cast<unsigned long>(row.startTick),
                       static_cast<unsigned long>(editSession.focus.commitBaseline.endTick),
                       static_cast<unsigned long>(row.endTick));
        }
    }

    markOverlapDeleteRowsEmitted(editSession.focus, rows);
    const EditPassId id = commitEditAction(track, std::move(rows));
    if (id == kInvalidEditPassId) {
        return;
    }
    lastPushedGeometryKind_ = NoteEditKind::Select;

    track.invalidateCaches();

    editSession.focus.commitBaseline = editSession.focus.last;
    editSession.focus.movingNoteRange.start = editSession.focus.last.startTick;
    editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
    clearCommittedOverlapScratchExceptHidden(editSession.focus);
}

void EditManager::commitPendingOverlapNoteEdits(Track& track) {
    if (!editSession.active || !editSession.focus.active) {
        return;
    }
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    EditPassVec rows = buildPreCommitOverlapEditPasses(editSession.focus);
    if (rows.empty()) {
        return;
    }

    MidiEventVec& sessionStoreEvents = sessionMidiEvents();
    resolveOverlapNotesForPreCommit(sessionStoreEvents, editSession.focus, channel,
                                    loopLength);

    markOverlapDeleteRowsEmitted(editSession.focus, rows);
    const EditPassId id = commitEditAction(track, std::move(rows));
    if (id == kInvalidEditPassId) {
        return;
    }

    track.invalidateCaches();
    clearCommittedOverlapScratchExceptHidden(editSession.focus);
}

void EditManager::rebuildNoteEditFocusAtSelect(Track& track, int selectedNoteIdx) {
    if (!editSession.active) {
        editSession.focus.clear();
        return;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);

    // commitBaseline / baselineMap come from committed Takes + Edits replay, not the live
    // session preview — otherwise a pending length preview (e.g. end 680) becomes baseline
    // on fader-1 reselect and the next ChangeLength commit is a no-op on rematerialize.
    MidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector( loopMidiEventsFromPasses, loopLength);
    rebuildNoteEditFocusFromStore(editSession.focus, loopMidiEventsFromPasses, channel,
                                  loopLength, selectedNoteIdx);

    const std::vector<DisplayNote> liveNotes =
        NoteUtils::reconstructNotes(sessionMidiEvents(), loopLength, false);
    if (selectedNoteIdx >= 0 &&
        selectedNoteIdx < static_cast<int>(liveNotes.size())) {
        const DisplayNote& live = liveNotes[static_cast<size_t>(selectedNoteIdx)];
        const NoteId noteId = editSession.focus.movingNoteId;
        MidiEventVec& sessionEvents = sessionMidiEvents();
        NoteBaseline linearBaseline;
        if (noteId != kInvalidNoteId &&
            findLinearNoteSpanForNoteId(sessionEvents, noteId, channel, linearBaseline)) {
            editSession.focus.last = linearBaseline;
        } else {
            editSession.focus.last = {live.note, live.velocity, live.startTick, live.endTick};
        }
        editSession.focus.movingNoteRange.start = editSession.focus.last.startTick;
        editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
    }
}

void EditManager::rebuildNoteEditFocusForDisplayNote(Track& track,
                                                     const DisplayNote& liveSelected) {
    if (!editSession.active) {
        editSession.focus.clear();
        return;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    MidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector( loopMidiEventsFromPasses, loopLength);
    rebuildNoteEditFocusFromStore(editSession.focus, loopMidiEventsFromPasses, channel,
                                  loopLength, -1);

    const NoteId baselineNoteId = findBaselineNoteIdForDisplay(editSession.focus, liveSelected);
    editSession.focus.movingNoteId = baselineNoteId;

    MidiEventVec& sessionEvents = sessionMidiEvents();
    NoteBaseline linearBaseline;
    if (baselineNoteId != kInvalidNoteId &&
        findLinearNoteSpanForNoteId(sessionEvents, baselineNoteId, channel, linearBaseline)) {
        editSession.focus.commitBaseline = linearBaseline;
        editSession.focus.last = linearBaseline;
    } else {
        editSession.focus.commitBaseline = {liveSelected.note, liveSelected.velocity,
                                          liveSelected.startTick, liveSelected.endTick};
        editSession.focus.last = editSession.focus.commitBaseline;
    }
    editSession.focus.movingNoteRange.start = editSession.focus.last.startTick;
    editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
    editSession.focus.active = true;
}

void EditManager::syncSelectedNoteIdxToFilteredInventory(Track& track) {
    if (!editSession.active) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    const std::vector<DisplayNote> filtered =
        noteEditManager.selectableDisplayNotesForEditUi(track);

    if (!editorSelectionHasNote(sessionState.selection)) {
        if (selectedNoteIdx >= 0) {
            setSelectedNoteIdx(-1);
        }
        return;
    }

    int matchIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
        sessionState.selection, filtered);
    if (matchIdx < 0) {
        setSelectedNoteIdx(-1);
    } else if (matchIdx != selectedNoteIdx) {
        setSelectedNoteIdx(matchIdx);
    }
}

NoteUtils::DisplayNoteVec EditManager::selectableDisplayNotesAtEditSelect(const Track& track) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (isNoteEditActive()) {
        return filterSelectableDisplayNotes(track.editAwareMidiEvents(), editSession.focus,
                                            track.getMidiChannel(), loopLength);
    }
    return track.getCachedNotes();
}

DisplayNote EditManager::liveEditDisplayNoteAtSelect(const Track& track) const {
    const int idx = getSelectedNoteIdx();
    const NoteUtils::DisplayNoteVec& notes = selectableDisplayNotesAtEditSelect(track);
    if (idx < 0 || idx >= static_cast<int>(notes.size())) {
        return {};
    }
    if (isNoteEditActive() && editSession.focus.active) {
        const NoteBaseline& last = editSession.focus.last;
        return {editSession.focus.movingNoteId, last.pitch, last.velocity, last.startTick,
                last.endTick};
    }
    return notes[static_cast<size_t>(idx)];
}

void EditManager::syncNoteEditFocusLastFromSessionStore(Track& track) {
    if (!editSession.active || !editSession.focus.active) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    NoteEditFocus& focus = editSession.focus;
    MidiEventVec& events = sessionMidiEvents();
    syncNoteEditFocusLinearFromSessionStore(focus, events, track.getMidiChannel(), loopLength);
}

EditManager editManager;

uint32_t EditManager::noteEditLoopLengthTicks(const Track& track) const {
    if (isNoteEditActive()) {
        return trackManager.getSelectedLoop(track).loopLengthTicks;
    }
    return track.getLoopLength();
}

uint32_t EditManager::noteEditLoopStartTick(const Track& track) const {
    if (isNoteEditActive()) {
        const Loop& loop = trackManager.getSelectedLoop(track);
        const uint32_t loopLength = loop.loopLengthTicks;
        return loopLength > 0 ? loop.loopStartTick % loopLength : 0;
    }
    const uint32_t loopLength = track.getLoopLength();
    return loopLength > 0 ? track.getLoopStartTick() % loopLength : 0;
}

void EditManager::openNoteEditSession(Track& track) {
    if (editSession.active) {
        return;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    editSession.sessionType = EditSessionType::Note;
    editSession.active = true;
    editSession.editPassIndex = 0;
    editSession.editPassIds.clear();
    editSession.replaceEditPassOnClose = false;
    editSession.undoStack.clear();
    loop.rematerializeEditView(editSession.store.mutStore());
    loop.assignMissingNoteIdsInStore(editSession.store.mutStore());
    loop.assignMissingNoteIds(loop.midiEvents());
    loop.assignMissingNoteIds(editSession.store.mutFlat());
    editSession.store.discardFlatCache();
    resetNoteEditSessionState();
    noteEditManager.prepareNoteEditSessionOpen();
    enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
    bumpSessionPreviewRevision();
    noteEditManager.sendNoteEditSessionFaderFeedback(track);
    logger.debug("EditSession opened editPass=0");
}

void EditManager::reopenNoteEditSession(Track& track) {
    if (editSession.active) {
        editSession.store.mutStore().clear();
        editSession.store.discardFlatCache();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.active = false;
        resetNoteEditSessionState();
        selectedNoteIdx = -1;
        hasMovedBracket = false;
    }
    editSession.sessionType = EditSessionType::Note;
    openNoteEditSession(track);
    track.invalidateCaches();
}

void EditManager::closeNoteEditPass(Track& track) {
    if (!editSession.active) {
        return;
    }
    if (editSession.replaceEditPassOnClose) {
        bakeNoteEditSessionStoreToPasses(track);
        editSession.replaceEditPassOnClose = false;
    }
    if (!editSession.editPassIds.empty()) {
        TrackUndo::pushNoteEditPassClosed(track, editSession.editPassIndex,
                                          editSession.editPassIds);
        logger.log(CAT_TRACK, LOG_INFO, "NoteEditPassClosed editPass=%u edits=%u",
                   static_cast<unsigned>(editSession.editPassIndex),
                   static_cast<unsigned>(editSession.editPassIds.size()));
    }
    editSession.editPassIds.clear();
    editSession.undoStack.clear();
    ++editSession.editPassIndex;
}

size_t EditManager::bakeNoteEditSessionStoreToPasses(Track& track) {
    if (!editSession.active) {
        return 0;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    if (editSession.store.isFlatDirty()) {
        editSession.store.syncFlatToStore();
    }

    MidiEventVec baselineStoreEvents;
    materializePassesExcludingEditPasses(loop, editSession.editPassIds, baselineStoreEvents);
    EditPassVec replacementRows =
        buildSessionStoreEditPasses(baselineStoreEvents, editSession.store.readFlat(),
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
            baselineStoreEvents, editSession.store.readFlat(), track.getMidiChannel(),
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

void EditManager::closeNoteEditSession(Track& track) {
    if (!editSession.active) {
        return;
    }
    closeNoteEditPass(track);
    editSession.store.mutStore().clear();
    editSession.store.discardFlatCache();
    editSession.active = false;
    editSession.sessionType = EditSessionType::Loop;
    editSession.editPassIndex = 0;
    editSession.replaceEditPassOnClose = false;
    clearLastFader1SelectNoteId();
}

void EditManager::revertNoteEditSessionForLoopClear(Track& track) {
    if (editSession.sessionType != EditSessionType::Note && !editSession.active) {
        return;
    }

    noteEditManager.resetLengthEditingModeOnSessionBoundary();
    editSession.replaceEditPassOnClose = false;
    editSession.editPassIds.clear();
    editSession.undoStack.clear();
    editSession.store.mutStore().clear();
    editSession.store.discardFlatCache();
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

void EditManager::rematerializeNoteEditSessionAfterWorkspaceReload(Track& track) {
    if (editSession.sessionType == EditSessionType::Note) {
        reopenNoteEditSession(track);
        return;
    }
    if (editSession.active) {
        editSession.store.mutStore().clear();
        editSession.store.discardFlatCache();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.active = false;
        resetNoteEditSessionState();
        selectedNoteIdx = -1;
        hasMovedBracket = false;
    }
    track.invalidateCaches();
}

EditPassId EditManager::commitEditAction(Track& track, EditPassVec rows) {
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
                       "commitEditAction incoming ChangeLength note=%u "
                       "start=%lu baselineEnd=%lu newEnd=%lu",
                       static_cast<unsigned>(editSession.focus.commitBaseline.pitch),
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
                       "commitEditAction saved ChangeLength note=%u "
                       "start=%lu baselineEnd=%lu newEnd=%lu",
                       static_cast<unsigned>(editSession.focus.commitBaseline.pitch),
                       static_cast<unsigned long>(editPass.startTick),
                       static_cast<unsigned long>(editSession.focus.commitBaseline.endTick),
                       static_cast<unsigned long>(editPass.endTick));
        }
    }

    // Drop live session flat before replay — takes + edits[] is canonical after saveNoteEditPass.
    if (editSession.store.isFlatDirty()) {
        editSession.store.syncFlatToStore();
    }
    const MidiEventVec sessionSnapshot = editSession.store.readFlat();
    editSession.store.discardFlatCache();
    MidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector( loopMidiEventsFromPasses,
                     loopLength);
    logChangeLengthCommitTrace("replay_flat", loopMidiEventsFromPasses,
                               loopLength, homePitch, homeStart);

    MidiEventVec takeOnlyFlat;
    loop.mergeActiveCapturePasses(takeOnlyFlat);
    logChangeLengthCommitTrace("take_only", takeOnlyFlat, loopLength, homePitch,
                               homeStart);

    const EditPassVec sessionOverlay =
        buildSessionStoreEditPasses(loopMidiEventsFromPasses, sessionSnapshot,
                                    track.getMidiChannel(), loopLength);
    if (!sessionOverlay.empty()) {
        applyNoteEditPassSequence(loopMidiEventsFromPasses, sessionOverlay, loopLength);
    }

    editSession.store.mutStore().loadFromFlat(loopMidiEventsFromPasses);
    editSession.store.discardFlatCache();
    logChangeLengthCommitTrace("session_store", editSession.store.readFlat(),
                               loopLength, homePitch, homeStart);

    logChangeLengthCommitTrace("loop_materialized", loop.midiEvents(), loopLength,
                               homePitch, homeStart);
    track.invalidateCaches();
    return lastId;
}

bool EditManager::pushSessionUndoOnKindChange(Track& track, NoteEditKind kind) {
    if (!shouldPushGeometryKindUndo(lastPushedGeometryKind_, kind)) {
        return true;
    }
    if (!editSession.active) {
        return true;
    }
    if (editSession.store.isFlatDirty()) {
        editSession.store.syncFlatToStore();
    }
    const SessionUndoEntry entry =
        buildSessionUndoEntry(editSession.focus, sessionState.selection,
                              editSession.store.readFlat(), track.getMidiChannel(),
                              noteEditLoopLengthTicks(track), editSession.editPassIds);
    if (!editSession.undoStack.pushEntry(entry)) {
        trackManager.reclaimUnreferencedDisabledPasses();
        if (!editSession.undoStack.pushEntry(entry)) {
            logger.log(CAT_TRACK, LOG_WARNING,
                       "Session undo push rejected: heap below reserve (need=%u free=%u)",
                       static_cast<unsigned>(Config::HEAP_RESERVE_BYTES +
                                             estimatedSessionUndoEntryBytes(entry)),
                       static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
            return false;
        }
    }
    lastPushedGeometryKind_ = kind;
    (void)track;
    return true;
}

void EditManager::foldLiveCaptureIntoNoteEditSession(Track& track, uint32_t closeTick) {
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

    loop.ensureCaptureEventsSorted();

    if (loopLength > 0) {
        (void)LoopStopFinalize::finalizeWrapWindowOnStore(loop.capture.store, loopLength, closeTick,
                                                          Config::TICKS_PER_BAR);
    }

    if (editSession.store.isFlatDirty()) {
        editSession.store.syncFlatToStore();
    }
    const MidiEventVec baselineStoreEvents = editSession.store.readFlat();

    MidiEventVec captureFlat;
    loop.capture.store.flatten(captureFlat);
    loop.assignMissingNoteIds(captureFlat);
    MidiEventVec& sessionFlat = editSession.store.mutFlat();
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
    editSession.store.syncFlatToStore();
    loop.discardCapture();
    loop.discardPendingCapturePass();

    const EditPassVec redoRows = buildSessionStoreEditPasses(
        baselineStoreEvents, editSession.store.readFlat(), track.getMidiChannel(), loopLength);
    if (redoRows.empty()) {
        return;
    }

    SessionUndoEntry entry;
    entry.selection = sessionState.selection;
    entry.focus = editSession.focus;
    entry.editPassIdsAtPush = editSession.editPassIds;
    entry.redoEditRows = redoRows;
    entry.redoFocus = editSession.focus;
    entry.redoSelection = sessionState.selection;
    entry.redoEditPassIds = editSession.editPassIds;
    entry.hasRedoPayload = true;

    if (!editSession.undoStack.pushEntry(entry)) {
        trackManager.reclaimUnreferencedDisabledPasses();
        if (!editSession.undoStack.pushEntry(entry)) {
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

void EditManager::restoreSessionUndoEntry(Track& track, const SessionUndoEntry& entry) {
    Loop& loop = trackManager.getSelectedLoop(track);
    applySessionUndoEntry(loop, editSession.store, entry, noteEditLoopLengthTicks(track),
                          editSession.editPassIds);
    editSession.focus = entry.focus;
    sessionState.selection = entry.selection;
    syncNoteEditSessionStateToUi(track);
    syncSelectedNoteIdxToFilteredInventory(track);
}

void EditManager::applyGeometryKindFromControl(Track& track, NoteEditKind kind,
                                               bool fromFaderControl) {
    (void)track;
    sessionState.kind = kind;
    if (fromFaderControl && isGeometryEditKind(kind)) {
        encoderCycleNeedsAnchor_ = true;
    }
}

bool EditManager::beginGeometryMutation(Track& track, NoteEditKind kind, bool fromFaderControl) {
    applyGeometryKindFromControl(track, kind, fromFaderControl);
    return pushSessionUndoOnKindChange(track, kind);
}

void EditManager::resetNoteEditSessionState() {
    sessionState = {};
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;
    sessionPreviewRevision_ = 0;
}

void EditManager::applySelectionFromGeometryEdit(Track& track, uint32_t bracketTick,
                                                 NoteId primaryNote) {
    sessionState.selection.bracketTick = bracketTick;
    sessionState.selection.primaryNote = primaryNote;
    sessionState.selection.selectedNotes.clear();
    if (primaryNote != kInvalidNoteId) {
        sessionState.selection.selectedNotes.push_back(primaryNote);
    }
    sessionState.selection.trackId = static_cast<TrackId>(trackManager.getSelectedTrackIndex());
    sessionState.selection.loopId = trackManager.getSelectedLoop(track).loopId;
    syncGeometrySelectionToUi(track);
}

void EditManager::syncGeometrySelectionToUi(Track& track) {
    bracketTick = sessionState.selection.bracketTick;
    displayManager.requestNoteInfoRefresh(track);
}

void EditManager::applySelectNav(Track& track, uint32_t bracketTick, NoteId primaryNote,
                                 bool requestFaderSync, bool skipFader1Outbound) {
    (void)skipFader1Outbound;
    const EditorSelection priorSelection = sessionState.selection;
    sessionState.kind = NoteEditKind::Select;
    sessionState.selection.bracketTick = bracketTick;
    sessionState.selection.primaryNote = primaryNote;
    sessionState.selection.selectedNotes.clear();
    if (primaryNote != kInvalidNoteId) {
        sessionState.selection.selectedNotes.push_back(primaryNote);
    }
    sessionState.selection.trackId = static_cast<TrackId>(trackManager.getSelectedTrackIndex());
    sessionState.selection.loopId = trackManager.getSelectedLoop(track).loopId;
    if (shouldResetGeometryKindUndoOnSelectChange(priorSelection, primaryNote)) {
        lastPushedGeometryKind_ = NoteEditKind::Select;
    }
    syncNoteEditSessionStateToUi(track);

    const bool selectionIdentityChanged =
        editorSelectionTargetChanged(priorSelection, bracketTick, primaryNote);
    const bool shouldSyncMotors =
        selectionIdentityChanged && primaryNote != kInvalidNoteId;
    if (shouldSyncMotors && !requestFaderSync) {
        noteEditManager.scheduleSelectDependentMotorSync(track, priorSelection, sessionState.selection);
    }
    if (requestFaderSync && primaryNote != kInvalidNoteId) {
        noteEditManager.scheduleNoteSelectFaderSync(track);
    }
}

void EditManager::applyCycleEditKind(Track& track) {
    sessionState.kind =
        resolveEncoderCyclePress(sessionState.kind, encoderCycleNeedsAnchor_);
    encoderCycleNeedsAnchor_ = false;
    syncNoteEditSessionStateToUi(track);
}

void EditManager::syncNoteEditSessionStateToUi(Track& track) {
  const int prevSelectedIdx = selectedNoteIdx;
  const NoteUtils::DisplayNoteVec& notes = selectableDisplayNotesAtEditSelect(track);
  if (editorSelectionHasNote(sessionState.selection)) {
    selectedNoteIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
        sessionState.selection, notes);
  } else {
    selectedNoteIdx = -1;
  }
  bracketTick = sessionState.selection.bracketTick;
    if (editorSelectionHasNote(sessionState.selection)) {
        if (sessionState.selection.primaryNote != kInvalidNoteId) {
            setLastFader1SelectNoteId(sessionState.selection.primaryNote);
        }
    } else {
        clearLastFader1SelectNoteId();
    }
    if (prevSelectedIdx != selectedNoteIdx) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection changed: %d -> %d", prevSelectedIdx,
                   selectedNoteIdx);
    }

    EditModeState mode = EDIT_MODE_SELECT;
    EditNoteState* targetState = &selectNoteState;
    switch (sessionState.kind) {
        case NoteEditKind::Select:
        case NoteEditKind::Add:
        case NoteEditKind::Delete:
            mode = EDIT_MODE_SELECT;
            targetState = &selectNoteState;
            break;
        case NoteEditKind::Move:
            mode = EDIT_MODE_START;
            targetState = &startNoteState;
            break;
        case NoteEditKind::Length:
            mode = EDIT_MODE_LENGTH;
            targetState = &lengthNoteState;
            break;
        case NoteEditKind::Pitch:
            mode = EDIT_MODE_PITCH;
            targetState = &pitchNoteState;
            break;
    }

    currentEditMode = mode;
    if (currentState != targetState) {
        if (currentState) {
            if (currentState == &startNoteState || currentState == &lengthNoteState ||
                currentState == &pitchNoteState) {
                commitAllPendingNoteEditActions(track);
            }
            currentState->onExit(*this, track);
        }
        currentState = targetState;
        if (currentState) {
            currentState->onEnter(*this, track, bracketTick);
        }
    }
    sendEditModeProgram(mode);
}

void EditManager::enterDefaultNoteEditSessionState(Track& track, uint32_t startTick) {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        bracketTick = 0;
        selectedNoteIdx = -1;
        clearLastFader1SelectNoteId();
        sessionState.kind = NoteEditKind::Select;
        sessionState.selection = {};
        return;
    }

    bracketTick = startTick % loopLength;
    selectNoteAtBracket(track, startTick);
    if (selectedNoteIdx < 0) {
        selectClosestNote(track, startTick);
    }

    NoteId primaryNote = kInvalidNoteId;
    if (selectedNoteIdx >= 0) {
        const auto notes = selectableDisplayNotesAtEditSelect(track);
        if (selectedNoteIdx < static_cast<int>(notes.size())) {
            primaryNote = notes[static_cast<size_t>(selectedNoteIdx)].noteId;
            setLastFader1SelectNoteId(primaryNote);
        }
    } else {
        clearLastFader1SelectNoteId();
    }

    sessionState.kind = NoteEditKind::Select;
    sessionState.selection.bracketTick = bracketTick;
    sessionState.selection.primaryNote = primaryNote;
    sessionState.selection.selectedNotes.clear();
    if (primaryNote != kInvalidNoteId) {
        sessionState.selection.selectedNotes.push_back(primaryNote);
    }
    sessionState.selection.trackId = static_cast<TrackId>(trackManager.getSelectedTrackIndex());
    sessionState.selection.loopId = trackManager.getSelectedLoop(track).loopId;
    syncNoteEditSessionStateToUi(track);
    if (selectedNoteIdx >= 0) {
        noteEditManager.syncReferenceStepFromBracketTick(bracketTick);
    }
}

void EditManager::applyUndoRedoLanding(Track& track) {
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;

    uint32_t bracket = bracketTick;
    NoteId primaryNote = kInvalidNoteId;

    const NoteEditFocus& focus = editSession.focus;
    const auto notes = selectableDisplayNotesAtEditSelect(track);
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        const uint32_t loopLength = noteEditLoopLengthTicks(track);
        const uint32_t bracketDisplay =
            (focus.last.startTick >= loopLength && loopLength > 0)
                ? (focus.last.startTick % loopLength)
                : focus.last.startTick;
        const int displayIdx = filteredDisplayNoteIndexForNoteIdAndStart(
            notes, focus.movingNoteId, bracketDisplay);
        if (displayIdx >= 0 && displayIdx < static_cast<int>(notes.size())) {
            const DisplayNote& dn = notes[static_cast<size_t>(displayIdx)];
            primaryNote = dn.noteId;
            bracket = dn.startTick % loopLength;
        }
    } else if (!notes.empty()) {
        selectClosestNote(track, bracketTick);
        if (selectedNoteIdx >= 0) {
            const DisplayNote& dn = notes[static_cast<size_t>(selectedNoteIdx)];
            primaryNote = dn.noteId;
            bracket = dn.startTick % noteEditLoopLengthTicks(track);
        }
    }

    noteEditManager.resetLengthEditingModeOnNoteSelect();
    applySelectNav(track, bracket, primaryNote);
    if (primaryNote != kInvalidNoteId) {
        noteEditManager.scheduleNoteSelectFaderSync(track);
    }
}

bool EditManager::sessionUndo(Track& track) {
    if (!editSession.active || !editSession.undoStack.canUndo()) {
        return false;
    }
    if (editSession.store.isFlatDirty()) {
        editSession.store.syncFlatToStore();
    }
    SessionUndoEntry redoPayload =
        buildSessionUndoEntry(editSession.focus, sessionState.selection,
                              editSession.store.readFlat(), track.getMidiChannel(),
                              noteEditLoopLengthTicks(track), editSession.editPassIds);
    SessionUndoEntry* entry = editSession.undoStack.popUndoTarget();
    if (entry == nullptr) {
        return false;
    }
    const bool liveCaptureEntry =
        entry->hasRedoPayload && entry->editRows.empty() && !entry->redoEditRows.empty();
    if (liveCaptureEntry) {
        entry->redoFocus = editSession.focus;
        entry->redoSelection = sessionState.selection;
        entry->redoEditPassIds = editSession.editPassIds;
    } else {
        entry->redoEditPassIds = editSession.editPassIds;
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

bool EditManager::sessionRedo(Track& track) {
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
    applySessionRedoEntry(loop, editSession.store, *entry, noteEditLoopLengthTicks(track),
                          editSession.editPassIds);
    editSession.focus = entry->redoFocus;
    sessionState.selection = entry->redoSelection;
    editSession.undoStack.advanceRedoCursor();
    editSession.replaceEditPassOnClose = true;
    track.invalidateCaches();
    applyUndoRedoLanding(track);
    return true;
}

bool EditManager::isSessionUndoDisplayActive() const {
    return editSession.active && editSession.sessionType == EditSessionType::Note;
}

MidiEventVec& EditManager::sessionMidiEvents() {
    return editSession.store.mutFlat();
}

const MidiEventVec& EditManager::sessionMidiEvents() const {
    return editSession.store.readFlat();
}

void EditManager::bumpSessionPreviewRevision() {
    ++sessionPreviewRevision_;
}

MidiEventVec& EditManager::editMidiEvents(Track& track) {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.getMidiEvents();
}

const MidiEventVec& EditManager::editMidiEvents(const Track& track) const {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.getMidiEvents();
}

EditManager::EditManager() {
    currentState = nullptr; // Start with no state
}

void EditManager::setState(EditNoteState* newState, Track& track, uint32_t startTick) {
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

void EditManager::onEncoderTurn(Track& track, int delta) {
    if (currentState) {
        if (isGeometryEditKind(sessionState.kind)) {
            pushSessionUndoOnKindChange(track, sessionState.kind);
        }
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
    const auto notes = noteEditManager.selectableDisplayNotesForEditUi(track);
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (notes.empty() || loopLength == 0) {
        bracketTick = loopLength > 0 ? startTick % loopLength : 0;
        applySelectNav(track, bracketTick, kInvalidNoteId);
        hasMovedBracket = true;
        return;
    }
    const uint32_t modStart = startTick % loopLength;
    uint32_t bestDist = loopLength;
    int bestIdx = 0;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const uint32_t noteTick = notes[static_cast<size_t>(i)].startTick % loopLength;
        const uint32_t dist =
            std::min((noteTick + loopLength - modStart) % loopLength,
                     (modStart + loopLength - noteTick) % loopLength);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    const DisplayNote& dn = notes[static_cast<size_t>(bestIdx)];
    bracketTick = dn.startTick % loopLength;
    applySelectNav(track, bracketTick, dn.noteId);
    hasMovedBracket = true;
}

void EditManager::selectNoteAtBracket(Track& track, uint32_t startTick) {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        selectClosestNote(track, startTick);
        return;
    }
    const uint32_t bracket = startTick % loopLength;
    const auto notes = noteEditManager.selectableDisplayNotesForEditUi(track);
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        if (notes[static_cast<size_t>(i)].startTick % loopLength == bracket) {
            const DisplayNote& dn = notes[static_cast<size_t>(i)];
            bracketTick = bracket;
            applySelectNav(track, bracketTick, dn.noteId);
            hasMovedBracket = true;
            return;
        }
    }
    selectClosestNote(track, startTick);
}

void EditManager::moveBracket(Track& track, int delta) {
    moveBracket(delta, track, Config::TICKS_PER_16TH_STEP);
}

void EditManager::stepSelectNavSlot(Track& track, int delta) {
    if (!ValidationUtils::validateLoopLength(noteEditLoopLengthTicks(track)) || delta == 0) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        noteEditManager.buildSelectNavigationSlots(track, bracketTick, true);
    if (slots.empty()) {
        return;
    }

    const EditorSelection& sel = sessionState.selection;
    int slotIdx = SelectNavigation::findSlotIndexForNoteId(
        slots, noteEditManager.selectableDisplayNotesForEditUi(track), sel.primaryNote,
        bracketTick, loopStartTick, loopLength);
    if (slotIdx < 0) {
        slotIdx = 0;
    }

    const int count = static_cast<int>(slots.size());
    slotIdx = (slotIdx + delta) % count;
    if (slotIdx < 0) {
        slotIdx += count;
    }

    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(slotIdx)];
    const uint32_t absoluteBracket =
        SelectNavigation::noteStorageTick(slot.relativeTick, loopStartTick, loopLength);
    const auto notes = noteEditManager.selectableDisplayNotesForEditUi(track);
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
    if (!editSession.active) {
        openNoteEditSession(track);
    }
    setState(newState, track, startTick);
}

void EditManager::exitEditMode(Track& track) {
    noteEditManager.resetLengthEditingModeOnSessionBoundary();
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

void EditManager::moveBracket(int delta, const Track& track, uint32_t ticksPerStep) {
    Track& mutableTrack = const_cast<Track&>(track);
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }
    const auto notes = noteEditManager.selectableDisplayNotesForEditUi(track);

    const uint32_t SNAP_WINDOW = 24;
    if (delta > 0) {
        const uint32_t targetTick = (bracketTick + ticksPerStep) % loopLength;
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            const uint32_t noteTick = notes[static_cast<size_t>(i)].startTick % loopLength;
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
            applySelectNav(mutableTrack, dn.startTick % loopLength, dn.noteId);
        } else {
            applySelectNav(mutableTrack, targetTick, kInvalidNoteId);
        }
    } else if (delta < 0) {
        const uint32_t targetTick =
            (bracketTick + loopLength - (ticksPerStep % loopLength)) % loopLength;
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            const uint32_t noteTick = notes[static_cast<size_t>(i)].startTick % loopLength;
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
            applySelectNav(mutableTrack, dn.startTick % loopLength, dn.noteId);
        } else {
            applySelectNav(mutableTrack, targetTick, kInvalidNoteId);
        }
    }
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
void EditManager::cycleNoteEditType(Track& track) {
    if (!editSession.active) {
        openNoteEditSession(track);
    }
    applyCycleEditKind(track);
    logger.log(CAT_TRACK, LOG_DEBUG, "Note edit type cycled to kind=%d",
               static_cast<int>(sessionState.kind));
}

void EditManager::sendEditModeProgram(EditModeState mode) {
    // Note edit kind (select/move/length/pitch) does not use ch16 program change.
    // Session type (loop vs note) is sendEditSessionChange only: PC 0 = loop, PC 1 = note.
    logger.log(CAT_MIDI, LOG_DEBUG, "Note edit kind=%d (no program change)", mode);
}

void EditManager::persistActiveNoteEditSession(Track& track) {
    if (!editSession.active) {
        return;
    }
    syncNoteEditFocusLastFromSessionStore(track);
    commitAllPendingNoteEditActions(track);
    bakeNoteEditSessionStoreToPasses(track);
}

void EditManager::cycleEditSession(Track& track) {
    if (editSession.sessionType == EditSessionType::Note) {
        noteEditManager.resetLengthEditingModeOnSessionBoundary();
        syncNoteEditFocusLastFromSessionStore(track);
        trackManager.reclaimUnreferencedDisabledPasses();
        commitAllPendingNoteEditActions(track);
        editSession.replaceEditPassOnClose = true;
        closeNoteEditSession(track);
        track.invalidateCaches();
    } else {
        editSession.sessionType = EditSessionType::Note;
    }
    sendEditSessionChange(editSession.sessionType);
    logger.log(CAT_TRACK, LOG_DEBUG, "Edit session cycled to: %d",
               static_cast<int>(editSession.sessionType));
}

void EditManager::sendEditSessionChange(EditSessionType sessionType) {
    editSession.sessionType = sessionType;

    uint8_t program = MidiConfig::SessionProgram::LOOP_EDIT;
    uint8_t triggerNote = 0;
    const char* modeName = "LOOP_EDIT";

    switch (sessionType) {
        case EditSessionType::Loop:
            program = MidiConfig::SessionProgram::LOOP_EDIT;
            triggerNote = 100;
            modeName = "LOOP_EDIT";
            break;
        case EditSessionType::Note:
            program = MidiConfig::SessionProgram::NOTE_EDIT;
            triggerNote = 0;
            modeName = "NOTE_EDIT";
            break;
        case EditSessionType::ControlChange:
            return;
    }

    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL, program);
    midiHandler.sendLedFeedbackNoteOn(triggerNote, 64);
    delay(10);
    midiHandler.sendLedFeedbackNoteOff(triggerNote);
    logger.log(CAT_MIDI, LOG_INFO, "Edit session: %s (Program %d, Note %d trigger)",
               modeName, program, triggerNote);

    if (sessionType == EditSessionType::Note) {
        noteEditManager.loopEditManager.onLeaveLoopEditSession();
        Track& track = trackManager.getSelectedTrack();
        reopenNoteEditSession(track);
    }
    if (sessionType == EditSessionType::Loop) {
        noteEditManager.loopEditManager.onEnterLoopEditSession(
            trackManager.getSelectedTrack());
    }
}

void EditManager::commitEditSessionOnDepart(Track& track) {
    if (isLoopEditSession()) {
        noteEditManager.loopEditManager.commitLoopEditOnDepart(track);
    }
    if (editSession.active) {
        persistActiveNoteEditSession(track);
    }
    if (currentState) {
        currentState->onExit(*this, track);
        currentState = nullptr;
    }
}

void EditManager::reenterEditSessionForFocusChange(Track& track, uint8_t /*previousSlot*/) {
    switch (editSession.sessionType) {
        case EditSessionType::Loop:
            noteEditManager.loopEditManager.reopenLoopEditSession(track);
            logger.log(CAT_TRACK, LOG_DEBUG, "LOOP_EDIT session refreshed for focus change");
            break;
        case EditSessionType::Note:
            if (editSession.active) {
                reopenNoteEditSession(track);
                noteEditManager.sendNoteEditSessionFaderFeedback(track);
                logger.log(CAT_TRACK, LOG_DEBUG, "NOTE_EDIT session reopened for focus change");
            }
            break;
        case EditSessionType::ControlChange:
            logger.log(CAT_TRACK, LOG_DEBUG, "ControlChange edit focus change (stub)");
            break;
    }
}

void EditManager::beforeSelectedTrackChange(Track& departingTrack) {
    commitEditSessionOnDepart(departingTrack);
}

void EditManager::onTrackChanged(Track& newTrack) {
    currentEditMode = EDIT_MODE_NONE;
    selectedNoteIdx = -1;
    hasMovedBracket = false;

    reenterEditSessionForFocusChange(newTrack, 255);

    logger.log(CAT_TRACK, LOG_DEBUG, "Edit state reset for new track");
}

void EditManager::beforeSelectedSlotChange(Track& track) {
    commitEditSessionOnDepart(track);
}

void EditManager::onSelectedSlotChanged(Track& track, uint8_t previousSlot) {
    reenterEditSessionForFocusChange(track, previousSlot);
}

size_t EditManager::getDisplayUndoCount(const Track& track) const {
    if (isSessionUndoDisplayActive()) {
        return editSession.undoStack.undoCount();
    }
    return TrackUndo::getUndoCount(track);
}

void EditManager::setSelectedNoteIdx(int idx) {
    if (selectedNoteIdx != idx) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection changed: %d -> %d", selectedNoteIdx, idx);
    }
    selectedNoteIdx = idx;
}

void EditManager::setLastFader1SelectNoteId(NoteId noteId) {
    lastFader1SelectNoteId = noteId;
}

void EditManager::clearLastFader1SelectNoteId() {
    lastFader1SelectNoteId = kInvalidNoteId;
}

void EditManager::resetSelection() {
    if (selectedNoteIdx != -1) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection reset: %d -> -1", selectedNoteIdx);
    }
    selectedNoteIdx = -1;
    clearLastFader1SelectNoteId();
}

