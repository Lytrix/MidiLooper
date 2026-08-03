//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

#include "MidiEvent.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

namespace LoopEventStoreConfig {
constexpr uint16_t CHUNK_CAPACITY = 256;
constexpr uint16_t POOL_CHUNK_COUNT = 512;
constexpr uint32_t BAR_TICKS = 768;
constexpr uint32_t INTERNAL_HEAP_SAFETY_FLOOR_BYTES = 12 * 1024;
}  // namespace LoopEventStoreConfig

struct EventChunk {
  MidiEvent events[LoopEventStoreConfig::CHUNK_CAPACITY];
  uint16_t used = 0;
  uint32_t firstTick = 0;
  uint32_t lastTick = 0;
};

using CaptureChunkIdList = std::vector<uint16_t, InternalHeapFirstAllocator<uint16_t>>;
using CommittedChunkIdList = std::vector<uint16_t, CommittedChunkIdAllocator<uint16_t>>;
using BarIndexVec = std::vector<size_t, InternalHeapFirstAllocator<size_t>>;

/// Runtime chunk lifecycle (ChunkManager). Independent of persistence state.
enum class ChunkLifecycleState : uint8_t { Free, Recording, Sealed };

/// Append-only fixed-size event chunks backed by a global PSRAM pool.
class LoopEventStore {
 public:
  static constexpr size_t NO_EVENT_INDEX = static_cast<size_t>(-1);

  static void initPool();
  static void resetPoolForTests();

  static uint16_t usedChunkCount();
  static uint16_t freeChunkCount();
  static bool canAllocChunkWithReserve();
  static bool hasInternalHeapHeadroomForNonCriticalWork(uint32_t freeHeapBytes);
  static uint32_t internalHeapSafetyFloorBytes();

  static ChunkLifecycleState chunkLifecycleState(uint16_t id);
  static uint16_t chunkReferenceCount(uint16_t id);

  /// Nested SD-load staging scope: sealChunk marks Persisted instead of admitting mid_pass.
  static void enterSdLoadStaging();
  static void leaveSdLoadStaging();
  static bool isSdLoadStaging();

  /// Derived-view / ephemeral seal: sealChunk does not touch PersistenceQueue.
  static void enterEphemeralSeal();
  static void leaveEphemeralSeal();
  static bool isEphemeralSeal();
  /// Release runtime references held by a chunk-ref list (does not clear the list).
  static void releaseChunkRefs(const CaptureChunkIdList& refs);
  static void releaseChunkRefs(const CommittedChunkIdList& refs);

  /// Sole internal→committed chunk-id transition (≤1 alloc, ≤1 copy). Clears \p src on success.
  static bool transferCaptureChunkIdsToCommittedChunkIds(CommittedChunkIdList& dest,
                                                 CaptureChunkIdList& src);

  /// Copy committed chunk refs (cold path; fails without abort when memory exhausted).
  static bool tryCopyCommittedChunkIds(CommittedChunkIdList& dest,
                                       const CommittedChunkIdList& src);

  /// Duplicate pool chunks for undo/snapshot isolation (committed → committed via staging store).
  static bool deepCloneCommittedChunkIds(CommittedChunkIdList& dest,
                                         const CommittedChunkIdList& src);

  LoopEventStore() = default;
  LoopEventStore(const LoopEventStore&) = delete;
  LoopEventStore& operator=(const LoopEventStore&) = delete;

  bool append(const MidiEvent& evt);
  size_t size() const { return eventCount_; }
  bool empty() const { return size() == 0; }
  const MidiEvent& at(size_t globalIndex) const;

  /// First global index whose event tick is >= tick, or size() if none.
  size_t lowerBoundIndex(uint32_t tick) const;
  /// First global index in a bar; returns size() when no events at/after that bar.
  size_t firstIndexForBar(uint32_t bar) const;
  /// Debug/inspection access to bar start index table.
  const BarIndexVec& barFirstIndices() const;

  void clear();

  /// Move chunk ownership from other into this (other cleared). O(chunks).
  void adoptAll(LoopEventStore& other);

  /// Merge tick-sorted events from other into this via chunk append (no copyEventsTo). O(events).
  void mergeFrom(LoopEventStore& other);

