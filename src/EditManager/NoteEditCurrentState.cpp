//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditCurrentState.h"

#include <algorithm>

#include "EditSessionAction.h"
#include "EditSessionLiveStoreSpan.h"
#include "ParticipatingNoteSession.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

namespace {

NOTE_EDIT_MEM bool spansEqual(const NoteBaseline& left, const NoteBaseline& right) {
  return left.pitch == right.pitch && left.velocity == right.velocity &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

}  // namespace

const NoteEditCurrentNoteState* NoteEditCurrentState::find(NoteId noteId) const {
  const auto it = rows_.find(noteId);
  if (it == rows_.end()) {
    return nullptr;
  }
  return &it->second;
}

NoteEditCurrentNoteState* NoteEditCurrentState::find(NoteId noteId) {
  const auto it = rows_.find(noteId);
  if (it == rows_.end()) {
    return nullptr;
  }
  return &it->second;
}

NOTE_EDIT_MEM NoteEditCurrentNoteState& NoteEditCurrentState::upsertRow(
    NoteId noteId, const NoteBaseline& committedSpan, const NoteBaseline& currentSpan,
    NoteEditPresenceType presence) {
  NoteEditCurrentNoteState& row = rows_[noteId];
  row.noteId = noteId;
  row.committedSpan = committedSpan;
  row.currentSpan = currentSpan;
  row.presence = presence;
  row.overlapParticipation = NoteEditOverlapParticipationType::Active;
  return row;
}

NOTE_EDIT_MEM NoteEditCurrentState NoteEditCurrentState::buildFromSessionStore(
    const MidiEventVec& store, uint8_t channel) {
  NoteEditCurrentState built;
  for (const MidiEvent& event : store) {
    if (!event.isNoteOn() || event.data.noteData.velocity == 0 ||
        event.noteId == kInvalidNoteId) {
      continue;
    }
    if (event.channel != channel) {
      continue;
    }
    if (built.find(event.noteId) != nullptr) {
      continue;
    }
    NoteBaseline span{};
    if (!readLiveLinearSpan(store, event.noteId, channel, span)) {
      continue;
    }
    built.upsertRow(event.noteId, span, span, NoteEditPresenceType::Visible);
  }
  return built;
}

NOTE_EDIT_MEM void NoteEditCurrentState::ensureVisibleRowsForDisplayNotes(
    const NoteUtils::DisplayNoteVec& notes) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    if (note.endTick == note.startTick) {
      continue;
    }
    const NoteBaseline span{note.note, note.velocity, note.startTick, note.endTick};
    NoteEditCurrentNoteState* row = find(note.noteId);
    if (row == nullptr) {
      upsertRow(note.noteId, span, span, NoteEditPresenceType::Visible);
      continue;
    }
    if (row->presence != NoteEditPresenceType::Visible) {
      continue;
    }
    if (!spansEqual(row->committedSpan, row->currentSpan)) {
      continue;
    }
    row->committedSpan = span;
    row->currentSpan = span;
  }
}

NOTE_EDIT_MEM void NoteEditCurrentState::projectToSessionStore(MidiEventVec& store,
                                                              uint8_t channel) const {
  store.erase(std::remove_if(store.begin(), store.end(),
                             [](const MidiEvent& event) {
                               return event.isNoteOn() || event.isNoteOff();
                             }),
              store.end());

  for (const auto& [noteId, row] : rows_) {
    if (!currentStateRowIsVisible(row)) {
      continue;
    }
    const NoteBaseline& span = row.currentSpan;
    MidiEvent on = MidiEvent::NoteOn(span.startTick, channel, span.pitch, span.velocity);
    on.noteId = noteId;
    store.push_back(on);
    MidiEvent off = MidiEvent::NoteOff(span.endTick, channel, span.pitch, 0);
    off.noteId = noteId;
    store.push_back(off);
  }
}

NOTE_EDIT_MEM NoteEditCurrentStateVerifyResult NoteEditCurrentState::verifyInvariants(
    NoteId selectedPrimaryNote) const {
  NoteEditCurrentStateVerifyResult result;
  for (const auto& [noteId, row] : rows_) {
    if (row.noteId != noteId) {
      result.passed = false;
      result.duplicateNoteIds = true;
    }
  }
  if (selectedPrimaryNote != kInvalidNoteId && find(selectedPrimaryNote) == nullptr) {
    result.passed = false;
    result.selectedNoteMissing = true;
  }
  return result;
}

