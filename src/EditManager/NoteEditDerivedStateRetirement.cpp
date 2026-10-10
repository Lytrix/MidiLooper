//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"
#include "EditManager.h"
#include "Globals.h"
#include "TrackManager.h"

#include <type_traits>

EDIT_MANAGER_IMPL_MEM bool EditManager::noteEditDerivedStateClearContextMatches(
    const DerivedStateClear& cursor) const {
    const uint8_t track = trackManager.getSelectedTrackIndex();
    return cursor.owner_ == this && !editSession.active && currentState == nullptr &&
        editSession.payloadClearResult(cursor.receipt_) == EditSession::PayloadClearResult::Complete &&
        track == cursor.track_ && trackManager.getSelectedSlotIndex(track) == cursor.slot_;
}

EDIT_MANAGER_IMPL_MEM EditManager::DerivedStateClearResult EditManager::noteEditDerivedStateClearResult(
    const DerivedStateClear& cursor) const {
    if (cursor.owner_ == nullptr) return DerivedStateClearResult::Idle;
    if (cursor.result_ == DerivedStateClearResult::Superseded ||
        !noteEditDerivedStateClearContextMatches(cursor)) return DerivedStateClearResult::Superseded;
    return cursor.result_;
}

EDIT_MANAGER_IMPL_MEM EditManager::DerivedStateClearResult EditManager::beginNoteEditDerivedStateClear(
    Track& track, const EditSession::PayloadClearReceipt& receipt, DerivedStateClear& cursor) {
    if (&track != &trackManager.getSelectedTrack()) return DerivedStateClearResult::Superseded;
    const auto payload = editSession.payloadClearResult(receipt);
    if (payload == EditSession::PayloadClearResult::Superseded) return DerivedStateClearResult::Superseded;
    if (payload != EditSession::PayloadClearResult::Complete || editSession.active || currentState != nullptr)
        return DerivedStateClearResult::Unavailable;
    if (cursor.owner_ == this && cursor.receipt_.owner == receipt.owner &&
        cursor.receipt_.token == receipt.token &&
        (cursor.result_ == DerivedStateClearResult::Progress || cursor.result_ == DerivedStateClearResult::Complete))
        return noteEditDerivedStateClearResult(cursor);

    cursor = {};
    cursor.owner_ = this;
    cursor.receipt_ = receipt;
    cursor.track_ = trackManager.getSelectedTrackIndex();
    cursor.slot_ = trackManager.getSelectedSlotIndex(cursor.track_);
    cursor.result_ = DerivedStateClearResult::Progress;
    cancelDefaultNoteEditSelectionPreparation();
    const auto invalidateSelection = [](EditorSelection& selection) {
        selection.trackId = kInvalidTrackId;
        selection.loopId = kInvalidLoopId;
        selection.primaryNote = kInvalidNoteId;
        selection.selectedTick = 0;
        // selectedNotes remains owned until its grants.
    };
    sessionState.kind = NoteEditKind::Select;
    invalidateSelection(sessionState.selection);
    invalidateSelection(selectionChangePrior_);
    selectionChangeRequestFaderSync_ = false;
    selectedNoteIdx = -1;
    currentEditMode = EDIT_MODE_NONE;
    hasMovedBracket = false;
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
    kindBoundaryUndoWarmArmedAtMs_ = 0;
    kindBoundaryUndoCacheRevision_ = UINT32_MAX;
    invalidateNoteEditDerivedCacheMetadata();
    return DerivedStateClearResult::Progress;
}

EDIT_MANAGER_IMPL_MEM EditManager::DerivedStateClearResult EditManager::stepNoteEditDerivedStateClear(
    DerivedStateClear& cursor) {
    const auto result = noteEditDerivedStateClearResult(cursor);
    if (result != DerivedStateClearResult::Progress) {
        if (cursor.owner_ == this && result == DerivedStateClearResult::Superseded)
            cursor.result_ = result;
        return result;
    }
    const auto dropFlatBuffer = [](auto& values) {
        using Vector = std::decay_t<decltype(values)>;
        static_assert(std::is_trivially_destructible<typename Vector::value_type>::value,
                      "flat buffer retirement must not hide nested destruction");
        if (values.capacity() == 0) return false;
        Vector{}.swap(values);
        return true;
    };
    const auto retireSelection = [&dropFlatBuffer](auto& ids) {
        if (!ids.empty()) { ids.pop_back(); return true; }
        return dropFlatBuffer(ids);
    };
    using Phase = DerivedStateClear::Phase;
    switch (cursor.phase_) {
    case Phase::Materialized:
        if (!dropFlatBuffer(noteEditFocusMaterializedLoopEvents_)) cursor.phase_ = Phase::Paint;
        break;
    case Phase::Paint:
        if (!dropFlatBuffer(noteEditPaintDisplayCacheNotes_)) cursor.phase_ = Phase::Committed;
        break;
    case Phase::Committed:
        if (!dropFlatBuffer(noteEditCommittedDisplayCacheNotes_)) cursor.phase_ = Phase::Selectable;
        break;
    case Phase::Selectable:
        if (!dropFlatBuffer(noteEditSelectableDisplayCacheNotes_)) cursor.phase_ = Phase::Selection;
        break;
    case Phase::Selection:
        if (!retireSelection(sessionState.selection.selectedNotes)) cursor.phase_ = Phase::PriorSelection;
        break;
    case Phase::PriorSelection:
        if (!retireSelection(selectionChangePrior_.selectedNotes)) cursor.phase_ = Phase::KindPayload;
        break;
    case Phase::KindPayload:
        if (!NoteEditSessionUndoStack::retireEntryUnit(kindBoundaryUndoCache_)) cursor.phase_ = Phase::KindShell;
        break;
    case Phase::KindShell:
        // Every nested value/node and large bucket table has drained. Fixed
        // empty-shell assignment only; allocator duration is unqualified.
        kindBoundaryUndoCache_ = {};
        cursor.phase_ = Phase::Terminal;
        break;
    case Phase::Terminal:
        cursor.phase_ = Phase::Complete;
        cursor.result_ = DerivedStateClearResult::Complete;
        return cursor.result_;
    case Phase::Complete:
        return cursor.result_;
    }
    return DerivedStateClearResult::Progress;
}