  void copyEventsTo(MidiEventVec& out) const;
  template <typename Alloc>
  void copyEventsTo(std::vector<MidiEvent, Alloc>& out) const {
    out.clear();
    out.reserve(size());
    for (uint16_t id : chunkIds_) {
      const EventChunk& c = chunk(id);
      out.insert(out.end(), c.events, c.events + c.used);
    }
  }
  /// Append events from one chunk id (read-only; does not mutate id).
  static void appendChunkRefEvent(uint16_t id, MidiEventVec& out);
  static void appendChunkRefEvent(
      uint16_t id, std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out);
  /// Append events from chunk refs (read-only; does not mutate ids).
  static void appendChunkRefEvents(const CaptureChunkIdList& ids, MidiEventVec& out);
  static void appendChunkRefEvents(const CommittedChunkIdList& ids, MidiEventVec& out);
  static void appendChunkRefEvents(
      const CaptureChunkIdList& ids,
      std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out);
  static void appendChunkRefEvents(
      const CommittedChunkIdList& ids,
      std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out);
  /// Read firstTick/lastTick for a live pool chunk (false if id unused / out of range).
  static bool chunkTickSpan(uint16_t id, uint32_t& firstTick, uint32_t& lastTick);
  /// Count events referenced by chunk ids without copyEventsTo.
  static size_t countEventsInChunkIds(const CaptureChunkIdList& ids);
  static size_t countEventsInChunkIds(const CommittedChunkIdList& ids);
  void loadFromEvents(const MidiEventVec& events);
  template <typename Alloc>
  void loadFromEvents(const std::vector<MidiEvent, Alloc>& events) {
    clear();
    for (const MidiEvent& evt : events) {
      if (!append(evt)) {
        break;
      }
    }
    if (!events.empty()) {
      lastAppendedTick_ = events.back().tick;
    }
  }

  /// Shift every event tick by delta; bumps up if any tick would go negative.
  void shiftAllTicks(int64_t delta);
  /// Drop events whose tick is >= tickLimit. Preserves event order.
  void dropEventsAtOrBeyondTick(uint32_t tickLimit);

  std::shared_ptr<LoopEventStore> cloneShared() const;

  const CaptureChunkIdList& chunkIds() const { return chunkIds_; }

  /// Move chunk ownership out of this store into dest (this store cleared). Used by take Seal.
  void detachChunksTo(CaptureChunkIdList& dest);

  /// Seal and move chunk refs into committed list (this store cleared). Fails without abort when
  /// extmem and internal heap cannot admit the committed vector.
  bool detachChunksToCommittedChunkIds(CommittedChunkIdList& dest);

  /// Take ownership of capture chunk refs from ids (ids cleared). CaptureBuilder only.
  void adoptChunkIds(CaptureChunkIdList& ids);

  /// Assign missing note ids on note-ons in place (no copyEventsTo/reload).
  template <typename AssignNoteIdFn>
  void assignMissingNoteIdsToNoteOns(AssignNoteIdFn assignNoteId);

 private:
  static bool hasHeadroomForCommittedChunkIdList(size_t count);
  static bool tryAssignCommittedChunkIds(CommittedChunkIdList& dest, const uint16_t* ids,
                                        size_t count);

  CaptureChunkIdList chunkIds_;
  size_t eventCount_ = 0;
  mutable BarIndexVec barFirstIndices_;
  mutable bool barIndexDirty_ = true;
  uint32_t lastAppendedTick_ = 0;

  static EventChunk* pool_;
  static bool poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static ChunkLifecycleState poolLifecycle_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static uint16_t poolChunkRefCount_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static bool poolReady_;
  static uint8_t sdLoadStagingDepth_;
  static uint8_t ephemeralSealDepth_;

  static void retainChunkReference(uint16_t id);
  static void releaseChunkReference(uint16_t id);
  static void sealChunk(uint16_t id);
  static bool isChunkSealed(uint16_t id);
  static void tryFreeChunk(uint16_t id);

  uint16_t allocChunk();
  void freeChunk(uint16_t id);
  bool hasSealedChunks() const;
  EventChunk& chunk(uint16_t id);
  const EventChunk& chunk(uint16_t id) const;
  bool appendToTailChunk(const MidiEvent& evt);
  void rebuildBarIndex() const;
  void markBarIndexDirty();
};

template <typename AssignNoteIdFn>
void LoopEventStore::assignMissingNoteIdsToNoteOns(AssignNoteIdFn assignNoteId) {
  for (uint16_t id : chunkIds_) {
    EventChunk& ec = chunk(id);
    for (uint16_t i = 0; i < ec.used; ++i) {
      MidiEvent& evt = ec.events[i];
      if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
        evt.noteId = assignNoteId();
      }
    }
  }
}
