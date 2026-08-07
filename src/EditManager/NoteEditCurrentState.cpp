//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditCurrentState.h"

#include <algorithm>

#include "EditSessionAction.h"
#include "EditSessionLiveStoreSpan.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

namespace {

NOTE_EDIT_MEM bool projectsToSessionStore(NoteEditPresenceType presence) {
  return presence == NoteEditPresenceType::Visible || presence == NoteEditPresenceType::Added;
}

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

NOTE_EDIT_MEM void NoteEditCurrentState::projectToSessionStore(MidiEventVec& store,
                                                              uint8_t channel) const {
  store.erase(std::remove_if(store.begin(), store.end(),
                             [](const MidiEvent& event) {
                               return event.isNoteOn() || event.isNoteOff();
                             }),
              store.end());

  for (const auto& [noteId, row] : rows_) {
    if (!projectsToSessionStore(row.presence)) {
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
    if (projectsToSessionStore(row.presence)) {
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

NOTE_EDIT_MEM bool NoteEditCurrentState::rowProjectsToStore(NoteId noteId) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  return projectsToSessionStore(row->presence);
}

NOTE_EDIT_MEM bool NoteEditCurrentState::isRowHiddenOrDeleted(NoteId noteId) const {
  const NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return false;
  }
  return row->presence == NoteEditPresenceType::Hidden ||
         row->presence == NoteEditPresenceType::Deleted;
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
      row->currentSpan = span;
      if (row->presence == NoteEditPresenceType::Hidden ||
          row->presence == NoteEditPresenceType::Deleted) {
        row->presence = NoteEditPresenceType::Visible;
      }
      return;
    case EditSessionActionType::ShortenNote:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Visible);
        return;
      }
      row->currentSpan = span;
      // Inner overlap: HideNote then ShortenNote must not promote back to Visible — that
      // reinserts a shortened tail on display/deselect (session_20260807_141218 DNTE len 143).
      return;
    case EditSessionActionType::HideNote:
      if (row == nullptr) {
        upsertRow(action.targetNoteId, span, span, NoteEditPresenceType::Hidden);
        return;
      }
      row->currentSpan = span;
      row->presence = NoteEditPresenceType::Hidden;
      return;
    case EditSessionActionType::MoveNote:
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

NOTE_EDIT_MEM void NoteEditCurrentState::markRowDeleted(NoteId noteId) {
  NoteEditCurrentNoteState* row = find(noteId);
  if (row == nullptr) {
    return;
  }
  row->presence = NoteEditPresenceType::Deleted;
}

NOTE_EDIT_MEM void NoteEditCurrentState::removeRow(NoteId noteId) {
  rows_.erase(noteId);
}

NOTE_EDIT_MEM void NoteEditCurrentState::syncProjectingRowsFromSessionStore(
    const MidiEventVec& store, uint8_t channel) {
  for (auto& [noteId, row] : rows_) {
    if (!projectsToSessionStore(row.presence)) {
      continue;
    }
    NoteBaseline span{};
    if (readLiveLinearSpan(store, noteId, channel, span)) {
      row.currentSpan = span;
    }
  }
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
