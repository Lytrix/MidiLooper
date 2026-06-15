//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <memory>
#include "MidiEvent.h"

/// Copy-on-write wrapper for committed loop MIDI events (Phase 3 ref snapshots).
/// Snapshots share the same buffer via std::shared_ptr until mut() splits on write.
class CowMidiEvents {
 public:
  CowMidiEvents() : data_(std::make_shared<MidiEventVec>()) {}

  MidiEventVec& mut() {
    if (data_.use_count() > 1) {
      data_ = std::make_shared<MidiEventVec>(*data_);
    }
    return *data_;
  }

  const MidiEventVec& read() const { return *data_; }

  bool empty() const { return data_->empty(); }
  size_t size() const { return data_->size(); }

  /// O(1) snapshot handle for undo history (shared, not copied).
  std::shared_ptr<const MidiEventVec> shareForSnapshot() const { return data_; }

  void restoreFromSnapshot(const std::shared_ptr<const MidiEventVec>& snap) {
    data_ = std::make_shared<MidiEventVec>(*snap);
  }

 private:
  std::shared_ptr<MidiEventVec> data_;
};

using MidiSnapshotRef = std::shared_ptr<const MidiEventVec>;