NOTE_EDIT_MEM NoteEditCurrentStateVerifyResult NoteEditCurrentState::verifyProjection(
    const MidiEventVec& store, uint8_t channel) const {
  NoteEditCurrentStateVerifyResult result = verifyInvariants();
  for (const auto& [noteId, row] : rows_) {
    if (currentStateRowIsVisible(row)) {
      NoteBaseline live{};
      if (!readLiveLinearSpan(store, noteId, channel, live) ||
          !spansEqual(live, row.currentSpan)) {
        result.passed = false;
        result.visibleProjectionMismatch = true;
      }
      continue;
    }
    NoteBaseline live{};
    if (readLiveLinearSpan(store, noteId, channel, live)) {
      result.passed = false;
      result.hiddenOrDeletedProjected = true;
    }
  }
  return result;
}

NOTE_EDIT_MEM bool NoteEditCurrentState::hasRow(NoteId noteId) const {
  return find(noteId) != nullptr;
}

NOTE_EDIT_MEM bool NoteEditCurrentState::rowIsVisible(NoteId noteId) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  return currentStateRowIsVisible(*row);
}

NOTE_EDIT_MEM NoteEditLifecycleType NoteEditCurrentState::rowLifecycle(NoteId noteId) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return NoteEditLifecycleType::Existing;
  }
  return currentStateRowLifecycle(*row);
}

NOTE_EDIT_MEM bool NoteEditCurrentState::rowProjectsToStore(NoteId noteId) const {
  return rowIsVisible(noteId);
}

NOTE_EDIT_MEM bool NoteEditCurrentState::rowIncludedInSelectableInventory(NoteId noteId) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  if (!currentStateRowIsVisible(*row)) {
    return false;
  }
  return !participatingSpanIsRightTailShortened(row->currentSpan, row->committedSpan);
}

NOTE_EDIT_MEM bool NoteEditCurrentState::rowIncludedInSelectableInventory(
    NoteId noteId, const NoteEditFocus& focus, int selectedNoteIdx) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  if (!currentStateRowIsVisible(*row)) {
    return false;
  }
  return !visibleShortenedOverlapTailInventoryMasked(*row, focus, selectedNoteIdx);
}

NOTE_EDIT_MEM bool NoteEditCurrentState::isRowHiddenOrDeleted(NoteId noteId) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  return row->presence == NoteEditPresenceType::Hidden ||
         currentStateRowLifecycleIsDeleted(*row);
}

NOTE_EDIT_MEM bool NoteEditCurrentState::readCurrentSpan(NoteId noteId, NoteBaseline& out) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  out = row->currentSpan;
  return true;
}

