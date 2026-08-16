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
        (noteId == focus.movingNoteId || currentState->rowIsVisible(noteId))) {
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
  const NoteEditCurrentNoteState* row = currentState.find(noteId);
  if (row == nullptr) {
    return false;
  }
  if (!currentStateRowIsOverlapParticipant(*row) &&
      focus.baselineMap.find(noteId) == focus.baselineMap.end()) {
    return false;
  }
  if (!currentStateRowIsVisible(*row)) {
    return true;
  }
  if (!currentStateRowIsExistingAndVisible(*row)) {
    return false;
  }
  if (currentStateRowIsRightTailShortened(*row)) {
    // Visible shortened stub: paint currentSpan until macro commit syncs committedSpan.
    return false;
  }
  return false;
}

bool currentStateRowMatchesCommittedDisplayNote(
    const NoteEditCurrentNoteState& row, const NoteUtils::DisplayNoteVec& committedBaseNotes) {
  // visual-cache rows can lack NoteId (161117). Span match still means the row is in the
  // committed base — rematerialize-only extras have a different end (175621 id 63).
  for (const NoteUtils::DisplayNote& dn : committedBaseNotes) {
    if (dn.note == row.committedSpan.pitch && dn.startTick == row.committedSpan.startTick &&
        dn.endTick == row.committedSpan.endTick) {
      return true;
    }
  }
  return false;
}

bool currentStateRowAllowedAsPaintExtra(const NoteEditCurrentNoteState& row,
                                        const NoteUtils::DisplayNoteVec& committedBaseNotes) {
  return currentStateRowLifecycleIsAdded(row) ||
         currentStateRowMatchesCommittedDisplayNote(row, committedBaseNotes);
}

bool nonVisibleParticipantSuppressedFromProjection(const NoteEditCurrentState& currentState,
                                                   NoteId noteId) {
  // Missing row is not Hidden — keep the committed display note (171219 / Stage 1).
  return noteId != kInvalidNoteId && currentState.isRowHiddenOrDeleted(noteId);
}

bool displaySpanIsWrap(uint32_t startTick, uint32_t endTick) {
  return endTick < startTick;
}

bool committedBaseHasSplitHeadTail(const NoteUtils::DisplayNoteVec& committedBaseNotes,
                                   NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  bool hasHead = false;
  bool hasTail = false;
  uint32_t headEnd = 0;
  uint32_t tailStart = 0;
  for (const NoteUtils::DisplayNote& dn : committedBaseNotes) {
    if (dn.noteId != noteId) {
      continue;
    }
    if (displaySpanIsWrap(dn.startTick, dn.endTick)) {
      return true;
    }
    if (dn.startTick == 0 && dn.endTick > dn.startTick) {
      hasHead = true;
      headEnd = dn.endTick;
    } else if (dn.endTick > dn.startTick && (!hasTail || dn.startTick > tailStart)) {
      hasTail = true;
      tailStart = dn.startTick;
    }
  }
  return hasHead && hasTail && headEnd < tailStart;
}

bool participantSpanReplacesSplitCache(const NoteBaseline& current,
                                       const NoteUtils::DisplayNoteVec& committedBaseNotes,
                                       NoteId noteId) {
  if (displaySpanIsWrap(current.startTick, current.endTick)) {
    return true;
  }
  if (!committedBaseHasSplitHeadTail(committedBaseNotes, noteId)) {
    return true;
  }
  // 193525: head-only currentSpan (0–96) must not drop the loop-end tail.
  return current.startTick != 0;
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
  if (currentState != nullptr && !currentState->empty()) {
    // §11 step 5.5: paint participants from current-state membership only (Ended excluded).
    const NoteIdList fromState =
        collectOverlapParticipantNoteIdsFromCurrentState(*currentState, focus.movingNoteId);
    for (NoteId noteId : fromState) {
      if (std::find(participants.begin(), participants.end(), noteId) == participants.end()) {
        participants.push_back(noteId);
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
      if (noteId == kInvalidNoteId || !currentState->rowIsVisible(noteId)) {
        continue;
      }
      const bool inCommittedBase = committedIds.find(noteId) != committedIds.end();
      if (!inCommittedBase && !currentStateRowAllowedAsPaintExtra(row, committedBaseNotes)) {
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
    const NoteEditCurrentNoteState* storeRow =
        currentState != nullptr ? currentState->find(evt.noteId) : nullptr;
    if (storeRow == nullptr ||
        !currentStateRowAllowedAsPaintExtra(*storeRow, committedBaseNotes)) {
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
      if (currentState != nullptr && currentState->rowIsVisible(noteId)) {
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
      NoteBaseline current{};
      if (currentState != nullptr && currentState->readCurrentSpan(dn.noteId, current) &&
          currentState->rowIsVisible(dn.noteId) &&
          !participantSpanReplacesSplitCache(current, committedBaseNotes, dn.noteId)) {
        result.push_back(dn);
      }
      continue;
    }
    if (invalidCommittedRowSupersededByParticipant(dn, participants, hiddenParticipants, focus,
                                                   mutableEvents, channel, loopLength)) {
      continue;
    }
    if (dn.noteId != kInvalidNoteId &&
        std::find(participants.begin(), participants.end(), dn.noteId) == participants.end() &&
        currentState != nullptr) {
      const NoteEditCurrentNoteState* row = currentState->find(dn.noteId);
      if (row != nullptr && currentStateRowIsOverlapParticipant(*row)) {
        NoteBaseline live{};
        if (!findLinearNoteSpanForNoteId(mutableEvents, dn.noteId, channel, live, dn.startTick,
                                         loopLength)) {
          continue;
        }
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

    NoteBaseline current{};
    if (currentState != nullptr && currentState->readCurrentSpan(noteId, current) &&
        currentState->rowIsVisible(noteId) &&
        !participantSpanReplacesSplitCache(current, committedBaseNotes, noteId)) {
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

    if (committedIds.find(noteId) == committedIds.end()) {
      const NoteEditCurrentNoteState* extraRow =
          currentState != nullptr ? currentState->find(noteId) : nullptr;
      if (extraRow == nullptr ||
          !currentStateRowAllowedAsPaintExtra(*extraRow, committedBaseNotes)) {
        continue;
      }
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

NOTE_EDIT_MEM NoteUtils::DisplayNoteVec filterSelectableDisplayNotes(
    const NoteUtils::DisplayNoteVec& projected, const NoteEditCurrentState* currentState,
    const NoteEditFocus& focus, int selectedNoteIdx) {
  if (currentState == nullptr || currentState->empty()) {
    return projected;
  }
  NoteUtils::DisplayNoteVec filtered;
  filtered.reserve(projected.size());
  for (const NoteUtils::DisplayNote& dn : projected) {
    // Missing row is not excluded — keep the painted committed note (174139 / Stage 5).
    if (dn.noteId != kInvalidNoteId && currentState->hasRow(dn.noteId) &&
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
