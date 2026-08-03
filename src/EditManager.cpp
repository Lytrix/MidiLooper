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
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SelectNavigation.h"
#include "TickPhase.h"
#include "Utils/ValidationUtils.h"
#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
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
                                const SessionMidiEventVec& flat,
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
    flushDeferredNoteEditDisplayRefresh(track);
    if (!editSession.active || !editSession.focus.active) {
        return;
    }
    if (!noteEditFocusHasPendingCommit(editSession.focus)) {
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

    // commitBaseline / baselineMap come from committed passes materialize, not the live
    // session preview — otherwise a pending length preview (e.g. end 680) becomes baseline
    // on fader-1 reselect and the next ChangeLength commit is a no-op on rematerialize.
    MidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector( loopMidiEventsFromPasses, loopLength);
    rebuildNoteEditFocusFromStore(editSession.focus, loopMidiEventsFromPasses, channel,
                                  loopLength, selectedNoteIdx);
    populateBaselineMapForEditClosure(editSession.focus, loopMidiEventsFromPasses,
                                      sessionMidiEvents(), channel, loopLength);

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
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    const MidiEventVec& committedLoopEvents = materializedLoopEventsForNoteEditFocus(track);
    rebuildNoteEditFocusFromStore(editSession.focus, committedLoopEvents, channel,
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
    if (baselineNoteId != kInvalidNoteId) {
        editSession.focus.baselineMap[baselineNoteId] = editSession.focus.commitBaseline;
    }
    populateBaselineMapForEditClosure(editSession.focus, committedLoopEvents,
                                      sessionEvents, channel, loopLength);
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
        selectableDisplayNotesForEditUi(track);

    if (!editorSelectionHasNote(sessionState.selection)) {
        if (selectedNoteIdx >= 0) {
            setSelectedNoteIdx(-1);
        }
        return;
    }

    const bool geometryMutationKind =
        sessionState.kind == NoteEditKind::Move ||
        sessionState.kind == NoteEditKind::Pitch ||
        sessionState.kind == NoteEditKind::Length;
    int matchIdx = -1;
    if (geometryMutationKind && editSession.focus.active &&
        editSession.focus.movingNoteId == sessionState.selection.primaryNote &&
        editorSelectionHasNote(sessionState.selection)) {
        const NoteEditFocus& focus = editSession.focus;
        const bool lengthBracket = sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
        const uint32_t storageBracketTick =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t displayBracket = NoteEditDisplaySnapshot::displayStartTickFromStorage(
            storageBracketTick, noteEditLoopStartTick(track), loopLength);
        matchIdx = filteredDisplayNoteIndexForNoteIdAndStart(
            filtered, focus.movingNoteId, displayBracket, noteEditLoopStartTick(track),
            loopLength);
    }
    if (matchIdx < 0) {
        matchIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
            sessionState.selection, filtered, noteEditLoopStartTick(track),
            noteEditLoopLengthTicks(track),
            sessionState.kind == NoteEditKind::Length || isLengthEditingMode());
    }
    if (matchIdx < 0 && editorSelectionHasNote(sessionState.selection)) {
        const NoteEditFocus& focus = editSession.focus;
        const uint32_t loopStartTick = noteEditLoopStartTick(track);
        if (focus.active && focus.movingNoteId == sessionState.selection.primaryNote) {
            const bool lengthBracket =
                sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
            const bool geometryMutationKind =
                sessionState.kind == NoteEditKind::Move ||
                sessionState.kind == NoteEditKind::Pitch ||
                sessionState.kind == NoteEditKind::Length;
            const uint32_t storageBracketTick =
                lengthBracket ? focus.last.endTick : focus.last.startTick;
            const uint32_t correctedTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
                storageBracketTick, loopStartTick, loopLength);
            if (!geometryMutationKind &&
                sessionState.selection.selectedTick != correctedTick) {
                sessionState.selection.selectedTick = correctedTick;
                selectedTick = correctedTick;
            }
            matchIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                sessionState.selection, filtered, loopStartTick, loopLength, lengthBracket);
        }
    }
    if (matchIdx < 0) {
        if (editorSelectionHasNote(sessionState.selection)) {
            bool noteStillPresent = false;
            int noteIdOnlyIdx = -1;
            for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
                if (filtered[static_cast<size_t>(i)].noteId == sessionState.selection.primaryNote) {
                    noteStillPresent = true;
                    noteIdOnlyIdx = i;
                    break;
                }
            }
            if (!noteStillPresent) {
                if (editSession.focus.active &&
                    editSession.focus.movingNoteId == sessionState.selection.primaryNote &&
                    (sessionState.kind == NoteEditKind::Move ||
                     sessionState.kind == NoteEditKind::Pitch ||
                     sessionState.kind == NoteEditKind::Length)) {
                    return;
                }
                setSelectedNoteIdx(-1);
            } else if (noteIdOnlyIdx >= 0 && editSession.focus.active &&
                       editSession.focus.movingNoteId == sessionState.selection.primaryNote) {
                setSelectedNoteIdx(noteIdOnlyIdx);
            }
        } else if (selectedNoteIdx >= 0) {
            setSelectedNoteIdx(-1);
        }
    } else if (matchIdx != selectedNoteIdx) {
        setSelectedNoteIdx(matchIdx);
    }
}

