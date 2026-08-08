//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditFocusInternal.h"
#include "NoteEditCurrentState.h"
#include "ParticipatingNoteSession.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

namespace {

template <typename Alloc>
bool resolveParticipantDisplaySpan(const NoteEditFocus& focus, NoteId noteId,
                                   std::vector<MidiEvent, Alloc>& sessionEvents, uint8_t channel,
                                   uint32_t loopLength, uint8_t& pitch, uint8_t& velocity,
                                   uint32_t& startTick, uint32_t& endTick,
                                   const NoteEditCurrentState* currentState) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  if (currentState != nullptr) {
    // C5 / §8 projection contract: visibility gates the row, currentSpan is the geometry.
    // Restoring committedSpan belongs to apply/mutation — projection reading it repainted the
    // pre-shorten length on deselect (session_20260808_013500).
    NoteBaseline current{};
    if (currentState->readCurrentSpan(noteId, current) &&
        (noteId == focus.movingNoteId || currentState->rowProjectsToStore(noteId))) {
      pitch = current.pitch;
      velocity = current.velocity;
      startTick = current.startTick;
      endTick = current.endTick;
      return true;
    }
    if (currentState->isRowHiddenOrDeleted(noteId)) {
      return false;
    }
  }
  if (noteId == focus.movingNoteId) {
    pitch = focus.last.pitch;
    velocity = focus.last.velocity;
    startTick = focus.last.startTick;
    endTick = focus.last.endTick;
    return true;
  }
  NoteBaseline live{};
  if (findLinearNoteSpanForNoteId(sessionEvents, noteId, channel, live, UINT32_MAX, loopLength)) {
    pitch = live.pitch;
    velocity = live.velocity;
    startTick = live.startTick;
    endTick = live.endTick;
    return true;
  }
  const auto baselineIt = focus.baselineMap.find(noteId);
  if (baselineIt == focus.baselineMap.end()) {
    return false;
  }
  const NoteBaseline& baseline = baselineIt->second;
  pitch = baseline.pitch;
  velocity = baseline.velocity;
  startTick = baseline.startTick;
  endTick = baseline.endTick;
  return true;
}

template <typename Alloc>
bool invalidCommittedRowSupersededByParticipant(
    const NoteUtils::DisplayNote& dn, const NoteIdList& participants,
    const std::unordered_set<NoteId>& hiddenParticipants, const NoteEditFocus& focus,
    std::vector<MidiEvent, Alloc>& sessionEvents, uint8_t channel, uint32_t loopLength) {
  if (dn.noteId != kInvalidNoteId) {
    return false;
  }
  const uint32_t dnEnd = dn.endTick >= dn.startTick ? dn.endTick : dn.startTick;
  for (NoteId noteId : participants) {
    if (noteId == kInvalidNoteId || hiddenParticipants.count(noteId) > 0) {
      continue;
    }
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    uint32_t startTick = 0;
    uint32_t endTick = 0;
    if (!resolveParticipantDisplaySpan(focus, noteId, sessionEvents, channel, loopLength, pitch,
                                       velocity, startTick, endTick, nullptr)) {
      continue;
    }
    if (pitch != dn.note) {
      continue;
    }
    if (linearStorageSpansOverlapLocal(dn.startTick, dnEnd, startTick, endTick)) {
      return true;
    }
  }
  return false;
}

bool noteEditCurrentStateOverlapRowIsDisplayMasked(const NoteEditCurrentState& currentState,
                                                 const NoteEditFocus& focus, NoteId noteId,
                                                 uint32_t loopLength) {
  if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
    return false;
  }
  if (!overlapParticipationLatchActive(focus, noteId) &&
      focus.baselineMap.find(noteId) == focus.baselineMap.end()) {
    return false;
  }
  const NoteEditCurrentNoteState* row = currentState.find(noteId);
  if (row == nullptr) {
    return false;
  }
  const NoteBaseline& committed = row->committedSpan;
  if (row->presence == NoteEditPresenceType::Hidden ||
      row->presence == NoteEditPresenceType::Deleted) {
    return true;
  }
  if (row->presence != NoteEditPresenceType::Visible) {
    return false;
  }
  const NoteBaseline& current = row->currentSpan;
  if (current.pitch != committed.pitch) {
    return false;
  }
  if (current.startTick == committed.startTick && current.endTick < committed.endTick) {
    // Visible shortened stub: paint currentSpan until macro commit syncs committedSpan.
    return false;
  }
  return false;
}

bool nonVisibleParticipantSuppressedFromProjection(const NoteEditCurrentState& currentState,
                                                   NoteId noteId) {
  return noteId != kInvalidNoteId && currentState.isRowHiddenOrDeleted(noteId);
}

