//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "MidiEvent.h"

#include <cstddef>
#include <cstdint>

/// Fixed-capacity set of overlap candidate NoteIds for one incoming overdub hold.
/// Insert is contains-then-add. Overflow fails; the set never grows.
/// Capacity is Gate 0 provisional: one 8-bar 16th-grid same-pitch hold fits (128).
/// 021304 (68-bar) unique same-pitch count is still unmeasured.
constexpr size_t kOverlapNoteIdSetCapacity = 128;

struct OverlapNoteIdSet {
  bool contains(NoteId noteId) const {
    if (noteId == kInvalidNoteId) {
      return false;
    }
    for (size_t i = 0; i < count_; ++i) {
      if (ids_[i] == noteId) {
        return true;
      }
    }
    return false;
  }

  /// Returns false on invalid id or overflow. Duplicate insert is success and does not grow.
  bool insert(NoteId noteId) {
    if (noteId == kInvalidNoteId) {
      return false;
    }
    if (contains(noteId)) {
      return true;
    }
    if (count_ >= kOverlapNoteIdSetCapacity) {
      overflowed_ = true;
      return false;
    }
    ids_[count_++] = noteId;
    return true;
  }

  void clear() {
    count_ = 0;
    overflowed_ = false;
  }

  size_t size() const { return count_; }
  bool overflowed() const { return overflowed_; }
  NoteId at(size_t index) const { return index < count_ ? ids_[index] : kInvalidNoteId; }

  bool operator==(const OverlapNoteIdSet& other) const {
    if (count_ != other.count_) {
      return false;
    }
    for (size_t i = 0; i < count_; ++i) {
      if (!other.contains(ids_[i])) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const OverlapNoteIdSet& other) const { return !(*this == other); }

 private:
  NoteId ids_[kOverlapNoteIdSetCapacity]{};
  size_t count_ = 0;
  bool overflowed_ = false;
};
