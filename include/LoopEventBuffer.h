//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <memory>
#include "LoopEventStore.h"
#include "MidiEvent.h"

/// Copy-on-write wrapper for chunked loop MIDI events (Phase 4).
template <typename FlatVec = MidiEventVec>
class LoopEventFlatCache {
 public:
  LoopEventFlatCache() : data_(std::make_shared<LoopEventStore>()) {}

  LoopEventStore& mutStore() {
    if (data_.use_count() > 1) {
      data_ = data_->cloneShared();
    }
    flatCache_.reset();
    flatDirty_ = false;
    return *data_;
  }

  /// Drop stale flat cache without syncing back to the chunk store.
  void discardFlatCache() {
    flatCache_.reset();
    flatDirty_ = false;
  }

  const LoopEventStore& readStore() const { return *data_; }

  bool empty() const { return data_->empty(); }
  size_t size() const { return data_->size(); }
  const MidiEvent& at(size_t index) const { return data_->at(index); }

  std::shared_ptr<const LoopEventStore> shareForSnapshot() const { return data_; }

  void restoreFromSnapshot(const std::shared_ptr<const LoopEventStore>& snap) {
    data_ = snap ? snap->cloneShared() : std::make_shared<LoopEventStore>();
    flatCache_.reset();
    flatDirty_ = false;
  }

  /// Legacy flat-vector access for edit/load paths (lazy flatten).
  const FlatVec& readFlat() const {
    if (!flatCache_) {
      flatCache_ = std::make_shared<FlatVec>();
      data_->flatten(*flatCache_);
    }
    return *flatCache_;
  }

  FlatVec& mutFlat() {
    if (data_.use_count() > 1) {
      data_ = data_->cloneShared();
    }
    if (!flatCache_) {
      flatCache_ = std::make_shared<FlatVec>();
      data_->flatten(*flatCache_);
    }
    flatDirty_ = true;
    return *flatCache_;
  }

  bool isFlatDirty() const { return flatDirty_; }

  void syncFlatToStore() {
    if (!flatDirty_ || !flatCache_) {
      return;
    }
    data_ = std::make_shared<LoopEventStore>();
    data_->loadFromFlat(*flatCache_);
    flatDirty_ = false;
  }

 private:
  std::shared_ptr<LoopEventStore> data_;
  mutable std::shared_ptr<FlatVec> flatCache_;
  bool flatDirty_ = false;
};

using CowLoopEventStore = LoopEventFlatCache<MidiEventVec>;
using NoteEditSessionStore = LoopEventFlatCache<SessionMidiEventVec>;
using PublishedLoopEventStore = LoopEventFlatCache<SessionMidiEventVec>;

using MidiSnapshotRef = std::shared_ptr<const LoopEventStore>;