bool noteEditCurrentStateHasOverlapDisplayMask(const NoteEditCurrentState& currentState,
                                               const NoteEditFocus& focus, uint32_t loopLength) {
  for (const auto& [noteId, row] : currentState.rows()) {
    if (noteId == kInvalidNoteId) {
      continue;
    }
    if (noteEditCurrentStateOverlapRowIsDisplayMasked(currentState, focus, noteId, loopLength)) {
      return true;
    }
  }
  return false;
}

}  // namespace

NOTE_EDIT_FOCUS_INTERNAL_MEM void sortNoteIdList(NoteIdList& ids) {
  for (size_t i = 1; i < ids.size(); ++i) {
    const NoteId key = ids[i];
    size_t j = i;
    while (j > 0 && ids[j - 1] > key) {
      ids[j] = ids[j - 1];
      --j;
    }
    ids[j] = key;
  }
}

NOTE_EDIT_FOCUS_INTERNAL_MEM bool displayNoteOrderBefore(const NoteUtils::DisplayNote& left,
                                         const NoteUtils::DisplayNote& right) {
  if (left.startTick != right.startTick) {
    return left.startTick < right.startTick;
  }
  if (left.note != right.note) {
    return left.note < right.note;
  }
  return left.noteId < right.noteId;
}

NOTE_EDIT_MEM NoteIdList collectProjectionParticipantNoteIds(
    const NoteEditFocus& focus, const NoteEditCurrentState* currentState) {
  NoteIdList participants;
  if (focus.movingNoteId != kInvalidNoteId) {
    participants.push_back(focus.movingNoteId);
  }
  for (NoteId noteId : focus.changedOverlapNoteIds) {
    if (noteId == kInvalidNoteId) {
      continue;
    }
    if (std::find(participants.begin(), participants.end(), noteId) == participants.end()) {
      participants.push_back(noteId);
    }
  }
  if (currentState != nullptr) {
    for (const auto& [noteId, row] : currentState->rows()) {
      if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
        continue;
      }
      if (row.presence == NoteEditPresenceType::Hidden ||
          row.presence == NoteEditPresenceType::Deleted) {
        if (std::find(participants.begin(), participants.end(), noteId) == participants.end()) {
          participants.push_back(noteId);
        }
      }
    }
  }
  sortNoteIdList(participants);
  return participants;
}