NOTE_EDIT_MEM void NoteEditCurrentState::applyEditSessionAction(const EditSessionAction& action) {
  if (action.targetNoteId == kInvalidNoteId) {
    return;
  }
  const NoteBaseline span{action.pitch, action.velocity, action.startTick, action.endTick};
  NoteEditCurrentNoteState* row = find(action.targetNoteId);

  switch (action.type) {
    case EditSessionActionType::RestoreNote:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Visible);
        return;
      }
      row->overlapParticipation = NoteEditOverlapParticipationType::Active;
      row->currentSpan = span;
      if (participatingSpanIsRightTailShortened(span, row->committedSpan)) {
        row->presence = NoteEditPresenceType::Hidden;
        return;
      }
      if (row->presence == NoteEditPresenceType::Hidden ||
          currentStateRowLifecycleIsDeleted(*row)) {
        row->presence = NoteEditPresenceType::Visible;
      }
      return;
    case EditSessionActionType::ShortenNote:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Visible);
        return;
      }
      row->overlapParticipation = NoteEditOverlapParticipationType::Active;
      row->currentSpan = span;
      if (participatingSpanIsRightTailShortened(span, row->committedSpan)) {
        // A tail below the sealed baseline is itself unsealed until the next macro commit.
        row->visibleOverlapShortenSealed = false;
      }
      if (row->presence == NoteEditPresenceType::Hidden) {
        if (participatingSpanIsRightTailShortened(span, row->committedSpan)) {
          row->presence = NoteEditPresenceType::Visible;
        }
        return;
      }
      // Overlap tail shorten stays Visible (semantically shortened); inventory mask is separate
      // from presence — see rowIncludedInSelectableInventory and display projection.
      return;
    case EditSessionActionType::HideNote:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Hidden);
        return;
      }
      row->overlapParticipation = NoteEditOverlapParticipationType::Active;
      row->currentSpan = span;
      row->presence = NoteEditPresenceType::Hidden;
      return;
    case EditSessionActionType::MoveNote:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Visible);
        return;
      }
      {
        const int32_t startDelta = static_cast<int32_t>(span.startTick) -
                                   static_cast<int32_t>(row->currentSpan.startTick);
        row->currentSpan = span;
        if (startDelta != 0) {
          const int32_t newCommittedStart =
              static_cast<int32_t>(row->committedSpan.startTick) + startDelta;
          const int32_t newCommittedEnd =
              static_cast<int32_t>(row->committedSpan.endTick) + startDelta;
          if (newCommittedStart >= 0 && newCommittedEnd > newCommittedStart) {
            row->committedSpan.startTick = static_cast<uint32_t>(newCommittedStart);
            row->committedSpan.endTick = static_cast<uint32_t>(newCommittedEnd);
          }
        }
      }
      return;
    case EditSessionActionType::ChangeLength:
    case EditSessionActionType::ChangePitch:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Visible);
        return;
      }
      row->currentSpan = span;
      return;
  }
}

NOTE_EDIT_MEM void NoteEditCurrentState::markOverlapParticipationEnded(NoteId noteId) {
  NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return;
  }
  row->overlapParticipation = NoteEditOverlapParticipationType::Ended;
}

NOTE_EDIT_MEM void NoteEditCurrentState::markOverlapParticipationActive(NoteId noteId) {
  NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return;
  }
  row->overlapParticipation = NoteEditOverlapParticipationType::Active;
}

NOTE_EDIT_MEM void NoteEditCurrentState::markRowDeleted(NoteId noteId) {
  NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return;
  }
  row->presence = NoteEditPresenceType::Deleted;
  row->overlapParticipation = NoteEditOverlapParticipationType::Active;
}

NOTE_EDIT_MEM void NoteEditCurrentState::removeRow(NoteId noteId) {
  rows_.erase(noteId);
}

NOTE_EDIT_MEM void NoteEditCurrentState::syncProjectingRowsFromSessionStore(
    const MidiEventVec& store, uint8_t channel) {
  for (auto& [noteId, row] : rows_) {
    if (!currentStateRowIsVisible(row)) {
      continue;
    }
    NoteBaseline span{};
    if (readLiveLinearSpan(store, noteId, channel, span)) {
      row.currentSpan = span;
    }
  }
}

NOTE_EDIT_MEM void NoteEditCurrentState::syncCommittedSpan(NoteId noteId,
                                                         const NoteBaseline& committedSpan) {
  NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return;
  }
  if (row->presence == NoteEditPresenceType::Visible &&
      committedSpan.endTick < row->committedSpan.endTick &&
      committedSpan.startTick == row->committedSpan.startTick) {
    row->visibleOverlapShortenSealed = true;
  }
  row->committedSpan = committedSpan;
  // Macro seal makes committed geometry authoritative for live span and session projection.
  row->currentSpan = committedSpan;
}

NOTE_EDIT_MEM NoteEditCurrentState NoteEditCurrentState::clone() const {
  NoteEditCurrentState copy;
  copy.assignFrom(*this);
  return copy;
}

NOTE_EDIT_MEM void NoteEditCurrentState::assignFrom(const NoteEditCurrentState& other) {
  rows_.clear();
  for (const auto& [noteId, row] : other.rows_) {
    rows_[noteId] = row;
  }
}

NOTE_EDIT_MEM void NoteEditCurrentState::mergeCaptureNotesAsAdded(const MidiEventVec& captureFlat,
                                                                uint8_t channel,
                                                                uint32_t loopLength) {
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(captureFlat, loopLength, false);
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    const NoteBaseline span{note.note, note.velocity, note.startTick, note.endTick};
    upsertRow(note.noteId, span, span, NoteEditPresenceType::Added);
  }
}
