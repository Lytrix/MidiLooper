// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "PassReclaim.h"
#include "Loop.h"
#include "Utils/TrackMem.h"
#include <algorithm>

TRACK_COLD_MEM bool Loop::DisabledPassReclaim::prewarm(size_t maximumKeys) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete) || maximumKeys > 4096) return false;
  keys_.resize(maximumKeys); scratch_.resize(maximumKeys); pinned_.resize(maximumKeys);
  return true;
}

TRACK_COLD_MEM bool Loop::DisabledPassReclaim::begin(
    Loop& loop, const GlobalUndoStack& stack, uint8_t slotIndex) {
  const size_t count = (loop.passes.hasRecordPass() ? 1 : 0) +
      loop.passes.overdubPasses.size() + loop.passes.editPasses.size();
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete) || slotIndex >= Config::MAX_LOOPS_PER_TRACK ||
      count > keys_.size() || count > scratch_.size() || count > pinned_.size() ||
      loop.canonicalStorageGeneration == UINT64_MAX || stack.cursor > stack.entries.size()) return false;
  loop_ = &loop; stack_ = &stack; slot_ = slotIndex;
  entries_ = stack.entries.data(); stackSize_ = stack.entries.size(); stackCursor_ = stack.cursor;
  nextEntryId_ = stack.nextEntryId; generation_ = loop.canonicalStorageGeneration; revision_ = loop.playbackRevision;
  count_ = cursor_ = write_ = entry_ = reference_ = units_ = reclaimed_ = 0;
  referencePart_ = 0; width_ = 1; left_ = 0; changed_ = false;
  phase_ = Phase::GatherRecord;
  return true;
}

TRACK_COLD_MEM size_t Loop::DisabledPassReclaim::find(uint64_t key) const {
  size_t first = 0, last = count_;
  while (first < last) {
    const size_t middle = first + (last - first) / 2;
    if (keys_[middle] < key) first = middle + 1;
    else last = middle;
  }
  return first;
}
TRACK_COLD_MEM void Loop::DisabledPassReclaim::mark(PassId id, bool edit) {
  if (id == kInvalidPassId) return;
  const uint64_t key = uint64_t(id) * 2 + edit;
  const size_t index = find(key);
  if (index < count_ && keys_[index] == key) pinned_[index] = 1;
}
TRACK_COLD_MEM bool Loop::DisabledPassReclaim::referenced(PassId id, bool edit) const {
  const uint64_t key = uint64_t(id) * 2 + edit;
  const size_t index = find(key);
  return index < count_ && keys_[index] == key && pinned_[index];
}
TRACK_COLD_MEM void Loop::DisabledPassReclaim::invalidateStorage() {
  if (!changed_) { ++loop_->canonicalStorageGeneration; generation_ = loop_->canonicalStorageGeneration; changed_ = true; }
}

TRACK_COLD_MEM void Loop::DisabledPassReclaim::nextReference() {
  if (entry_ == stackSize_) { cursor_ = write_ = 0; phase_ = Phase::Record; return; }
  const auto& row = stack_->entries[entry_];
  if (row.slotIndex != slot_ || row.kind == UndoEntryKind::LoopBoundaryChange) {
    ++entry_; referencePart_ = 0; reference_ = 0; return;
  }
  if (referencePart_ == 0) {
    reference_ = 0;
    switch (row.kind) {
      case UndoEntryKind::RecordPassAdded: mark(row.passId, false); referencePart_ = 9; break;
      case UndoEntryKind::OverdubPassAdded:
        if (row.passIds.empty()) { mark(row.passId, false); referencePart_ = 2; }
        else referencePart_ = 1;
        break;
      case UndoEntryKind::NoteEditPassClosed:
      case UndoEntryKind::ControlChangeEditPassClosed: referencePart_ = 2; break;
      case UndoEntryKind::ClearSlot: referencePart_ = 3; break;
      default: referencePart_ = 9; break;
    }
    return;
  }
  if (referencePart_ == 1 || referencePart_ == 2) {
    const auto& ids = referencePart_ == 1 ? row.passIds : row.editPassIds;
    if (reference_ < ids.size()) { mark(ids[reference_++], referencePart_ == 2); return; }
    referencePart_ = referencePart_ == 1 ? 2 : 9; reference_ = 0; return;
  }
  if (referencePart_ >= 3 && referencePart_ <= 8) {
    const auto& root = referencePart_ < 6 ? row.beforeSnapshot : row.afterSnapshot;
    const uint8_t part = (referencePart_ - 3) % 3;
    if (root) {
      const auto& passes = root->passes;
      if (part == 0) { if (passes.hasRecordPass()) mark(passes.recordPass.id, false); }
      else if (part == 1 && reference_ < passes.overdubPasses.size()) {
        mark(passes.overdubPasses[reference_++].id, false); return;
      } else if (part == 2 && reference_ < passes.editPasses.size()) {
        mark(passes.editPasses[reference_++].id, true); return;
      }
    }
    ++referencePart_; reference_ = 0; return;
  }
  ++entry_; referencePart_ = 0; reference_ = 0;
}

