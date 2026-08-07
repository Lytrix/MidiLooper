//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <algorithm>

#include "EditManager.h"
#include "Globals.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "TrackManager.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteUtils.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

using DisplayNote = NoteUtils::DisplayNote;

EDIT_MANAGER_IMPL_MEM void EditManager::ensureNoteEditFocusForLiveEdit(Track& track,
                                                                       const DisplayNote& fallbackWhenNoFocus) {
    if (!editSession.active) {
        return;
    }
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const bool driverValid =
        editSession.focus.active &&
        isLiveEditDriverValidForTrack(track);
    if (driverValid) {
        return;
    }
    if (editSession.focus.active && !editorSelectionHasNote(sessionState.selection)) {
        return;
    }
    if (getSelectedNoteIdx() < 0) {
        return;
    }
    rebuildNoteEditFocusForDisplayNote(track, fallbackWhenNoFocus);
}

EDIT_MANAGER_IMPL_MEM void EditManager::cancelPendingDeleteForSelectNote(NoteId noteId) {
    if (noteId == kInvalidNoteId) {
        return;
    }
    EditPassVec& rows = editSession.applyOwnedEditPassRows;
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [noteId](const EditPass& row) {
                                  return row.targetNoteId == noteId &&
                                         row.actionType == EditActionType::Delete;
                              }),
               rows.end());
}

