//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <algorithm>

#include "EditManager.h"
#include "EditSessionInteraction.h"
#include "Globals.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "ParticipatingNoteSession.h"
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
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const NoteEditCurrentState* currentStatePtr =
        editSession.noteEditCurrentState.empty() ? nullptr : &editSession.noteEditCurrentState;

    if (selectedNoteIdx >= 0) {
        const NoteUtils::DisplayNoteVec notes = selectableDisplayNotesAtEditSelect(track);
        if (selectedNoteIdx < static_cast<int>(notes.size())) {
            rebuildNoteEditFocusForDisplayNote(track, notes[static_cast<size_t>(selectedNoteIdx)]);
        }
        return;
    }

    BaselineMap preservedOverlapBaselines;
    std::unordered_map<NoteId, OverlapNote, NoteIdHash> preservedOverlapNotes;
    NoteBaseline preservedMoverSpan{};
    NoteId preservedMovingNoteId = kInvalidNoteId;
    const bool pendingMoverCommit = noteEditFocusHasPendingCommit(editSession.focus);
    const bool pendingOverlapDiff =
        noteEditFocusHasPendingBaselineMapDiff(editSession.focus, sessionMidiEvents(), channel,
                                               loopLength, currentStatePtr);
    if (!pendingMoverCommit && !pendingOverlapDiff) {
        preservedMovingNoteId = editSession.focus.movingNoteId;
        preservedMoverSpan = editSession.focus.last;
        const NoteIdList preservedParticipantIds =
            currentStatePtr != nullptr
                ? collectOverlapParticipantNoteIdsFromCurrentState(*currentStatePtr,
                                                                   preservedMovingNoteId)
                : NoteIdList{};
        for (NoteId noteId : preservedParticipantIds) {
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

    if (pendingMoverCommit || pendingOverlapDiff) {
        // Session store owns live geometry — do not clear focus (session_20260807_015614).
        overlayUneditedBaselineMapFromDisplayNotes(
            editSession.focus, currentStatePtr, visualCacheNotesForSelectedSlot(track));
        editSession.focus.active = true;
        return;
    }

    editSession.focus.clear();
    for (const auto& [noteId, baseline] : preservedOverlapBaselines) {
        editSession.focus.baselineMap[noteId] = baseline;
    }
    for (auto& [noteId, entry] : preservedOverlapNotes) {
        editSession.focus.overlapNotes[noteId] = std::move(entry);
    }
    if (!editSession.noteEditCurrentState.empty() && preservedMovingNoteId != kInvalidNoteId) {
        clearChangedOverlapParticipationWhenInteractionCleared(
            editSession.focus, editSession.noteEditCurrentState, preservedMoverSpan,
            preservedMovingNoteId);
    }
    invalidateProjectedNoteEditDisplayCache();
}

EDIT_MANAGER_IMPL_MEM void EditManager::rebuildNoteEditFocusForDisplayNote(Track& track,
                                                                            const DisplayNote& liveSelected) {
    if (!editSession.active) {
        editSession.focus.clear();
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    ensureCurrentStateVisibleRowsFromVisualCache(track);

    const NoteEditCurrentState* currentStatePtr =
        editSession.noteEditCurrentState.empty() ? nullptr : &editSession.noteEditCurrentState;
    noteEditFocusApplyDisplayNote(editSession.focus, liveSelected, currentStatePtr);
    evictOverlapScratchForSelectedNote(editSession.focus, editSession.focus.movingNoteId);
    overlayUneditedBaselineMapFromDisplayNotes(
        editSession.focus, currentStatePtr, visualCacheNotesForSelectedSlot(track));
#if defined(SESSION_CAPTURE)
    const NoteId baselineNoteId = editSession.focus.movingNoteId;
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
    if (!editSession.noteEditCurrentState.empty()) {
        const NoteIdList overlapIds = collectOverlapParticipantNoteIdsFromCurrentState(
            editSession.noteEditCurrentState, editSession.focus.movingNoteId);
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "NOTE_EDIT focus rebuild: overlapParticipants count=%u",
                   static_cast<unsigned>(overlapIds.size()));
        for (NoteId overlapId : overlapIds) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "NOTE_EDIT focus rebuild: overlapParticipantNoteId=%lu",
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

EDIT_MANAGER_IMPL_MEM void EditManager::clearVisibleOverlapParticipationBeforeDeselect() {
    if (!editSession.active || !editSession.focus.active ||
        editSession.focus.movingNoteId == kInvalidNoteId ||
        editSession.noteEditCurrentState.empty()) {
        return;
    }
    clearChangedOverlapParticipationWhenInteractionCleared(
        editSession.focus, editSession.noteEditCurrentState, editSession.focus.last,
        editSession.focus.movingNoteId);
    invalidateProjectedNoteEditDisplayCache();
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
    const bool lengthBracket = sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
    return isMacroCommitAlignedWithSelectTarget(selectNoteId, selectBracketTick, editSession.focus,
                                                noteEditLoopStartTick(track), loopLength,
                                                lengthBracket,
                                                editSession.noteEditCurrentState.empty()
                                                    ? nullptr
                                                    : &editSession.noteEditCurrentState);
}