NoteUtils::DisplayNoteVec EditManager::selectableDisplayNotesAtEditSelect(const Track& track) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (isNoteEditActive()) {
        return filteredSelectableDisplayNotesForNoteEdit(track);
    }
    return track.getCachedNotes();
}

NoteUtils::DisplayNoteVec EditManager::filteredSelectableDisplayNotesForNoteEdit(
    const Track& track) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (!editSession.active || loopLength == 0) {
        return {};
    }
    const NoteEditFocus& focus = editSession.focus;
    const uint32_t previewRevision = sessionPreviewRevision_;
    const size_t overlapCount = focus.overlapNotes.size();
    if (previewRevision == noteEditSelectableDisplayCachePreviewRevision_ &&
        overlapCount == noteEditSelectableDisplayCacheOverlapCount_ &&
        loopLength == noteEditSelectableDisplayCacheLoopLength_ &&
        !noteEditSelectableDisplayCacheNotes_.empty()) {
        return noteEditSelectableDisplayCacheNotes_;
    }
    noteEditSelectableDisplayCachePreviewRevision_ = previewRevision;
    noteEditSelectableDisplayCacheOverlapCount_ = overlapCount;
    noteEditSelectableDisplayCacheLoopLength_ = loopLength;
    noteEditSelectableDisplayCacheNotes_ =
        filterSelectableDisplayNotes(track.editAwareMidiEvents(), focus, track.getMidiChannel(),
                                     loopLength);
    return noteEditSelectableDisplayCacheNotes_;
}

void EditManager::invalidateNoteEditDerivedCaches() {
    noteEditFocusMaterializeLoopRevision_ = UINT32_MAX;
    noteEditFocusMaterializeSlot_ = 255;
    noteEditFocusMaterializeLoopLength_ = 0;
    noteEditFocusMaterializedLoopEvents_.clear();
    noteEditSelectableDisplayCachePreviewRevision_ = UINT32_MAX;
    noteEditSelectableDisplayCacheOverlapCount_ = static_cast<size_t>(-1);
    noteEditSelectableDisplayCacheLoopLength_ = 0;
    noteEditSelectableDisplayCacheNotes_.clear();
}

const MidiEventVec& EditManager::materializedLoopEventsForNoteEditFocus(Track& track) {
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint8_t slot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const uint32_t revision = loop.playbackRevision;
    if (revision == noteEditFocusMaterializeLoopRevision_ && slot == noteEditFocusMaterializeSlot_ &&
        loopLength == noteEditFocusMaterializeLoopLength_ &&
        !noteEditFocusMaterializedLoopEvents_.empty()) {
        return noteEditFocusMaterializedLoopEvents_;
    }
    noteEditFocusMaterializeLoopRevision_ = revision;
    noteEditFocusMaterializeSlot_ = slot;
    noteEditFocusMaterializeLoopLength_ = loopLength;
    noteEditFocusMaterializedLoopEvents_.clear();
    loop.passes.materializeToEventVector(noteEditFocusMaterializedLoopEvents_, loopLength);
    return noteEditFocusMaterializedLoopEvents_;
}