template <typename Alloc>
NOTE_EDIT_MEM NoteUtils::DisplayNoteVec projectNoteEditDisplayNotes(
    const NoteUtils::DisplayNoteVec& committedBaseNotes,
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength, const NoteEditCurrentState* currentState) {
  if (loopLength == 0) {
    return committedBaseNotes;
  }
  const bool sessionCurrentStatePaint =
      currentState != nullptr && !currentState->empty();
  const bool overlapDisplayMaskPending =
      currentState != nullptr &&
      noteEditCurrentStateHasOverlapDisplayMask(*currentState, focus, loopLength);
  if (!focus.active && !overlapDisplayMaskPending && !sessionCurrentStatePaint) {
    return committedBaseNotes;
  }

  NoteIdList participants = collectProjectionParticipantNoteIds(focus, currentState);
  std::unordered_set<NoteId> committedIds;
  committedIds.reserve(committedBaseNotes.size());
  for (const NoteUtils::DisplayNote& dn : committedBaseNotes) {
    if (dn.noteId != kInvalidNoteId) {
      committedIds.insert(dn.noteId);
    }
  }
  if (currentState != nullptr) {
    for (const auto& [noteId, row] : currentState->rows()) {
      if (noteId == kInvalidNoteId || !currentState->rowProjectsToStore(noteId)) {
        continue;
      }
      if (std::find(participants.begin(), participants.end(), noteId) == participants.end()) {
        participants.push_back(noteId);
      }
    }
  }
  for (const MidiEvent& evt : sessionEvents) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 || evt.noteId == kInvalidNoteId) {
      continue;
    }
    if (committedIds.find(evt.noteId) != committedIds.end()) {
      continue;
    }
    if (focus.baselineMap.find(evt.noteId) != focus.baselineMap.end()) {
      continue;
    }
    if (std::find(participants.begin(), participants.end(), evt.noteId) == participants.end()) {
      participants.push_back(evt.noteId);
    }
  }
  sortNoteIdList(participants);

  std::unordered_set<NoteId> hiddenParticipants;
  std::vector<MidiEvent, Alloc>& mutableEvents =
      const_cast<std::vector<MidiEvent, Alloc>&>(sessionEvents);
  for (NoteId noteId : participants) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    if (currentState != nullptr &&
        noteEditCurrentStateOverlapRowIsDisplayMasked(*currentState, focus, noteId, loopLength)) {
      hiddenParticipants.insert(noteId);
      continue;
    }
    NoteBaseline live{};
    if (!findLinearNoteSpanForNoteId(mutableEvents, noteId, channel, live, UINT32_MAX,
                                     loopLength)) {
      if (currentState != nullptr && currentState->rowProjectsToStore(noteId)) {
        continue;
      }
      hiddenParticipants.insert(noteId);
    }
  }

  NoteUtils::DisplayNoteVec result;
  result.reserve(committedBaseNotes.size());
  for (const NoteUtils::DisplayNote& dn : committedBaseNotes) {
    if (dn.noteId != kInvalidNoteId && currentState != nullptr &&
        nonVisibleParticipantSuppressedFromProjection(*currentState, dn.noteId)) {
      continue;
    }
    if (dn.noteId != kInvalidNoteId && hiddenParticipants.count(dn.noteId) > 0) {
      continue;
    }
    if (dn.noteId != kInvalidNoteId &&
        std::find(participants.begin(), participants.end(), dn.noteId) != participants.end()) {
      continue;
    }
    if (invalidCommittedRowSupersededByParticipant(dn, participants, hiddenParticipants, focus,
                                                   mutableEvents, channel, loopLength)) {
      continue;
    }
    if (dn.noteId != kInvalidNoteId &&
        std::find(participants.begin(), participants.end(), dn.noteId) == participants.end() &&
        overlapParticipationLatchActive(focus, dn.noteId)) {
      NoteBaseline live{};
      if (!findLinearNoteSpanForNoteId(mutableEvents, dn.noteId, channel, live, dn.startTick,
                                       loopLength)) {
        continue;
      }
    }
    result.push_back(dn);
  }

  std::unordered_map<NoteId, size_t> indexById;
  indexById.reserve(result.size());
  for (size_t i = 0; i < result.size(); ++i) {
    if (result[i].noteId != kInvalidNoteId) {
      indexById[result[i].noteId] = i;
    }
  }

  bool needsSort = false;
  for (NoteId noteId : participants) {
    if (noteId == kInvalidNoteId || hiddenParticipants.count(noteId) > 0) {
      continue;
    }

    NoteUtils::DisplayNote participantDn{};
    participantDn.noteId = noteId;
    if (!resolveParticipantDisplaySpan(focus, noteId, mutableEvents, channel, loopLength,
                                       participantDn.note, participantDn.velocity,
                                       participantDn.startTick, participantDn.endTick,
                                       currentState)) {
      continue;
    }

    const auto it = indexById.find(noteId);
    if (it != indexById.end()) {
      const uint32_t oldStart = result[it->second].startTick;
      result[it->second] = participantDn;
      if (oldStart != participantDn.startTick) {
        needsSort = true;
      }
      continue;
    }

    size_t bindIdx = result.size();
    const auto baselineIt = focus.baselineMap.find(noteId);
    if (baselineIt != focus.baselineMap.end()) {
      const NoteBaseline& baseline = baselineIt->second;
      for (size_t i = 0; i < result.size(); ++i) {
        if (result[i].noteId != kInvalidNoteId) {
          continue;
        }
        if (result[i].note == baseline.pitch && result[i].startTick == baseline.startTick &&
            result[i].endTick == baseline.endTick) {
          bindIdx = i;
          break;
        }
      }
    }
    if (bindIdx < result.size()) {
      const uint32_t oldStart = result[bindIdx].startTick;
      result[bindIdx] = participantDn;
      indexById[noteId] = bindIdx;
      if (oldStart != participantDn.startTick) {
        needsSort = true;
      }
      continue;
    }

    result.push_back(participantDn);
    indexById[noteId] = result.size() - 1;
    needsSort = true;
  }

  if (needsSort) {
    for (size_t i = 1; i < result.size(); ++i) {
      const NoteUtils::DisplayNote key = result[i];
      size_t j = i;
      while (j > 0 && displayNoteOrderBefore(key, result[j - 1])) {
        result[j] = result[j - 1];
        --j;
      }
      result[j] = key;
    }
  }

  return result;
}

NOTE_EDIT_MEM NoteUtils::DisplayNoteVec filterProjectingSelectableDisplayNotes(
    const NoteUtils::DisplayNoteVec& projected, const NoteEditCurrentState* currentState,
    const NoteEditFocus& focus, int selectedNoteIdx) {
  if (currentState == nullptr || currentState->empty()) {
    return projected;
  }
  NoteUtils::DisplayNoteVec filtered;
  filtered.reserve(projected.size());
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId != kInvalidNoteId &&
        !currentState->rowIncludedInSelectableInventory(dn.noteId, focus, selectedNoteIdx)) {
      continue;
    }
    filtered.push_back(dn);
  }
  return filtered;
}

template NoteUtils::DisplayNoteVec projectNoteEditDisplayNotes<InternalHeapFirstAllocator<MidiEvent>>(
    const NoteUtils::DisplayNoteVec&, const MidiEventVec&, const NoteEditFocus&, uint8_t,
    uint32_t, const NoteEditCurrentState*);
template NoteUtils::DisplayNoteVec
projectNoteEditDisplayNotes<ExternalMemoryFirstAllocator<MidiEvent>>(
    const NoteUtils::DisplayNoteVec&, const SessionMidiEventVec&, const NoteEditFocus&, uint8_t,
    uint32_t, const NoteEditCurrentState*);