TRACK_COLD_MEM Loop::DisabledPassReclaim::Result Loop::DisabledPassReclaim::step(size_t maximumUnits) {
  if (!loop_ || !stack_ || phase_ == Phase::Idle || phase_ == Phase::Denied ||
      loop_->canonicalStorageGeneration != generation_ || loop_->playbackRevision != revision_ ||
      stack_->entries.data() != entries_ ||
      stack_->entries.size() != stackSize_ || stack_->cursor != stackCursor_ || stack_->nextEntryId != nextEntryId_) {
    phase_ = Phase::Denied; return Result::Denied;
  }
  auto& passes = loop_->passes;
  for (size_t unit = 0; unit < maximumUnits && phase_ != Phase::Complete; ++unit) {
    ++units_;
    switch (phase_) {
      case Phase::GatherRecord:
        if (passes.hasRecordPass()) { keys_[count_] = uint64_t(passes.recordPass.id) * 2; pinned_[count_++] = 0; }
        phase_ = Phase::GatherOverdub; break;
      case Phase::GatherOverdub:
        if (cursor_ < passes.overdubPasses.size()) {
          keys_[count_] = uint64_t(passes.overdubPasses[cursor_++].id) * 2; pinned_[count_++] = 0;
        } else { cursor_ = 0; phase_ = Phase::GatherEdit; }
        break;
      case Phase::GatherEdit:
        if (cursor_ < passes.editPasses.size()) {
          keys_[count_] = uint64_t(passes.editPasses[cursor_++].id) * 2 + 1; pinned_[count_++] = 0;
        } else { cursor_ = 0; phase_ = Phase::SortSetup; }
        break;
      case Phase::SortSetup:
        if (width_ >= count_) { phase_ = Phase::References; break; }
        if (left_ >= count_) { keys_.swap(scratch_); width_ *= 2; left_ = 0; break; }
        middle_ = std::min(left_ + width_, count_); right_ = std::min(left_ + 2 * width_, count_);
        a_ = out_ = left_; b_ = middle_; phase_ = Phase::SortCopy; break;
      case Phase::SortCopy:
        if (out_ == right_) { left_ = right_; phase_ = Phase::SortSetup; }
        else if (a_ == middle_ || (b_ < right_ && keys_[b_] < keys_[a_])) scratch_[out_++] = keys_[b_++];
        else scratch_[out_++] = keys_[a_++];
        break;
      case Phase::References: nextReference(); break;
      case Phase::Record:
        if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Disabled &&
            !referenced(passes.recordPass.id, false)) {
          invalidateStorage();
          if (!LoopEventStore::releaseLastCommittedChunkRef(passes.recordPass.committedChunkIds)) {
            passes.recordPass.id = kInvalidPassId; ++reclaimed_; phase_ = Phase::Overdub;
          }
        } else phase_ = Phase::Overdub;
        break;
      case Phase::Overdub:
        if (cursor_ == passes.overdubPasses.size()) { phase_ = Phase::OverdubTail; break; }
        if (passes.overdubPasses[cursor_].id != kInvalidPassId &&
            passes.overdubPasses[cursor_].state == CapturePassState::Disabled &&
            !referenced(passes.overdubPasses[cursor_].id, false)) {
          invalidateStorage();
          auto& row = passes.overdubPasses[cursor_];
          if (!LoopEventStore::releaseLastCommittedChunkRef(row.committedChunkIds)) {
            row = OverdubPass{}; ++cursor_; ++reclaimed_;
          }
        } else {
          if (write_ != cursor_) passes.overdubPasses[write_] = std::move(passes.overdubPasses[cursor_]);
          ++write_; ++cursor_;
        }
        break;
      case Phase::OverdubTail:
        if (passes.overdubPasses.size() > write_) passes.overdubPasses.pop_back();
        else { cursor_ = write_ = 0; phase_ = Phase::Edit; }
        break;
      case Phase::Edit:
        if (cursor_ == passes.editPasses.size()) { phase_ = Phase::EditTail; break; }
        if (passes.editPasses[cursor_].state == EditPassState::Disabled &&
            !referenced(passes.editPasses[cursor_].id, true)) {
          invalidateStorage();
          auto& row = passes.editPasses[cursor_];
          if (!row.addedEvents.empty()) row.addedEvents.pop_back();
          else { row = EditPass{}; ++cursor_; ++reclaimed_; }
        } else {
          if (write_ != cursor_) passes.editPasses[write_] = std::move(passes.editPasses[cursor_]);
          ++write_; ++cursor_;
        }
        break;
      case Phase::EditTail:
        if (passes.editPasses.size() > write_) passes.editPasses.pop_back();
        else phase_ = Phase::Complete;
        break;
      default: phase_ = Phase::Denied; return Result::Denied;
    }
  }
  return complete() ? Result::Complete : Result::Pending;
}