EDIT_MANAGER_IMPL_MEM void EditManager::rebuildNoteEditFocusAtSelect(Track& track, int selectedNoteIdx) {
    if (!editSession.active) {
        editSession.focus.clear();
        return;
    }
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);

    NoteIdList preservedChangedOverlapNoteIds;
    BaselineMap preservedOverlapBaselines;
    std::unordered_map<NoteId, OverlapNote, NoteIdHash> preservedOverlapNotes;
    const bool pendingMoverCommit = noteEditFocusHasPendingCommit(editSession.focus);
    const bool pendingOverlapDiff =
        noteEditFocusHasPendingBaselineMapDiff(editSession.focus, sessionMidiEvents(), channel,
                                             loopLength);
    if (selectedNoteIdx < 0 && !pendingMoverCommit && !pendingOverlapDiff) {
        preservedChangedOverlapNoteIds = editSession.focus.changedOverlapNoteIds;
        for (NoteId noteId : preservedChangedOverlapNoteIds) {
            const auto baselineIt = editSession.focus.baselineMap.find(noteId);
            if (baselineIt != editSession.focus.baselineMap.end()) {
                preservedOverlapBaselines[noteId] = baselineIt->second;
            }
            const auto scratchIt = editSession.focus.overlapNotes.find(noteId);
            if (scratchIt != editSession.focus.overlapNotes.end()) {
                preservedOverlapNotes[noteId] = scratchIt->second;
            }
        }
    }

    // commitBaseline / baselineMap come from committed passes materialize, not the live
    // session preview — otherwise a pending length preview (e.g. end 680) becomes baseline
    // on fader-1 reselect and the next ChangeLength commit is a no-op on rematerialize.
    MidiEventVec loopMidiEventsFromPasses;
    loop.passes.materializeToEventVector(loopMidiEventsFromPasses, loopLength);
    if (selectedNoteIdx < 0 && (pendingMoverCommit || pendingOverlapDiff)) {
        // Session store owns live geometry — do not clear focus from passes materialize or
        // preserve stale overlap closure. Clearing focus.active makes projection return
        // committed passes only (session_20260807_015614: mover at 1248 reverted to 3600).
        populateBaselineMapForEditClosure(editSession.focus, loopMidiEventsFromPasses,
                                          sessionMidiEvents(), channel, loopLength);
        reconcileChangedOverlapNoteIdsFromLiveStore(editSession.focus, sessionMidiEvents(), channel,
                                                    loopLength);
        editSession.focus.active = true;
        return;
    }
    rebuildNoteEditFocusFromStore(editSession.focus, loopMidiEventsFromPasses, channel, loopLength,
                                  selectedNoteIdx);
    if (selectedNoteIdx < 0) {
        for (const auto& [noteId, baseline] : preservedOverlapBaselines) {
            editSession.focus.baselineMap[noteId] = baseline;
        }
        editSession.focus.changedOverlapNoteIds = std::move(preservedChangedOverlapNoteIds);
        for (auto& [noteId, entry] : preservedOverlapNotes) {
            editSession.focus.overlapNotes[noteId] = std::move(entry);
        }
        return;
    }
    populateBaselineMapForEditClosure(editSession.focus, loopMidiEventsFromPasses,
                                      sessionMidiEvents(), channel, loopLength);
    reconcileChangedOverlapNoteIdsFromLiveStore(editSession.focus, sessionMidiEvents(), channel,
                                                loopLength);

    const std::vector<DisplayNote> liveNotes =
        NoteUtils::reconstructNotes(sessionMidiEvents(), loopLength, false);
    if (selectedNoteIdx >= 0 && selectedNoteIdx < static_cast<int>(liveNotes.size())) {
        const DisplayNote& live = liveNotes[static_cast<size_t>(selectedNoteIdx)];
        editSession.focus.last = {live.note, live.velocity, live.startTick, live.endTick};
        const NoteId noteId = editSession.focus.movingNoteId;
        MidiEventVec& sessionEvents = sessionMidiEvents();
        NoteBaseline linearBaseline;
        if (noteId != kInvalidNoteId &&
            findLinearNoteSpanForNoteId(sessionEvents, noteId, channel, linearBaseline, UINT32_MAX,
                                        loopLength)) {
            editSession.focus.last.startTick = linearBaseline.startTick;
            editSession.focus.last.endTick = linearBaseline.endTick;
            editSession.focus.last.pitch = linearBaseline.pitch;
            editSession.focus.last.velocity = linearBaseline.velocity;
        }
        editSession.focus.movingNoteRange.start = editSession.focus.last.startTick;
        editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
        evictOverlapScratchForSelectedNote(editSession.focus, noteId);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::rebuildNoteEditFocusForDisplayNote(Track& track,
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
    rebuildNoteEditFocusFromStore(editSession.focus, committedLoopEvents, channel, loopLength, -1);

    const NoteId baselineNoteId = findBaselineNoteIdForDisplay(editSession.focus, liveSelected);
    editSession.focus.movingNoteId = baselineNoteId;
    evictOverlapScratchForSelectedNote(editSession.focus, baselineNoteId);

    editSession.focus.commitBaseline = {liveSelected.note, liveSelected.velocity,
                                        liveSelected.startTick, liveSelected.endTick};
    editSession.focus.last = editSession.focus.commitBaseline;
    MidiEventVec& sessionEvents = sessionMidiEvents();
    MidiEventVec committedMutable(committedLoopEvents.begin(), committedLoopEvents.end());
    NoteBaseline committedBaseline;
    if (baselineNoteId != kInvalidNoteId &&
        findLinearNoteSpanForNoteId(committedMutable, baselineNoteId, channel, committedBaseline,
                                    UINT32_MAX, loopLength)) {
        editSession.focus.commitBaseline.pitch = committedBaseline.pitch;
        editSession.focus.commitBaseline.velocity = committedBaseline.velocity;
        editSession.focus.commitBaseline.startTick = committedBaseline.startTick;
        editSession.focus.commitBaseline.endTick = committedBaseline.endTick;
    }
    NoteBaseline liveBaseline;
    const bool liveSpanFromCurrentState =
        !editSession.noteEditCurrentState.empty() && baselineNoteId != kInvalidNoteId &&
        editSession.noteEditCurrentState.readCurrentSpan(baselineNoteId, liveBaseline);
    if (liveSpanFromCurrentState) {
        editSession.focus.last = liveBaseline;
        const NoteEditCurrentNoteState* currentRow =
            editSession.noteEditCurrentState.find(baselineNoteId);
        if (currentRow != nullptr) {
            editSession.focus.commitBaseline = currentRow->committedSpan;
        }
    } else if (baselineNoteId != kInvalidNoteId &&
               findLinearNoteSpanForNoteId(sessionEvents, baselineNoteId, channel, liveBaseline,
                                           UINT32_MAX, loopLength)) {
        editSession.focus.last.pitch = liveBaseline.pitch;
        editSession.focus.last.velocity = liveBaseline.velocity;
        editSession.focus.last.startTick = liveBaseline.startTick;
        editSession.focus.last.endTick = liveBaseline.endTick;
    }
    editSession.focus.movingNoteRange.start = editSession.focus.last.startTick;
    editSession.focus.movingNoteRange.end = editSession.focus.last.endTick;
    editSession.focus.active = true;
#if defined(SESSION_CAPTURE)
    if (baselineNoteId != kInvalidNoteId) {
        const NoteEditCurrentNoteState* currentRow =
            editSession.noteEditCurrentState.find(baselineNoteId);
        const int presenceValue =
            currentRow != nullptr ? static_cast<int>(currentRow->presence) : -1;
        const bool projectsToStore =
            editSession.noteEditCurrentState.rowProjectsToStore(baselineNoteId);
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "NOTE_EDIT focus rebuild: noteId=%lu presence=%d rowProjectsToStore=%d",
                   static_cast<unsigned long>(baselineNoteId), presenceValue,
                   projectsToStore ? 1 : 0);
    }
#endif
    if (baselineNoteId != kInvalidNoteId) {
        editSession.focus.baselineMap[baselineNoteId] = editSession.focus.commitBaseline;
    }
    populateBaselineMapForEditClosure(editSession.focus, committedLoopEvents, sessionEvents,
                                      channel, loopLength);
    reconcileChangedOverlapNoteIdsFromLiveStore(editSession.focus, sessionEvents, channel,
                                                loopLength);
#if defined(SESSION_CAPTURE)
    {
        const NoteIdList& overlapIds = editSession.focus.changedOverlapNoteIds;
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "NOTE_EDIT focus rebuild: changedOverlapNoteIds count=%u",
                   static_cast<unsigned>(overlapIds.size()));
        for (NoteId overlapId : overlapIds) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "NOTE_EDIT focus rebuild: changedOverlapNoteId=%lu",
                       static_cast<unsigned long>(overlapId));
        }
    }
