// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "GlobalUndoStack.h"
#include "PassReclaim.h"
#include "Utils/TrackMem.h"

TRACK_COLD_MEM bool UndoEntryReleaseCursor::begin(UndoEntry& entry) {
  if (active_) return false;
  entry_ = std::move(entry);
  units_ = 0;
  phase_ = 0;
  active_ = true;
  return true;
}

TRACK_COLD_MEM bool UndoEntryReleaseCursor::step(size_t maximumUnits) {
  for (size_t unit = 0; unit < maximumUnits && active_; ++unit) {
    ++units_;
    switch (phase_) {
      case 0:
        persisted_.begin(entry_.beforeSnapshot);
        ++phase_;
        break;
      case 1:
      case 3:
        if (persisted_.step(1)) ++phase_;
        break;
      case 2:
        persisted_.begin(entry_.afterSnapshot);
        ++phase_;
        break;
      case 4:
        LoopSnapshot::beginRelease(published_, entry_.beforePublishedSnapshot);
        ++phase_;
        break;
      case 5:
      case 7:
        if (LoopSnapshot::stepRelease(published_, 1)) ++phase_;
        break;
      case 6:
        LoopSnapshot::beginRelease(published_, entry_.afterPublishedSnapshot);
        ++phase_;
        break;
      case 8:
        if (!entry_.passIds.empty()) {
          entry_.passIds.pop_back();
        } else {
          PassIdList{}.swap(entry_.passIds);
          ++phase_;
        }
        break;
      case 9:
        if (!entry_.editPassIds.empty()) {
          entry_.editPassIds.pop_back();
        } else {
          EditPassIdList{}.swap(entry_.editPassIds);
          ++phase_;
        }
        break;
      default:
        entry_ = UndoEntry{};
        active_ = false;
        break;
    }
  }
  return complete();
}

TRACK_COLD_MEM bool GlobalUndoStackTrimCursor::begin(GlobalUndoStack& privateStack) {
  if (stack_ || privateStack.cursor > privateStack.entries.size()) return false;
  stack_ = &privateStack;
  phase_ = 0;
  units_ = 0;
  trimmed_ = 0;
  shift_ = 0;
  return true;
}

TRACK_COLD_MEM bool GlobalUndoStackTrimCursor::step(size_t maximumUnits) {
  for (size_t unit = 0; unit < maximumUnits && stack_; ++unit) {
    ++units_;
    auto& entries = stack_->entries;
    switch (phase_) {
      case 0: {
        const bool trim = entries.size() > Config::ABSOLUTE_MAX_UNDO_ENTRIES ||
            (entries.size() > Config::MIN_UNDO_DEPTH && overUndoMemoryPressure(*stack_));
        if (!trim) {
          stack_ = nullptr;
          break;
        }
        release_.begin(entries.front());
        phase_ = 1;
        break;
      }
      case 1:
        if (release_.step(1)) {
          shift_ = 1;
          phase_ = 2;
        }
        break;
      case 2:
        if (shift_ < entries.size()) {
          // Destination was emptied by retirement or the previous move.
          entries[shift_ - 1] = std::move(entries[shift_]);
          ++shift_;
        } else {
          entries.pop_back();  // Moved-from entry, no retained resource graph.
          if (stack_->cursor > 0) --stack_->cursor;
          ++trimmed_;
          phase_ = 0;  // Only now may physical memory feedback select another entry.
        }
        break;
    }
  }
  return complete();
}
