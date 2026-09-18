//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCompanionOverlapWalk.h"
#include "Utils/NoteEditMem.h"

NOTE_EDIT_MEM bool RuntimeCompanionOverlapWalk::begin(
    const NoteUtils::DisplayNote* notes, size_t count, NoteId target, uint8_t pitch,
    uint32_t start, uint32_t newEnd, uint32_t loopLength) {
  if (!available() ||
      (notes == nullptr && count != 0) || target == kInvalidNoteId || loopLength == 0 ||
      count > 2 * RuntimePlaybackViewConfig::kEventsPerView) return false;
  discard();
  notes_ = notes;
  count_ = count;
  target_ = target;
  pitch_ = pitch;
  start_ = start;
  end_ = newEnd > loopLength ? newEnd % loopLength : newEnd;
  loopLength_ = loopLength;
  phase_ = Phase::Shorten;
  return true;
}

NOTE_EDIT_MEM RuntimeCompanionOverlapResult RuntimeCompanionOverlapWalk::step(size_t quantum) {
  lastStepInputCount_ = 0;
  if (phase_ == Phase::Idle || quantum == 0) return RuntimeCompanionOverlapResult::Denied;
  if (actionReady_) return RuntimeCompanionOverlapResult::ActionReady;
  while (true) {
    if (phase_ == Phase::Complete) return RuntimeCompanionOverlapResult::Complete;
    if (cursor_ == count_) {
      cursor_ = 0;
      phase_ = phase_ == Phase::Shorten ? Phase::Delete : Phase::Complete;
      continue;
    }
    if (quantum == 0) return RuntimeCompanionOverlapResult::Pending;
    --quantum; // Includes unrelated, same-target, and non-overlapping notes.
    ++lastStepInputCount_;
    const size_t index = cursor_++;
    const auto& note = notes_[index];
    if (note.noteId == target_ || note.note != pitch_ ||
        !NoteUtils::notesOverlap(start_, end_, note.startTick, note.endTick, loopLength_)) continue;
    ActionType kind = ActionType::Delete;
    const bool contained = end_ >= start_
        ? note.startTick >= start_ && note.endTick <= end_
        : note.startTick >= start_ || note.endTick <= end_;
    if (contained) {
      if (note.startTick > start_ && note.endTick == end_) continue;
    } else if (note.startTick >= start_ && note.endTick > end_) {
      continue;
    } else if (note.startTick == end_ && note.endTick > end_) {
      kind = ActionType::Delete;
    } else if (note.startTick < start_) {
      kind = ActionType::Shorten;
    }
    if ((phase_ == Phase::Shorten) != (kind == ActionType::Shorten)) continue;
    action_ = {kind, note.noteId, kind == ActionType::Shorten
                                  ? (start_ == 0 ? loopLength_ - 1 : start_ - 1) : 0, index};
    actionReady_ = true;
    return RuntimeCompanionOverlapResult::ActionReady;
  }
}

NOTE_EDIT_MEM bool RuntimeCompanionOverlapWalk::consumeAction() {
  if (!actionReady_) return false;
  actionReady_ = false;
  return true;
}

NOTE_EDIT_MEM void RuntimeCompanionOverlapWalk::discard() {
  notes_ = nullptr;
  count_ = cursor_ = lastStepInputCount_ = 0;
  target_ = kInvalidNoteId;
  pitch_ = 0;
  start_ = end_ = loopLength_ = 0;
  phase_ = Phase::Idle;
  action_ = Action{};
  actionReady_ = false;
}