#endif
}

EDIT_MANAGER_IMPL_MEM void EditManager::syncSelectedNoteIdxToFilteredInventory(Track& track) {
    if (!editSession.active) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    const std::vector<DisplayNote> filtered = selectableDisplayNotesForEditUi(track);

    if (!editorSelectionHasNote(sessionState.selection)) {
        if (selectedNoteIdx >= 0) {
            setSelectedNoteIdx(-1);
        }
        return;
    }

    const bool lengthBracket = sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
    const uint32_t loopStartTick = noteEditLoopStartTick(track);
    const bool geometryMoverHold =
        editSession.focus.active &&
        editSession.focus.movingNoteId == sessionState.selection.primaryNote &&
        isGeometryEditKind(sessionState.kind);
    int matchIdx = NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
        sessionState.selection, filtered, editSession.focus, loopStartTick, loopLength,
        lengthBracket);
    if (matchIdx < 0 && editorSelectionHasNote(sessionState.selection)) {
        const NoteEditFocus& focus = editSession.focus;
        if (focus.active && focus.movingNoteId == sessionState.selection.primaryNote) {
            const uint32_t storageBracketTick =
                lengthBracket ? focus.last.endTick : focus.last.startTick;
            const uint32_t correctedTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
                storageBracketTick, loopStartTick, loopLength);
            if (sessionState.selection.selectedTick != correctedTick) {
                sessionState.selection.selectedTick = correctedTick;
                selectedTick = correctedTick;
            }
            matchIdx = NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
                sessionState.selection, filtered, focus, loopStartTick, loopLength, lengthBracket);
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
                const uint32_t loopStart = noteEditLoopStartTick(track);
                const bool lengthBracket =
                    sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
                const uint32_t storageStart =
                    lengthBracket ? editSession.focus.last.endTick : editSession.focus.last.startTick;
                if (int byFocus = filteredDisplayNoteIndexForMovingNote(
                        filtered, editSession.focus.movingNoteId, storageStart, loopStart,
                        loopLength);
                    byFocus >= 0) {
                    setSelectedNoteIdx(byFocus);
                } else if (!geometryMoverHold) {
                    setSelectedNoteIdx(noteIdOnlyIdx);
                }
            }
        } else if (selectedNoteIdx >= 0) {
            setSelectedNoteIdx(-1);
        }
    } else if (matchIdx != selectedNoteIdx) {
        if (geometryMoverHold && selectedNoteIdx >= 0 &&
            selectedNoteIdx < static_cast<int>(filtered.size()) &&
            filtered[static_cast<size_t>(selectedNoteIdx)].noteId ==
                sessionState.selection.primaryNote) {
            return;
        }
        setSelectedNoteIdx(matchIdx);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::syncNoteEditFocusLastFromSessionStore(Track& track) {
    if (!editSession.active || !editSession.focus.active) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    NoteEditFocus& focus = editSession.focus;
    if (!editSession.noteEditCurrentState.empty()) {
        syncNoteEditFocusLastFromCurrentState(focus, sessionState.selection.primaryNote,
                                              editSession.noteEditCurrentState);
        return;
    }
    MidiEventVec& events = sessionMidiEvents();
    syncNoteEditFocusLinearFromSessionStore(focus, events, track.getMidiChannel(), loopLength);
}

EDIT_MANAGER_IMPL_MEM bool EditManager::isLiveEditDriverValidForTrack(const Track& track) const {
    if (!editSession.active || !editSession.focus.active) {
        return false;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return false;
    }
    if (!editSession.noteEditCurrentState.empty()) {
        return isLiveEditDriverValidFromCurrentState(sessionState.selection, editSession.focus,
                                                     editSession.noteEditCurrentState);
    }
    return isLiveEditDriverValid(sessionState.selection, editSession.focus, sessionMidiEvents(),
                               track.getMidiChannel(), loopLength);
}

EDIT_MANAGER_IMPL_MEM bool EditManager::isMacroCommitAlignedWithSelectTargetForTrack(
    const Track& track, NoteId selectNoteId, uint32_t selectBracketTick) const {
    if (!editSession.active || !editSession.focus.active) {
        return true;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return true;
    }
    const NoteEditFocus& focus = editSession.focus;
    if (focus.movingNoteId != kInvalidNoteId && selectNoteId != focus.movingNoteId) {
        if (noteEditFocusHasPendingCommit(focus)) {
            return false;
        }
        if (noteEditFocusHasPendingBaselineMapDiff(focus, sessionMidiEvents(),
                                                   track.getMidiChannel(), loopLength)) {
            return false;
        }
    }
    const bool lengthBracket = sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
    return isMacroCommitAlignedWithSelectTarget(selectNoteId, selectBracketTick, editSession.focus,
                                                noteEditLoopStartTick(track), loopLength,
                                                lengthBracket);
}
