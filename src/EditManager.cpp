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
#include "ClockManager.h"
#include <map>
#include <vector>
#include <cmath>

using DisplayNote = NoteUtils::DisplayNote;

namespace {

void markOverlapDeleteRowsEmitted(NoteEditFocus& focus, const EditPassVec& rows) {
    for (const EditPass& row : rows) {
        if (row.actionType != EditActionType::Delete) {
            continue;
        }
        if (OverlapNote* entry = findOverlapNoteEntry(focus, row.target)) {
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
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    MidiEventVec& sessionStoreEvents = sessionMidiEvents();
    resolveOverlapNotesForPreCommit(sessionStoreEvents, editSession.focus, channel, loopLength);

    EditPassVec rows = buildPreCommitEditPasses(editSession.focus, channel);
    if (rows.empty()) {
        return;
    }

    for (const EditPass& row : rows) {
        if (row.actionType == EditActionType::Update &&
            row.propertyType == EditPropertyType::Length) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "Edit committed ChangeLength start=%lu baselineEnd=%lu newEnd=%lu",
                       static_cast<unsigned long>(row.target.startTick),
                       static_cast<unsigned long>(row.target.endTick),
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
    const uint32_t loopLength = track.getLoopLength();
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
    Loop& loop = track.getActiveLoop();
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = track.getLoopLength();

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
        editSession.focus.last = {live.note, live.velocity, live.startTick, live.endTick};
        if (editSession.focus.last.endTick > editSession.focus.commitBaseline.endTick) {
            editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
        }
    }
}

void EditManager::rebuildNoteEditFocusForDisplayNote(Track& track,
                                                     const DisplayNote& liveSelected) {
    if (!editSession.active) {
        editSession.focus.clear();
        return;
    }
    Loop& loop = track.getActiveLoop();
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    MidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector( loopMidiEventsFromPasses, loopLength);
    rebuildNoteEditFocusFromStore(editSession.focus, loopMidiEventsFromPasses, channel,
                                  loopLength, -1);

    const NoteRef baselineRef =
        findBaselineRefForNote(editSession.focus, channel, liveSelected.note,
                               liveSelected.startTick, liveSelected.endTick);
    editSession.focus.moving = baselineRef;
    const auto baselineIt = editSession.focus.baselineMap.find(baselineRef);
    if (baselineIt != editSession.focus.baselineMap.end()) {
        editSession.focus.commitBaseline = baselineIt->second;
    } else {
        editSession.focus.commitBaseline = {liveSelected.note, liveSelected.velocity,
                                                liveSelected.startTick, liveSelected.endTick};
    }
    editSession.focus.last = {liveSelected.note, liveSelected.velocity, liveSelected.startTick,
                                  liveSelected.endTick};
    editSession.focus.movingNoteRange.start = liveSelected.startTick;
    editSession.focus.movingNoteRange.end = liveSelected.endTick;
    if (editSession.focus.last.endTick > editSession.focus.commitBaseline.endTick) {
        editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
    }
    editSession.focus.active = true;
}

void EditManager::syncSelectedNoteIdxToFilteredInventory(Track& track) {
    if (!editSession.active || selectedNoteIdx < 0) {
        return;
    }
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    const std::vector<DisplayNote> filtered = filterSelectableDisplayNotes(
        sessionMidiEvents(), editSession.focus, track.getMidiChannel(), loopLength);

    if (!editSession.focus.active) {
        if (selectedNoteIdx >= static_cast<int>(filtered.size())) {
            setSelectedNoteIdx(-1);
        }
        return;
    }

    const NoteBaseline& last = editSession.focus.last;
    int matchIdx = -1;
    for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
        const DisplayNote& dn = filtered[static_cast<size_t>(i)];
        if (dn.note == last.pitch && dn.startTick == last.startTick) {
            matchIdx = i;
            break;
        }
    }
    if (matchIdx < 0) {
        setSelectedNoteIdx(-1);
    } else if (matchIdx != selectedNoteIdx) {
        setSelectedNoteIdx(matchIdx);
    }
}

std::vector<DisplayNote> EditManager::selectableDisplayNotesAtEditSelect(const Track& track) const {
    const uint32_t loopLength = track.getLoopLength();
    if (isNoteEditActive()) {
        return filterSelectableDisplayNotes(track.editAwareMidiEvents(), editSession.focus,
                                            track.getMidiChannel(), loopLength);
    }
    const auto& cachedNotes = track.getCachedNotes();
    return std::vector<DisplayNote>(cachedNotes.begin(), cachedNotes.end());
}

DisplayNote EditManager::liveEditDisplayNoteAtSelect(const Track& track) const {
    const int idx = getSelectedNoteIdx();
    const std::vector<DisplayNote> notes = selectableDisplayNotesAtEditSelect(track);
    if (idx < 0 || idx >= static_cast<int>(notes.size())) {
        return {};
    }
    if (isNoteEditActive() && editSession.focus.active) {
        const NoteBaseline& last = editSession.focus.last;
        return {last.pitch, last.velocity, last.startTick, last.endTick};
    }
    return notes[static_cast<size_t>(idx)];
}

EditManager editManager;

void EditManager::openNoteEditSession(Track& track) {
    if (editSession.active) {
        return;
    }
    Loop& loop = track.getActiveLoop();
    editSession.sessionType = EditSessionType::Note;
    editSession.active = true;
    editSession.editPassIndex = 0;
    editSession.editPassIds.clear();
    editSession.replaceEditPassOnClose = false;
    editSession.undoStack.clear();
    loop.rematerializeEditView(editSession.store.mutStore());
    editSession.store.discardFlatCache();
    resetNoteEditSessionState();
    enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
    logger.debug("EditSession opened editPass=0");
}

void EditManager::closeNoteEditPass(Track& track) {
    if (!editSession.active) {
        return;
    }
    Loop& loop = track.getActiveLoop();
    if (editSession.replaceEditPassOnClose) {
        if (editSession.store.isFlatDirty()) {
            editSession.store.syncFlatToStore();
        }

        MidiEventVec baselineStoreEvents;
        materializePassesExcludingEditPasses(loop, editSession.editPassIds,
                                             baselineStoreEvents);
        EditPassVec replacementRows =
            buildSessionStoreEditPasses(baselineStoreEvents, editSession.store.readFlat(),
                                        track.getMidiChannel(), track.getLoopLength());

        if (editSession.editPassIds.empty()) {
            if (!replacementRows.empty()) {
                const EditPassType passType = passTypeForSession(editSession.sessionType);
                for (EditPass& row : replacementRows) {
                    const EditPassId id =
                        loop.saveNoteEditPass(editSession.editPassIndex, std::move(row), passType);
                    if (id != kInvalidEditPassId) {
                        editSession.editPassIds.push_back(id);
                    }
                }
                track.invalidateCaches();
                logger.log(CAT_TRACK, LOG_INFO,
                           "NoteEditPass live capture baked editPass=%u rows=%u",
                           static_cast<unsigned>(editSession.editPassIndex),
                           static_cast<unsigned>(replacementRows.size()));
            }
        } else {
            const EditPassIdList staleEditPassIds = editSession.editPassIds;
            editSession.editPassIds.clear();

            if (replacementRows.empty()) {
                loop.replaceNoteEditPass(editSession.editPassIndex, staleEditPassIds, EditPassVec{});
                track.invalidateCaches();
            } else {
                const unsigned replacementRowCount =
                    static_cast<unsigned>(replacementRows.size());
                EditPassIdList replacementIds =
                    loop.replaceNoteEditPass(editSession.editPassIndex, staleEditPassIds,
                                             std::move(replacementRows));
                if (replacementIds.empty()) {
                    trackManager.reclaimUnreferencedDisabledPasses();
                    replacementRows =
                        buildSessionStoreEditPasses(baselineStoreEvents,
                                                    editSession.store.readFlat(),
                                                    track.getMidiChannel(),
                                                    track.getLoopLength());
                    replacementIds =
                        loop.replaceNoteEditPass(editSession.editPassIndex, staleEditPassIds,
                                                 std::move(replacementRows));
                }

                if (!replacementIds.empty()) {
                    for (const EditPassId id : replacementIds) {
                        editSession.editPassIds.push_back(id);
                    }
                    track.invalidateCaches();
                    logger.log(CAT_TRACK, LOG_INFO,
                               "NoteEditPass replaced editPass=%u stale=%u replacement=%u rows=%u",
                               static_cast<unsigned>(editSession.editPassIndex),
                               static_cast<unsigned>(staleEditPassIds.size()),
                               static_cast<unsigned>(replacementIds.front()),
                               replacementRowCount);
                } else {
                    editSession.editPassIds = staleEditPassIds;
                    logger.log(CAT_TRACK, LOG_WARNING,
                               "NoteEditPass replace rejected editPass=%u stale=%u rows=%u",
                               static_cast<unsigned>(editSession.editPassIndex),
                               static_cast<unsigned>(staleEditPassIds.size()),
                               replacementRowCount);
                }
            }
        }
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
    clearLastFader1SelectRef();
}

EditPassId EditManager::commitEditAction(Track& track, EditPassVec rows) {
    if (!editSession.active || rows.empty()) {
        return kInvalidEditPassId;
    }
    Loop& loop = track.getActiveLoop();
    const uint8_t homePitch = editSession.focus.commitBaseline.pitch;
    const uint32_t homeStart = editSession.focus.commitBaseline.startTick;
    const uint32_t loopLength = loop.loopLengthTicks;

    for (const EditPass& row : rows) {
        if (row.actionType == EditActionType::Update &&
            row.propertyType == EditPropertyType::Length) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "commitEditAction incoming ChangeLength refCh=%u note=%u "
                       "start=%lu baselineEnd=%lu newEnd=%lu",
                       row.target.channel, row.target.note,
                       static_cast<unsigned long>(row.target.startTick),
                       static_cast<unsigned long>(row.target.endTick),
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
                       "commitEditAction saved ChangeLength refCh=%u note=%u "
                       "start=%lu baselineEnd=%lu newEnd=%lu",
                       editPass.target.channel, editPass.target.note,
                       static_cast<unsigned long>(editPass.target.startTick),
                       static_cast<unsigned long>(editPass.target.endTick),
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

void EditManager::pushSessionUndoOnKindChange(Track& track, NoteEditKind kind) {
    if (!shouldPushGeometryKindUndo(lastPushedGeometryKind_, kind)) {
        return;
    }
    if (!editSession.active) {
        return;
    }
    if (editSession.store.isFlatDirty()) {
        editSession.store.syncFlatToStore();
    }
    const SessionUndoEntry entry =
        buildSessionUndoEntry(editSession.focus, sessionState.selection,
                              editSession.store.readFlat(), track.getMidiChannel(),
                              track.getLoopLength(), editSession.editPassIds);
    if (!editSession.undoStack.pushEntry(entry)) {
        logger.log(CAT_TRACK, LOG_WARNING,
                   "Session undo push rejected: heap below reserve (need=%u free=%u)",
                   static_cast<unsigned>(Config::HEAP_RESERVE_BYTES +
                                         estimatedSessionUndoEntryBytes(entry)),
                   static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
        return;
    }
    lastPushedGeometryKind_ = kind;
    (void)track;
}

void EditManager::foldLiveCaptureIntoNoteEditSession(Track& track, uint32_t closeTick) {
    if (!editSession.active) {
        return;
    }
    Loop& loop = track.getActiveLoop();
    const uint32_t loopLength = track.getLoopLength();
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
        logger.log(CAT_TRACK, LOG_WARNING,
                   "Session live capture undo push rejected: heap below reserve (need=%u free=%u)",
                   static_cast<unsigned>(Config::HEAP_RESERVE_BYTES +
                                         estimatedSessionUndoEntryBytes(entry)),
                   static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
        return;
    }
    editSession.replaceEditPassOnClose = true;
    track.invalidateCaches();
    logger.log(CAT_TRACK, LOG_INFO, "EditSession live capture folded rows=%u",
               static_cast<unsigned>(redoRows.size()));
}

void EditManager::restoreSessionUndoEntry(Track& track, const SessionUndoEntry& entry) {
    Loop& loop = track.getActiveLoop();
    applySessionUndoEntry(loop, editSession.store, entry, track.getLoopLength(),
                          editSession.editPassIds);
    editSession.focus = entry.focus;
    sessionState.selection = entry.selection;
}

void EditManager::applyGeometryKindFromControl(Track& track, NoteEditKind kind,
                                               bool fromFaderControl) {
    (void)track;
    sessionState.kind = kind;
    if (fromFaderControl && isGeometryEditKind(kind)) {
        encoderCycleNeedsAnchor_ = true;
    }
}

void EditManager::beginGeometryMutation(Track& track, NoteEditKind kind, bool fromFaderControl) {
    applyGeometryKindFromControl(track, kind, fromFaderControl);
    pushSessionUndoOnKindChange(track, kind);
}

void EditManager::resetNoteEditSessionState() {
    sessionState = {};
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;
}

void EditManager::applySelectNav(Track& track, int displayIdx, uint32_t bracket,
                                 const NoteRef& ref, bool hasNote) {
    const NoteEditSelection priorSelection = sessionState.selection;
    sessionState.kind = NoteEditKind::Select;
    sessionState.selection.displayIdx = displayIdx;
    sessionState.selection.bracketTick = bracket;
    sessionState.selection.hasNote = hasNote;
    sessionState.selection.ref = hasNote ? ref : NoteRef{};
    if (shouldResetGeometryKindUndoOnSelectChange(priorSelection, hasNote, ref)) {
        lastPushedGeometryKind_ = NoteEditKind::Select;
    }
    syncNoteEditSessionStateToUi(track);
}

void EditManager::applyCycleEditKind(Track& track) {
    sessionState.kind =
        resolveEncoderCyclePress(sessionState.kind, encoderCycleNeedsAnchor_);
    encoderCycleNeedsAnchor_ = false;
    syncNoteEditSessionStateToUi(track);
}

void EditManager::syncNoteEditSessionStateToUi(Track& track) {
    // Push authoritative sessionState into legacy UI fields (do not read legacy back in).
    const int prevSelectedIdx = selectedNoteIdx;
    selectedNoteIdx = sessionState.selection.displayIdx;
    bracketTick = sessionState.selection.bracketTick;
    if (sessionState.selection.hasNote) {
        if (sessionState.selection.ref.channel != 0) {
            setLastFader1SelectRef(sessionState.selection.ref);
        }
    } else {
        clearLastFader1SelectRef();
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
    if (sessionState.selection.hasNote) {
        noteEditManager.sendSelectnoteFaderUpdate(track);
    }
}

void EditManager::enterDefaultNoteEditSessionState(Track& track, uint32_t startTick) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        bracketTick = 0;
        selectedNoteIdx = -1;
        clearLastFader1SelectRef();
        sessionState.kind = NoteEditKind::Select;
        sessionState.selection = {};
        return;
    }

    bracketTick = startTick % loopLength;
    selectNoteAtBracket(track, startTick);
    if (selectedNoteIdx < 0) {
        selectClosestNote(track, startTick);
    }

    NoteRef ref{};
    if (selectedNoteIdx >= 0) {
        const auto notes = selectableDisplayNotesAtEditSelect(track);
        if (selectedNoteIdx < static_cast<int>(notes.size())) {
            ref = noteRefFromDisplay(track.getMidiChannel(),
                                     notes[static_cast<size_t>(selectedNoteIdx)]);
            setLastFader1SelectRef(ref);
        }
    } else {
        clearLastFader1SelectRef();
    }

    sessionState.kind = NoteEditKind::Select;
    sessionState.selection.displayIdx = selectedNoteIdx;
    sessionState.selection.bracketTick = bracketTick;
    sessionState.selection.hasNote = selectedNoteIdx >= 0;
    sessionState.selection.ref = ref;
    syncNoteEditSessionStateToUi(track);
}

void EditManager::applyUndoRedoLanding(Track& track) {
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;

    int displayIdx = -1;
    uint32_t bracket = bracketTick;
    NoteRef ref{};
    bool hasNote = false;

    const NoteEditFocus& focus = editSession.focus;
    const auto notes = selectableDisplayNotesAtEditSelect(track);
    if (focus.active) {
        displayIdx = filteredDisplayNoteIndexForNoteRef(track.getMidiChannel(), focus, notes,
                                                        focus.moving);
        if (displayIdx >= 0 && displayIdx < static_cast<int>(notes.size())) {
            const DisplayNote& dn = notes[static_cast<size_t>(displayIdx)];
            ref = noteRefFromDisplay(track.getMidiChannel(), dn);
            bracket = dn.startTick % track.getLoopLength();
            hasNote = true;
        }
    } else if (!notes.empty()) {
        selectClosestNote(track, bracketTick);
        displayIdx = selectedNoteIdx;
        if (displayIdx >= 0) {
            ref = noteRefFromDisplay(track.getMidiChannel(),
                                     notes[static_cast<size_t>(displayIdx)]);
            bracket = notes[static_cast<size_t>(displayIdx)].startTick % track.getLoopLength();
            hasNote = true;
        }
    }

    noteEditManager.resetLengthEditingModeOnNoteSelect();
    applySelectNav(track, displayIdx, bracket, ref, hasNote);
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
                              track.getLoopLength(), editSession.editPassIds);
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

    Loop& loop = track.getActiveLoop();
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
    Loop& loop = track.getActiveLoop();
    loop.enableEditPasses(entry->redoEditPassIds);
    editSession.editPassIds = entry->redoEditPassIds;
    applySessionRedoEntry(loop, editSession.store, *entry, track.getLoopLength(),
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
    // If entering a note-edit or pitch-edit state, record undo count to freeze display
    if (currentState == &startNoteState || currentState == &lengthNoteState || currentState == &pitchNoteState) {
        undoCountOnStateEnter = TrackUndo::getUndoCount(track);
    }
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
    if (!editSession.active) {
        openNoteEditSession(track);
    }
    setState(newState, track, startTick);
}

void EditManager::exitEditMode(Track& track) {
    noteEditManager.resetLengthEditingModeOnSessionBoundary();
    commitAllPendingNoteEditActions(track);

    closeNoteEditPass(track);
    if (track.getActiveLoop().isEditStateDirty()) {
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
void EditManager::cycleNoteEditType(Track& track) {
    if (!editSession.active) {
        openNoteEditSession(track);
    }
    applyCycleEditKind(track);
    logger.log(CAT_TRACK, LOG_DEBUG, "Note edit type cycled to kind=%d",
               static_cast<int>(sessionState.kind));
}

void EditManager::sendEditModeProgram(EditModeState mode) {
    // Send program change to indicate current edit mode
    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL, mode);
    logger.log(CAT_MIDI, LOG_DEBUG, "Sent edit mode program: %d", mode);
}

void EditManager::cycleEditSession(Track& track) {
    if (editSession.sessionType == EditSessionType::Note) {
        editSession.sessionType = EditSessionType::Loop;
    } else {
        editSession.sessionType = EditSessionType::Note;
        if (!editSession.active) {
            openNoteEditSession(track);
        } else {
            enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
        }
    }
    sendEditSessionChange(editSession.sessionType);
    logger.log(CAT_TRACK, LOG_DEBUG, "Edit session cycled to: %d",
               static_cast<int>(editSession.sessionType));
}

void EditManager::sendEditSessionChange(EditSessionType sessionType) {
    editSession.sessionType = sessionType;

    uint8_t program = 0;
    uint8_t triggerNote = 0;
    const char* modeName = "LOOP_EDIT";

    switch (sessionType) {
        case EditSessionType::Loop:
            program = 0;
            triggerNote = 100;
            modeName = "LOOP_EDIT";
            break;
        case EditSessionType::Note:
            program = 1;
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
        Track& track = trackManager.getSelectedTrack();
        if (!editSession.active) {
            openNoteEditSession(track);
        } else {
            enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
        }
    }
    if (sessionType == EditSessionType::Loop) {
        sendCurrentLoopLengthCC(trackManager.getSelectedTrack());
    }
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

void EditManager::setLastFader1SelectRef(const NoteRef& ref) {
    lastFader1SelectRef = ref;
}

void EditManager::clearLastFader1SelectRef() {
    lastFader1SelectRef = {};
}

void EditManager::resetSelection() {
    if (selectedNoteIdx != -1) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection reset: %d -> -1", selectedNoteIdx);
    }
    selectedNoteIdx = -1;
    clearLastFader1SelectRef();
}

