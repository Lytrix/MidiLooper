//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <memory>
#include <type_traits>
#include "LoopEventStore.h"
#include "MidiEvent.h"

/// Copy-on-write wrapper for chunked loop MIDI events (Phase 4).
template <typename EventVec = MidiEventVec>
class LoopEventVectorCache {
 public:
  LoopEventVectorCache() : data_(std::make_shared<LoopEventStore>()) {}

  LoopEventStore& mutStore() {
    if (data_.use_count() > 1) {
      data_ = data_->cloneShared();
    }
    eventsCache_.reset();
    eventsDirty_ = false;
    return *data_;
  }

  /// Drop stale flat cache without syncing back to the chunk store.
  void discardEventsCache() {
    eventsCache_.reset();
    eventsDirty_ = false;
  }

  enum class ClearResult : uint8_t { Progress, Complete };

  /// Destructive component grant, NOT a qualified asynchronous operation.
  /// Caller excludes mutable borrowers and qualifies session/focus each turn.
  /// Never sync dirty values, clone shared content, or materialize a flat view.
  /// Legacy unstamped stores require a live pool, as for clear(). Buffer/header
  /// allocator latency is unqualified; supported event payloads are trivial.
  ClearResult stepClear() {
    static_assert(std::is_trivially_destructible<typename EventVec::value_type>::value,
                  "bounded cache discard requires trivial event payloads");
    if (eventsCache_) {
      discardEventsCache();
      return ClearResult::Progress;
    }
    if (data_.use_count() > 1) {
      data_ = std::make_shared<LoopEventStore>();
      return ClearResult::Progress;
    }
    if (!data_->chunkIds().empty()) {
      data_->releaseCaptureChunkGrain();
      return ClearResult::Progress;
    }
    if (data_->hasRetainedStorage()) {
      data_ = std::make_shared<LoopEventStore>();
      return ClearResult::Progress;
    }
    return ClearResult::Complete;
  }

  const LoopEventStore& readStore() const { return *data_; }

  bool empty() const { return data_->empty(); }
  size_t size() const { return data_->size(); }
  /// Count the values mutEvents would expose without creating/cloning/syncing them.
  size_t eventValueCount() const {
    return eventsCache_ ? eventsCache_->size() : data_->size();
  }
  const MidiEvent& at(size_t index) const { return data_->at(index); }

  std::shared_ptr<const LoopEventStore> shareForSnapshot() const { return data_; }

  void restoreFromSnapshot(const std::shared_ptr<const LoopEventStore>& snap) {
    data_ = snap ? snap->cloneShared() : std::make_shared<LoopEventStore>();
    eventsCache_.reset();
    eventsDirty_ = false;
  }

  /// Legacy event-vector access for edit/load paths (lazy copyEventsTo).
  const EventVec& readEvents() const {
    if (!eventsCache_) {
      eventsCache_ = std::make_shared<EventVec>();
      data_->copyEventsTo(*eventsCache_);
    }
    return *eventsCache_;
  }

  EventVec& mutEvents() {
    if (data_.use_count() > 1) {
      data_ = data_->cloneShared();
    }
    if (!eventsCache_) {
      eventsCache_ = std::make_shared<EventVec>();
      data_->copyEventsTo(*eventsCache_);
    }
    eventsDirty_ = true;
    return *eventsCache_;
  }

  bool isEventsDirty() const { return eventsDirty_; }

  /// Inspect pending values without creating/syncing the flat cache. Mutable
  /// legacy reads can mark an empty cache dirty without adding overlay content.
  bool hasDirtyEventValues() const {
    return eventsDirty_ && eventsCache_ && !eventsCache_->empty();
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  bool hasEventsCacheForTest() const { return static_cast<bool>(eventsCache_); }
#endif

  void syncEventsToStore() {
    if (!eventsDirty_ || !eventsCache_) {
      return;
    }
    data_ = std::make_shared<LoopEventStore>();
    data_->loadFromEvents(*eventsCache_);
    eventsDirty_ = false;
  }

 private:
  std::shared_ptr<LoopEventStore> data_;
  mutable std::shared_ptr<EventVec> eventsCache_;
  bool eventsDirty_ = false;
};

using CowLoopEventStore = LoopEventVectorCache<MidiEventVec>;
using NoteEditSessionStore = LoopEventVectorCache<SessionMidiEventVec>;
using PassesMaterializedEventStore = LoopEventVectorCache<SessionMidiEventVec>;

using MidiSnapshotRef = std::shared_ptr<const LoopEventStore>;
