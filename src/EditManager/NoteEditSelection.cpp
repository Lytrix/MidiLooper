//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <vector>

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditEvent.h"
#include "EditManager.h"
#include "EditNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "Logger.h"
#include "NoteEditSessionState.h"
#include "TickPhase.h"
#include "TrackManager.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/SelectNavigation.h"
#include "Utils/NoteUtils.h"

EDIT_MANAGER_IMPL_MEM void EditManager::applyGeometryKindFromControl(Track& track, NoteEditKind kind,
                                                                     bool fromFaderControl) {
    (void)track;
    sessionState.kind = kind;
    if (fromFaderControl && isGeometryEditKind(kind)) {
        encoderCycleNeedsAnchor_ = true;
    }
}

EDIT_MANAGER_IMPL_MEM bool EditManager::beginGeometryMutation(Track& track, NoteEditKind kind,
                                                              bool fromFaderControl) {
    applyGeometryKindFromControl(track, kind, fromFaderControl);
    return pushSessionUndoOnKindChange(track, kind);
}

EDIT_MANAGER_IMPL_MEM void EditManager::resetNoteEditSessionState() {
    sessionState = {};
    lengthEditingMode_ = false;
    lengthFineAnchorEndTick_ = 0;
    encoderCycleNeedsAnchor_ = false;
    lastPushedGeometryKind_ = NoteEditKind::Select;
    sessionPreviewRevision_ = 0;
    sessionPlaybackPreviewRevision_ = 0;
    deferredNoteEditDisplayRefreshPending_ = false;
    deferredNoteEditDisplayRefreshArmedAtMs_ = 0;
    kindBoundaryUndoCacheValid_ = false;
    kindBoundaryUndoWarmPending_ = false;
    kindBoundaryUndoCacheRevision_ = UINT32_MAX;
    invalidateNoteEditDerivedCaches();
}

EDIT_MANAGER_IMPL_MEM void EditManager::applySelectionFromGeometryEdit(Track& track,
                                                                       uint32_t selectedTick,
                                                                       NoteId primaryNote) {
    const bool selectionChanged =
        sessionState.selection.selectedTick != selectedTick ||
        sessionState.selection.primaryNote != primaryNote ||
        !editorSelectionHasNote(sessionState.selection);
    sessionState.selection.selectedTick = selectedTick;
    sessionState.selection.primaryNote = primaryNote;
    sessionState.selection.selectedNotes.clear();
    if (primaryNote != kInvalidNoteId) {
        sessionState.selection.selectedNotes.push_back(primaryNote);
    }
    sessionState.selection.trackId = static_cast<TrackId>(trackManager.getSelectedTrackIndex());
    sessionState.selection.loopId = trackManager.getSelectedLoop(track).loopId;
    const int prevSelectedIdx = selectedNoteIdx;
    syncSelectedNoteIdxToFilteredInventory(track);
    if (selectionChanged) {
        syncGeometrySelectionToUi(track);
    } else if (prevSelectedIdx != selectedNoteIdx) {
        displayManager.requestNoteInfoRefresh(track);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::syncGeometrySelectionToUi(Track& track) {
    displayManager.requestNoteInfoRefresh(track);
}

EDIT_MANAGER_IMPL_MEM void EditManager::applySelectNav(Track& track, uint32_t selectedTick,
                                                       NoteId primaryNote, bool requestFaderSync,
                                                       bool skipFader1Outbound) {
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
        scheduleKindBoundaryUndoWarm();
    }
    const bool selectionIdentityChanged =
        editorSelectionTargetChanged(priorSelection, selectedTick, primaryNote);
    syncNoteEditSessionStateToUi(track);
    if (selectionIdentityChanged) {
        displayManager.requestNoteInfoRefresh(track);
    }
    if (selectionIdentityChanged && !deferSelectionSurfaceEvents_) {
        selectionChangePrior_ = priorSelection;
        selectionChangeRequestFaderSync_ = requestFaderSync;
        emitEditEvent(EditEvent::SelectionChanged);
    }
}

EDIT_MANAGER_IMPL_MEM void EditManager::applyCycleEditKind(Track& track) {
    sessionState.kind = resolveEncoderCyclePress(sessionState.kind, encoderCycleNeedsAnchor_);
    encoderCycleNeedsAnchor_ = false;
    syncNoteEditSessionStateToUi(track);
}

EDIT_MANAGER_IMPL_MEM void EditManager::syncNoteEditSessionStateToUi(Track& track) {
    const int prevSelectedIdx = selectedNoteIdx;
    const NoteUtils::DisplayNoteVec& notes = selectableDisplayNotesAtEditSelect(track);
    const bool lengthBracket = sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
    if (editorSelectionHasNote(sessionState.selection)) {
        selectedNoteIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
            sessionState.selection, notes, noteEditLoopStartTick(track),
            noteEditLoopLengthTicks(track), lengthBracket);
    } else {
        selectedNoteIdx = -1;
    }
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
            currentState->onEnter(*this, track, getSelectedTick());
        }
    }
    sendEditModeProgram(mode);
}

EDIT_MANAGER_IMPL_MEM void EditManager::enterDefaultNoteEditSessionState(Track& track,
                                                                         uint32_t transportTick) {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        selectedNoteIdx = -1;
        clearLastFader1SelectNoteId();
        sessionState.kind = NoteEditKind::Select;
        sessionState.selection = {};
        return;
    }

    const Loop& loop = trackManager.getSelectedLoop(track);
    const uint32_t loopStartTick = noteEditLoopStartTick(track);
    const uint32_t playheadPhase = tickPhaseInLoop(transportTick, loop.startLoopTick, loopLength);
    const uint32_t bracketTick =
        SelectNavigation::noteRelativeTick(playheadPhase, loopStartTick, loopLength);
    selectNoteAtBracket(track, bracketTick);
    if (selectedNoteIdx < 0) {
        selectClosestNote(track, bracketTick);
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
    sessionState.selection.selectedTick = getSelectedTick();
    sessionState.selection.primaryNote = primaryNote;
    sessionState.selection.selectedNotes.clear();
    if (primaryNote != kInvalidNoteId) {
        sessionState.selection.selectedNotes.push_back(primaryNote);
    }
    sessionState.selection.trackId = static_cast<TrackId>(trackManager.getSelectedTrackIndex());
    sessionState.selection.loopId = trackManager.getSelectedLoop(track).loopId;
    syncNoteEditSessionStateToUi(track);
    if (selectedNoteIdx >= 0) {
        syncReferenceStepFromSelectedTick(getSelectedTick());
    }
}

EDIT_MANAGER_IMPL_MEM std::vector<NoteUtils::DisplayNote> EditManager::selectableDisplayNotesForEditUi(
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

EDIT_MANAGER_IMPL_MEM std::vector<SelectNavigation::SelectNavSlot> EditManager::buildSelectNavigationSlots(
    const Track& track, uint32_t selectedTick, bool includeSelectedTickIfMissing) const {
    const uint32_t loopLength =
        isNoteEditActive() ? noteEditLoopLengthTicks(track) : track.getLoopLength();
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    return SelectNavigation::buildSelectNavigationSlots(loopLength, noteEditLoopStartTick(track),
                                                        notes, selectedTick,
                                                        includeSelectedTickIfMissing);
}

EDIT_MANAGER_IMPL_MEM void EditManager::syncReferenceStepFromSelectedTick(uint32_t selectedTick) {
    referenceStep_ = selectedTick / Config::TICKS_PER_16TH_STEP;
}