DisplayNote EditManager::liveEditDisplayNoteAtSelect(const Track& track) const {
    if (isNoteEditActive() && editSession.focus.active) {
        const NoteBaseline& last = editSession.focus.last;
        return {editSession.focus.movingNoteId, last.pitch, last.velocity, last.startTick,
                last.endTick};
    }
    const int idx = getSelectedNoteIdx();
    const NoteUtils::DisplayNoteVec& notes = selectableDisplayNotesAtEditSelect(track);
    if (idx < 0 || idx >= static_cast<int>(notes.size())) {
        return {};
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

bool EditManager::isLengthBracketEditActive() const {
    return sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
}

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

#ifndef NOTE_EDIT_OPEN_BISECT_STAGE
#define NOTE_EDIT_OPEN_BISECT_STAGE 4
#endif

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
    emitSessionOpenedToSurface(false, true);
    DIAG_EVENT(Diagnostics::Edit::NoteEditOpenExit);
    logger.debug("EditSession opened editPass=0");
}

void EditManager::reopenNoteEditSession(Track& track) {
    if (editSession.active) {
        editSession.store.mutStore().clear();
        editSession.store.discardEventsCache();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.active = false;
        resetNoteEditSessionState();
        selectedNoteIdx = -1;
        hasMovedBracket = false;
    }
    editSession.sessionType = EditSessionType::Note;
    openNoteEditSession(track);
    DIAG_COUNTER_INC(CacheInvalidateBroad);
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

void EditManager::closeNoteEditSession(Track& track) {
    if (!editSession.active) {
        return;
    }
    flushDeferredNoteEditDisplayRefresh(track);
    closeNoteEditPass(track);
    editSession.store.mutStore().clear();
    editSession.store.discardEventsCache();
    editSession.focus.clear();
    editSession.active = false;
    editSession.sessionType = EditSessionType::Loop;
    editSession.editPassIndex = 0;
    editSession.replaceEditPassOnClose = false;
    clearLastFader1SelectNoteId();
    (void)track;
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

void EditManager::rematerializeNoteEditSessionAfterWorkspaceReload(Track& track) {
    if (editSession.sessionType == EditSessionType::Note) {
        reopenNoteEditSession(track);
        return;
    }
    if (editSession.active) {
        editSession.store.mutStore().clear();
        editSession.store.discardEventsCache();
        editSession.undoStack.clear();
        editSession.editPassIds.clear();
        editSession.active = false;
        resetNoteEditSessionState();
        selectedNoteIdx = -1;
        hasMovedBracket = false;
    }
    // Loaded workspace data is authoritative; live display cache already invalidated by caller.
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
    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }
    const MidiEventVec sessionSnapshot = editSession.store.readEvents();
    editSession.store.discardEventsCache();
    SessionMidiEventVec loopMidiEventsFromPasses;
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

    editSession.store.mutStore().loadFromEvents(loopMidiEventsFromPasses);
    editSession.store.discardEventsCache();
    logChangeLengthCommitTrace("session_store", editSession.store.readEvents(),
                               loopLength, homePitch, homeStart);

    logChangeLengthCommitTrace("loop_materialized", loop.midiEvents(), loopLength,
                               homePitch, homeStart);
    track.invalidateCaches();
    return lastId;
}

bool EditManager::pushSessionUndoOnKindChange(Track& track, NoteEditKind kind) {
    if (kind == NoteEditKind::Pitch && isLengthEditingMode()) {
        return true;
    }
    if (!shouldPushGeometryKindUndo(lastPushedGeometryKind_, kind)) {
        return true;
    }
    if (!editSession.active) {
        return true;
    }
    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }
    const SessionUndoEntry entry =
        buildSessionUndoEntry(editSession.focus, sessionState.selection,
                              editSession.store.readEvents(), track.getMidiChannel(),
                              noteEditLoopLengthTicks(track), editSession.editPassIds);
    if (!editSession.undoStack.pushEntry(entry)) {
        editSession.store.discardEventsCache();
        trackManager.reclaimUnreferencedDisabledPasses();
        if (!editSession.undoStack.pushEntry(entry)) {
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

    if (!editSession.undoStack.pushEntry(entry)) {
        editSession.store.discardEventsCache();
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
    lengthEditingMode_ = false;
    lengthFineAnchorEndTick_ = 0;
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;
    sessionPreviewRevision_ = 0;
    sessionPlaybackPreviewRevision_ = 0;
    deferredNoteEditDisplayRefreshPending_ = false;
    deferredNoteEditDisplayRefreshArmedAtMs_ = 0;
    invalidateNoteEditDerivedCaches();
}

void EditManager::applySelectionFromGeometryEdit(Track& track, uint32_t selectedTick,
                                                 NoteId primaryNote) {
    sessionState.selection.selectedTick = selectedTick;
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
    selectedTick = sessionState.selection.selectedTick;
    displayManager.requestNoteInfoRefresh(track);
}

void EditManager::applySelectNav(Track& track, uint32_t selectedTick, NoteId primaryNote,
                                 bool requestFaderSync, bool skipFader1Outbound) {
    (void)skipFader1Outbound;
    const EditorSelection priorSelection = sessionState.selection;
    sessionState.kind = NoteEditKind::Select;
    sessionState.selection.selectedTick = selectedTick;
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
        editorSelectionTargetChanged(priorSelection, selectedTick, primaryNote);
    if (selectionIdentityChanged && primaryNote != kInvalidNoteId) {
        selectionChangePrior_ = priorSelection;
        selectionChangeRequestFaderSync_ = requestFaderSync;
        emitEditEvent(EditEvent::SelectionChanged);
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
  const bool lengthBracket =
      sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
  if (editorSelectionHasNote(sessionState.selection)) {
    selectedNoteIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
        sessionState.selection, notes, noteEditLoopStartTick(track),
        noteEditLoopLengthTicks(track), lengthBracket);
  } else {
    selectedNoteIdx = -1;
  }
  selectedTick = sessionState.selection.selectedTick;
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
            currentState->onEnter(*this, track, selectedTick);
        }
    }
    sendEditModeProgram(mode);
}

void EditManager::enterDefaultNoteEditSessionState(Track& track, uint32_t transportTick) {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        selectedTick = 0;
        selectedNoteIdx = -1;
        clearLastFader1SelectNoteId();
        sessionState.kind = NoteEditKind::Select;
        sessionState.selection = {};
        return;
    }

    const Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t loopStartTick = noteEditLoopStartTick(track);
    const uint32_t playheadPhase =
        tickPhaseInLoop(transportTick, loop.startLoopTick, loopLength);
    selectedTick =
        SelectNavigation::noteRelativeTick(playheadPhase, loopStartTick, loopLength);
    selectNoteAtBracket(track, selectedTick);
    if (selectedNoteIdx < 0) {
        selectClosestNote(track, selectedTick);
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
    sessionState.selection.selectedTick = selectedTick;
    sessionState.selection.primaryNote = primaryNote;
    sessionState.selection.selectedNotes.clear();
    if (primaryNote != kInvalidNoteId) {
        sessionState.selection.selectedNotes.push_back(primaryNote);
    }
    sessionState.selection.trackId = static_cast<TrackId>(trackManager.getSelectedTrackIndex());
    sessionState.selection.loopId = trackManager.getSelectedLoop(track).loopId;
    syncNoteEditSessionStateToUi(track);
    if (selectedNoteIdx >= 0) {
        syncReferenceStepFromSelectedTick(selectedTick);
    }
}

void EditManager::applyUndoRedoLanding(Track& track) {
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;

    uint32_t bracket = selectedTick;
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
        selectClosestNote(track, selectedTick);
        if (selectedNoteIdx >= 0) {
            const DisplayNote& dn = notes[static_cast<size_t>(selectedNoteIdx)];
            primaryNote = dn.noteId;
            bracket = displayStartTickFromStorageNote(dn.startTick, noteEditLoopStartTick(track),
                                                      noteEditLoopLengthTicks(track));
        }
    }

    noteEditManager.resetLengthEditingModeOnNoteSelect();
    applySelectNav(track, bracket, primaryNote, primaryNote != kInvalidNoteId);
}

bool EditManager::sessionUndo(Track& track) {
    if (!editSession.active || !editSession.undoStack.canUndo()) {
        return false;
    }
    if (editSession.store.isEventsDirty()) {
        editSession.store.syncEventsToStore();
    }
    SessionUndoEntry redoPayload =
        buildSessionUndoEntry(editSession.focus, sessionState.selection,
                              editSession.store.readEvents(), track.getMidiChannel(),
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
    return editSession.store.mutEvents();
}

const MidiEventVec& EditManager::sessionMidiEvents() const {
    return editSession.store.readEvents();
}

void EditManager::bumpSessionPreviewRevision() {
    ++sessionPreviewRevision_;
}

void EditManager::bumpSessionPlaybackPreviewRevision() {
    ++sessionPlaybackPreviewRevision_;
}

void EditManager::scheduleDeferredNoteEditDisplayRefresh() {
    deferredNoteEditDisplayRefreshPending_ = true;
    deferredNoteEditDisplayRefreshArmedAtMs_ = millis();
}

void EditManager::flushDeferredNoteEditDisplayRefresh(Track& track) {
    if (!deferredNoteEditDisplayRefreshPending_) {
        return;
    }
    deferredNoteEditDisplayRefreshPending_ = false;
    bumpSessionPlaybackPreviewRevision();
    track.invalidateCaches(true);
#ifndef PIO_UNIT_TEST_NATIVE
    displayManager.requestNoteInfoRefresh(track);
#endif
}

void EditManager::processDeferredNoteEditDisplayRefresh(Track& track) {
    if (!deferredNoteEditDisplayRefreshPending_) {
        return;
    }
    const uint32_t now = millis();
    if (now - deferredNoteEditDisplayRefreshArmedAtMs_ < kDeferredNoteEditDisplayRefreshIdleMs) {
        return;
    }
    flushDeferredNoteEditDisplayRefresh(track);
}

MidiEventVec& EditManager::editMidiEvents(Track& track) {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.legacyMidiEventsFromCommitted();
}

const MidiEventVec& EditManager::editMidiEvents(const Track& track) const {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.legacyMidiEventsFromCommitted();
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

void EditManager::onButtonPress(Track& track) {
    if (!isNoteEditActive() || currentState == nullptr) {
        return;
    }
    currentState->onButtonPress(*this, track);
}

void EditManager::selectClosestNote(Track& track, uint32_t startTick) {
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

void EditManager::selectNoteAtBracket(Track& track, uint32_t startTick) {
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

void EditManager::moveBracket(Track& track, int delta) {
    moveBracket(delta, track, Config::TICKS_PER_16TH_STEP);
}

void EditManager::stepSelectNavSlot(Track& track, int delta) {
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

void EditManager::switchToNextState(Track& track) {
    // Example: cycle between noteState and startNoteState
    if (currentState == &noteHomeState) {
        setState(&startNoteState, track, selectedTick);
    } else {
        setState(&noteHomeState, track, selectedTick);
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

void EditManager::selectNextNote(const Track& track) {
    moveBracket(1, track, 1);
}

void EditManager::selectPrevNote(const Track& track) {
    moveBracket(-1, track, 1);
}

void EditManager::enterPitchEditMode(Track& track) {
    setState(&pitchNoteState, track, selectedTick);
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

void EditManager::emitSessionOpenedToSurface(bool includeMidi, bool includeNoteFaderFeedback) {
    sessionOpenedIncludesMidi_ = includeMidi;
    sessionOpenedIncludesFaderFeedback_ = includeNoteFaderFeedback;
    emitEditEvent(EditEvent::SessionOpened);
}

void EditManager::sendEditSessionChange(EditSessionType sessionType, bool notifySurfaceMidi) {
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
        emitSessionOpenedToSurface(true, false);
    } else if (sessionTypeChanged || notifySurfaceMidi) {
        emitSessionOpenedToSurface(sessionTypeChanged || notifySurfaceMidi, false);
    }
}

void EditManager::commitEditSessionOnDepart(Track& track) {
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

void EditManager::reenterEditSessionForFocusChange(Track& track, uint8_t /*previousSlot*/) {
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

size_t EditManager::getDisplayUndoCount(const Track& track, const Loop& loop) const {
    if (isSessionUndoDisplayActive()) {
        return editSession.undoStack.undoCount();
    }
    return TrackUndo::undoDepthForLoop(track, loop);
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

bool EditManager::deleteSelectedNote(Track& track,
                                     const NoteUtils::DisplayNoteVec& filteredNotes) {
    if (selectedNoteIdx < 0 && lastFader1SelectNoteId == kInvalidNoteId) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return false;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const NoteEditFocus& focus = editSession.focus;
    const EditorSelection& selection = sessionState.selection;

    int resolvedIdx = selectedNoteIdx;
    NoteId deleteTargetNoteId = kInvalidNoteId;
    if (editorSelectionHasNote(selection)) {
        deleteTargetNoteId = selection.primaryNote;
    } else if (lastFader1SelectNoteId != kInvalidNoteId) {
        deleteTargetNoteId = lastFader1SelectNoteId;
    }
    if (deleteTargetNoteId != kInvalidNoteId) {
        const uint32_t selectedTick = editorSelectionHasNote(selection) ? selection.selectedTick
                                                                        : UINT32_MAX;
        resolvedIdx =
            selectedTick != UINT32_MAX
                ? filteredDisplayNoteIndexForNoteIdAndStart(
                      filteredNotes, deleteTargetNoteId, selectedTick, noteEditLoopStartTick(track),
                      noteEditLoopLengthTicks(track))
                : -1;
    }
    if (resolvedIdx < 0 || resolvedIdx >= static_cast<int>(filteredNotes.size())) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return false;
    }
    if (deleteTargetNoteId == kInvalidNoteId) {
        deleteTargetNoteId = noteIdFromFilteredDisplayNote(filteredNotes, resolvedIdx);
    }

    const NoteUtils::DisplayNote selectedNote = filteredNotes[static_cast<size_t>(resolvedIdx)];

    const bool noteEditActive = editSession.active;
    beginGeometryMutation(track, NoteEditKind::Delete, false);
    const bool deleteTargetDiffersFromFocus =
        noteEditActive && focus.active && focus.movingNoteId != deleteTargetNoteId;
    if (deleteTargetDiffersFromFocus) {
        commitPendingOverlapNoteEdits(track);
        rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    } else if (noteEditActive && !focus.active) {
        rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    }
    commitAllPendingNoteEditActions(track);

    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t noteEnd = selectedNote.endTick;
    const NoteUtils::DisplayNoteVec notesAfter = selectableDisplayNotesAtEditSelect(track);
    const int refreshedIdx =
        editorSelectionHasNote(selection)
            ? filteredDisplayNoteIndexForNoteIdAndStart(
                  notesAfter, deleteTargetNoteId, selection.selectedTick,
                  noteEditLoopStartTick(track), noteEditLoopLengthTicks(track))
            : -1;
    if (refreshedIdx >= 0 && refreshedIdx < static_cast<int>(notesAfter.size())) {
        const NoteUtils::DisplayNote& refreshed = notesAfter[static_cast<size_t>(refreshedIdx)];
        notePitch = refreshed.note;
        noteStart = refreshed.startTick;
        noteEnd = refreshed.endTick;
    } else {
        for (const NoteUtils::DisplayNote& n : notesAfter) {
            if (n.noteId == deleteTargetNoteId) {
                notePitch = n.note;
                noteStart = n.startTick;
                noteEnd = n.endTick;
                break;
            }
        }
    }

    logger.info("MIDI Encoder: Deleting note noteId=%lu pitch=%d, start=%lu, end=%lu",
                static_cast<unsigned long>(deleteTargetNoteId), notePitch, noteStart, noteEnd);

    auto& midiEvents = track.editAwareMidiEvents();
    MidiEvent* noteOnEvent = nullptr;
    for (MidiEvent& e : midiEvents) {
        if (e.type == midi::NoteOn && e.data.noteData.velocity > 0 &&
            e.data.noteData.note == notePitch && e.tick == noteStart) {
            noteOnEvent = &e;
            break;
        }
    }

    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent != nullptr) {
        noteOffEvent = NoteMovementUtils::findCorrespondingNoteOff(midiEvents, noteOnEvent,
                                                                   notePitch, noteStart, noteEnd);
    }

    int deletedCount = 0;
    auto eraseByPointer = [&](MidiEvent* needle) {
        if (needle == nullptr) {
            return;
        }
        for (auto it = midiEvents.begin(); it != midiEvents.end(); ++it) {
            if (&(*it) == needle) {
                midiEvents.erase(it);
                ++deletedCount;
                return;
            }
        }
    };
    if (noteOnEvent != nullptr || noteOffEvent != nullptr) {
        eraseByPointer(noteOffEvent);
        eraseByPointer(noteOnEvent);
    } else {
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            const bool matchOn =
                (it->type == midi::NoteOn && it->data.noteData.velocity > 0 &&
                 it->data.noteData.note == notePitch && it->tick == noteStart);
            const bool matchOff =
                ((it->type == midi::NoteOff ||
                  (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                 it->data.noteData.note == notePitch && it->tick == noteEnd);
            if (matchOn || matchOff) {
                it = midiEvents.erase(it);
                ++deletedCount;
            } else {
                ++it;
            }
        }
    }

    logger.info("MIDI Encoder: Deleted %d MIDI events for note", deletedCount);

    EditPass del{};
    del.passType = EditPassType::Note;
    del.actionType = EditActionType::Delete;
    del.propertyType = EditPropertyType::None;
    del.targetNoteId = deleteTargetNoteId;
    commitEditAction(track, EditPassVec{del});
    track.invalidateCaches();

    setSelectedNoteIdx(-1);
    rebuildNoteEditFocusAtSelect(track, -1);

    logger.info("MIDI Encoder: Note deleted, maintaining current edit mode");
    emitEditEvent(EditEvent::GeometryChanged);
    return true;
}

bool EditManager::moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetTick) {
    const NoteEditFocus& focus = editSession.focus;
    uint32_t fromStart = currentNote.startTick;
    if (focus.active && focus.last.pitch == currentNote.note &&
        focus.last.startTick == currentNote.startTick) {
        fromStart = focus.last.startTick;
    }
    if (fromStart == targetTick) {
        return false;
    }
    if (!beginGeometryMutation(track, NoteEditKind::Move, true)) {
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note move aborted: session undo snapshot unavailable (heap reserve)");
        return false;
    }
    const int32_t tickDifference =
        static_cast<int32_t>(targetTick) - static_cast<int32_t>(fromStart);

    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note movement with overlap handling: from=%lu to=%lu difference=%ld overlapNotes=%zu",
               fromStart, targetTick, tickDifference, editSession.focus.overlapNotes.size());

    ensureNoteEditFocusForLiveEdit(track, currentNote);
    if (focus.active) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Overlap move bridge: pitch=%d, start=%lu, end=%lu",
                   focus.last.pitch, static_cast<unsigned long>(focus.last.startTick),
                   static_cast<unsigned long>(focus.last.endTick));
    }

    uint32_t dummyStart = currentNote.startTick;
    uint32_t dummyEnd = currentNote.endTick;
    return NoteMovementUtils::applyNoteEditChange(
        track, *this, NoteMovementUtils::NoteEditChangeKind::Move, currentNote, targetTick,
        static_cast<int>(tickDifference), 0, 0, 0, dummyStart, dummyEnd);
}

bool EditManager::changeNoteEndWithOverlapHandling(Track& track,
                                                   const NoteUtils::DisplayNote& currentNote,
                                                   uint32_t targetEndTick) {
    const NoteEditFocus& focus = editSession.focus;
    const uint32_t currentEnd = focus.active ? focus.last.endTick : currentNote.endTick;
    if (currentEnd == targetEndTick) {
        return false;
    }
    if (!beginGeometryMutation(track, NoteEditKind::Length, true)) {
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note length change aborted: session undo snapshot unavailable (heap reserve)");
        return false;
    }
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note length change with overlap handling: pitch=%d, start=%lu, end %lu->%lu",
               currentNote.note, currentNote.startTick, currentNote.endTick, targetEndTick);
    NoteMovementUtils::changeLengthWithOverlapHandling(track, *this, currentNote, targetEndTick);
    return true;
}

std::vector<NoteUtils::DisplayNote> EditManager::selectableDisplayNotesForEditUi(
    const Track& track) const {
    const uint8_t trackIndex = trackManager.getSelectedTrackIndex();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackIndex);
    const uint32_t loopLength = isNoteEditActive() ? noteEditLoopLengthTicks(track)
                                                   : track.getLoopLengthForSlot(displaySlot);
    NoteUtils::DisplayNoteVec notes;
    if (!isNoteEditActive() || loopLength == 0) {
        const auto& cachedNotes = track.getCachedNotes();
        notes.assign(cachedNotes.begin(), cachedNotes.end());
    } else {
        const NoteUtils::DisplayNoteVec filtered = filteredSelectableDisplayNotesForNoteEdit(track);
        notes.assign(filtered.begin(), filtered.end());
    }

    if (loopLength > 0) {
        const uint32_t currentTick = clockManager.getCurrentTick();
        const DetailedWindowContext window =
            displayManager.resolveDetailedWindow(track, displaySlot, currentTick);
        if (window.active) {
            notes = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, window.window,
                                                                            loopLength);
        }
    }
    return std::vector<NoteUtils::DisplayNote>(notes.begin(), notes.end());
}

std::vector<SelectNavigation::SelectNavSlot> EditManager::buildSelectNavigationSlots(
    const Track& track, uint32_t selectedTick, bool includeSelectedTickIfMissing) const {
    const uint32_t loopLength = isNoteEditActive() ? noteEditLoopLengthTicks(track)
                                                   : track.getLoopLength();
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    return SelectNavigation::buildSelectNavigationSlots(
        loopLength, noteEditLoopStartTick(track), notes, selectedTick, includeSelectedTickIfMissing);
}

void EditManager::syncReferenceStepFromSelectedTick(uint32_t selectedTick) {
    referenceStep_ = selectedTick / Config::TICKS_PER_16TH_STEP;
}

void EditManager::emitEditEvent(EditEvent event) {
    if (editEventListener_ != nullptr) {
        editEventListener_->onEditEvent(event);
    }
}

void EditManager::clearLengthEditingMode(bool emitEvent) {
    if (!lengthEditingMode_) {
        return;
    }
    lengthEditingMode_ = false;
    if (emitEvent) {
        emitEditEvent(EditEvent::LengthModeChanged);
    }
}

void EditManager::clearLengthEditingModeOnNoteSelect() {
    clearLengthEditingMode(false);
}

void EditManager::toggleLengthEditMode(Track& track) {
    const bool enabling = !lengthEditingMode_;
    lengthEditingMode_ = enabling;

    if (lengthEditingMode_) {
        logger.info("[MIDI] Length editing mode ENABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE END position (length editing)");
        syncNoteEditFocusLastFromSessionStore(track);
        const uint32_t loopLength = track.getLoopLength();
        if (getSelectedNoteIdx() >= 0 && loopLength > 0) {
            const NoteUtils::DisplayNote liveNote = liveEditDisplayNoteAtSelect(track);
            const uint32_t relEnd = liveNote.endTick % loopLength;
            setSelectedTick(relEnd);
            lengthFineAnchorEndTick_ = relEnd;
            setReferenceStep(relEnd / Config::TICKS_PER_16TH_STEP);
            beginGeometryMutation(track, NoteEditKind::Length, false);
        }
    } else {
        logger.info("[MIDI] Length editing mode DISABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE START position (position editing)");
        commitAllPendingNoteEditActions(track);
        syncNoteEditFocusLastFromSessionStore(track);
        const NoteUtils::DisplayNote liveNote = liveEditDisplayNoteAtSelect(track);
        const uint32_t loopLength = track.getLoopLength();
        if (loopLength > 0) {
            const uint32_t relStart = liveNote.startTick % loopLength;
            setSelectedTick(relStart);
            setReferenceStep(relStart / Config::TICKS_PER_16TH_STEP);
        }
        if (getSelectedNoteIdx() >= 0) {
            beginGeometryMutation(track, NoteEditKind::Move, false);
        }
    }
    emitEditEvent(EditEvent::LengthModeChanged);
}

