//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSession.h"

#include <type_traits>

NOTE_EDIT_MEM EditSession::PayloadClearReceipt EditSession::beginPayloadClear() {
  if (payloadClearPhase_ == PayloadClearPhase::Idle ||
      payloadClearPhase_ == PayloadClearPhase::Complete ||
      payloadClearPhase_ == PayloadClearPhase::Superseded) {
    advancePayloadClearToken();
    active = false;
    replaceEditPassOnClose = false;
    openedOnCurrentSnapshot = false;
    openedSnapshotRevision = 0;
    durableCheckpointCreated = false;
    focus.beginClear();
    payloadClearUndoToken_ = undoStack.beginClear();
    payloadClearPhase_ = PayloadClearPhase::Store;
  }
  return {this, payloadClearToken_};
}

NOTE_EDIT_MEM EditSession::PayloadClearResult EditSession::payloadClearResult(
    const PayloadClearReceipt& receipt) const {
  if (receipt.owner != this || receipt.token == 0 || receipt.token != payloadClearToken_)
    return PayloadClearResult::Superseded;
  if (payloadClearPhase_ == PayloadClearPhase::Complete) return PayloadClearResult::Complete;
  if (payloadClearPhase_ == PayloadClearPhase::Idle ||
      payloadClearPhase_ == PayloadClearPhase::Superseded) return PayloadClearResult::Superseded;
  return PayloadClearResult::Progress;
}

NOTE_EDIT_MEM EditSession::PayloadClearResult EditSession::stepPayloadClear(
    const PayloadClearReceipt& receipt) {
  const auto result = payloadClearResult(receipt);
  if (result != PayloadClearResult::Progress) return result;
  const auto releaseEmptyCapacity = [](auto& values) {
    if (!values.empty() || values.capacity() == 0) return false;
    using Vector = std::decay_t<decltype(values)>;
    Vector{}.swap(values); // zero element destructors; allocator duration unqualified
    return true;
  };
  switch (payloadClearPhase_) {
    case PayloadClearPhase::Store:
      if (store.stepClear() == CowLoopEventStore::ClearResult::Complete)
        payloadClearPhase_ = PayloadClearPhase::CurrentState;
      break;
    case PayloadClearPhase::CurrentState:
      if (!noteEditCurrentState.stepClear()) payloadClearPhase_ = PayloadClearPhase::Undo;
      break;
    case PayloadClearPhase::Undo: {
      const auto undoResult = undoStack.stepClear(payloadClearUndoToken_);
      if (undoResult == NoteEditSessionUndoStack::ClearResult::Complete)
        payloadClearPhase_ = PayloadClearPhase::UndoStorage;
      else if (undoResult != NoteEditSessionUndoStack::ClearResult::Progress) {
        payloadClearPhase_ = PayloadClearPhase::Superseded;
        return PayloadClearResult::Superseded;
      }
      break;
    }
    case PayloadClearPhase::UndoStorage:
      // Never turn a replaced/nonempty child stack into an empty-buffer grant.
      if (!undoStack.clearComplete(payloadClearUndoToken_) || !undoStack.entries_.empty()) {
        payloadClearPhase_ = PayloadClearPhase::Superseded;
        return PayloadClearResult::Superseded;
      }
      if (!releaseEmptyCapacity(undoStack.entries_)) payloadClearPhase_ = PayloadClearPhase::Focus;
      break;
    case PayloadClearPhase::Focus:
      if (!focus.stepClear()) payloadClearPhase_ = PayloadClearPhase::EditIds;
      break;
    case PayloadClearPhase::EditIds:
      if (!NoteEditSessionUndoStack::retireScalarVectorUnit(editPassIds) &&
          !releaseEmptyCapacity(editPassIds)) payloadClearPhase_ = PayloadClearPhase::DurableIds;
      break;
    case PayloadClearPhase::DurableIds:
      if (!NoteEditSessionUndoStack::retireScalarVectorUnit(durableCheckpointedEditPassIds) &&
          !releaseEmptyCapacity(durableCheckpointedEditPassIds))
        payloadClearPhase_ = PayloadClearPhase::ApplyRows;
      break;
    case PayloadClearPhase::ApplyRows:
      if (!NoteEditSessionUndoStack::retireEditRowUnit(applyOwnedEditPassRows) &&
          !releaseEmptyCapacity(applyOwnedEditPassRows)) {
        payloadClearPhase_ = PayloadClearPhase::Complete;
        return PayloadClearResult::Complete;
      }
      break;
    case PayloadClearPhase::Idle:
    case PayloadClearPhase::Complete:
    case PayloadClearPhase::Superseded:
      return payloadClearResult(receipt);
  }
  return PayloadClearResult::Progress;
}

#if defined(PIO_UNIT_TEST_NATIVE)
size_t EditSession::payloadClearInventoryForTest() const {
  const auto mapUnits = [](const auto& map) {
    return map.size() + (map.bucket_count() > 2 ? 1u : 0u);
  };
  size_t units = store.readStore().chunkIds().size() +
      (store.hasEventsCacheForTest() ? 1u : 0u) +
      (store.readStore().hasRetainedStorage() ? 1u : 0u);
  units += mapUnits(noteEditCurrentState.rows()) + undoStack.clearInventoryForTest() +
      (undoStack.entries_.capacity() ? 1u : 0u) +
      mapUnits(focus.baselineMap) + mapUnits(focus.overlapNotes);
  units += editPassIds.size() + (editPassIds.capacity() ? 1u : 0u) +
      durableCheckpointedEditPassIds.size() + (durableCheckpointedEditPassIds.capacity() ? 1u : 0u) +
      applyOwnedEditPassRows.size() + (applyOwnedEditPassRows.capacity() ? 1u : 0u);
  for (const auto& row : applyOwnedEditPassRows) units += row.addedEvents.size();
  return units;
}
#endif
