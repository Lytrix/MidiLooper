//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <array>
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
/// RC-MEM-5: the chunk array is grown in blocks of this many chunks instead of being reserved
/// whole at boot. The id space stays flat and contiguous -- chunk ids cross
/// `PersistenceQueue::onChunkFreed`, the committed chunk-id lists and the storage I/O format --
/// so only the backing storage is blocked, never the identifier. The worst occupancy observed
/// in any captured run is 167 of 512 chunks, so this keeps the 512-chunk admission ceiling
/// while backing only what content actually reaches.
constexpr uint16_t POOL_CHUNKS_PER_BLOCK = 64;
constexpr uint16_t POOL_BLOCK_COUNT =
    (POOL_CHUNK_COUNT + POOL_CHUNKS_PER_BLOCK - 1) / POOL_CHUNKS_PER_BLOCK;
constexpr uint32_t BAR_TICKS = 768;
/// The longest loop the bar index will describe. Deliberately far above any supported loop (64 bars is
/// the target maximum) so a legitimate over-run take is unaffected, but finite: the index is sized from
/// `tick / BAR_TICKS`, and an unbounded tick (a torn or uninitialised value near UINT32_MAX) otherwise
/// requests ~5.6M entries -- a single ~22 MB allocation that aborts the firmware (`#MEM_EXT_REFUSED
/// bytes=22369620`). 4096 bars caps the index at 16 KB while leaving every real loop untouched.
constexpr uint32_t MAX_INDEXED_BARS = 4096;
constexpr uint32_t MAX_INDEXED_TICKS = MAX_INDEXED_BARS * BAR_TICKS;
constexpr uint32_t MAX_BAR_INDEX = MAX_INDEXED_BARS;
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

class LoopEventStore;

/// Cursor for a copy-on-write capture canonicalization.  The source remains
/// outside the store while this cursor overwrites one already allocated chunk
/// row at a time, so no sort scratch or chunk allocation is needed on the
/// continuation path.
struct LoopEventStoreRewriteCursor {
  const LoopEventStore* owner = nullptr;
  uint64_t ownershipGeneration = 0;
  uint64_t poolLifetime = 0;
  size_t sourceIndex = 0;
  size_t chunkIndex = 0;
  size_t targetCount = 0;
  size_t trimRemaining = 0;
  uint16_t eventIndexInChunk = 0;
  bool trimming = false;
  bool active = false;
  /// Optional source-owner receipt for consumers that reuse this output cursor.
  /// Both parts are needed: separate descriptors can share a generation, and
  /// the same descriptor address can be reused by a later generation.
  uint32_t sourceGeneration = 0;
  const void* sourceOwner = nullptr;
};

enum class LoopEventStoreRewriteResult : uint8_t { Pending, Complete, Denied };

/// Borrowed control state for the existing prepared-store -> committed-directory
/// transition. Both containers and the owner generation outlive the cursor.
/// Partial destination contents are owned, but not ordered/publishable yet.
class LoopEventStoreCommitCursor {
 public:
  bool active() const { return active_; }
  bool complete() const { return complete_; }
 private:
  friend class LoopEventStore;
  const LoopEventStore* owner_ = nullptr;
  CommittedChunkIdList* destination_ = nullptr;
  const uint16_t* destinationData_ = nullptr;
  const uint64_t* generation_ = nullptr;
  uint64_t expectedGeneration_ = 0;
  uint64_t ownershipGeneration_ = 0;
  uint64_t poolLifetime_ = 0;
  size_t remainingChunks_ = 0;
  size_t remainingEvents_ = 0;
  size_t transferred_ = 0;
  size_t reverseIndex_ = 0;
  bool active_ = false;
  bool complete_ = false;
};


enum class LoopEventStoreReadResult : uint8_t { Pending, Complete, Denied };

/// Unpublished raw input for one pass. The owner retains immutable chunk refs,
/// output storage, and the generation token for this cursor's entire lifetime.
/// Invalidate the token BEFORE releasing/replacing refs (including pool reuse).
/// This reads stored order only; historical edits and pass ordering are separate.
class LoopEventStoreReadCursor {
 public:
  bool ready() const { return complete_ && generation_ && *generation_ == expected_; }
  size_t size() const { return ready() ? count_ : 0; }
  const MidiEvent* data() const { return ready() ? output_ : nullptr; }
  void discard() { *this = LoopEventStoreReadCursor{}; }
 private:
  friend class LoopEventStore;
  const uint16_t* refs_ = nullptr;
  size_t refCount_ = 0, chunk_ = 0, count_ = 0, capacity_ = 0;
  uint16_t event_ = 0;
  MidiEvent* output_ = nullptr;
  const uint64_t* generation_ = nullptr;
  uint64_t expected_ = 0;
  bool active_ = false, complete_ = false;
};

/// Runtime chunk lifecycle (ChunkManager). Independent of persistence state.
enum class ChunkLifecycleState : uint8_t { Free, Recording, Sealed };

enum class LoopEventStoreAppendDeny : uint8_t {
  None,
  PoolUnavailable,
  PoolExhausted,
  /// The event's tick is beyond any supported loop length (a torn/uninitialised value). It is refused
  /// rather than allowed to size the bar index, which would otherwise request megabytes for one event.
  TickOutOfRange,
};

/// Append-only fixed-size event chunks backed by a global PSRAM pool.
class LoopEventStore {
 public:
  class CaptureRange;
  class ChunkReservation;

  /// Cold admission: reserve the complete chunk-id directory and one empty tail.
  /// Only an empty store may become a capture-range producer. Returns false on
  /// pool exhaustion; vector allocation retains the existing allocator policy.
  bool reserveCaptureTail();
  /// Cold directory/persistence admission, without claiming an event chunk.
  bool prewarmCaptureDirectory();
  bool reservedAppendReady() const;
  /// Retire at most one raw-input reference after its canonical copy is complete.
  bool releaseCaptureChunkGrain();
  /// Consume pre-owned physical capacity; never grow a directory/bar index or
  /// allocate from the pool. Unused reservation chunks remain caller-owned.
  bool appendReserved(const MidiEvent& event, ChunkReservation& reservation);
  /// O(1) ownership cut, using the destination's previously reserved empty tail.
  /// The producer must be append-only: no outstanding mutable/rewrite borrowers.
  /// No copy, refcount walk, directory growth or pool allocation occurs here.
  bool sealCaptureRange(CaptureRange& destination);
  /// Non-mutating precondition check for `sealCaptureRange`. The action edge
  /// proves this before admitting a stop so the ownership transfer itself can
  /// never fail midway through admission.
  bool canSealCaptureRange(const CaptureRange& destination) const;
  /// True when this store already holds exactly the pre-reserved empty capture
  /// directory a successor producer must append into, so a begin-capture reset
  /// must not release it (releasing would force a cold re-reservation at the
  /// action edge). False means `clear()`/`reserveCaptureTail()` owns the reset.
  bool resetCaptureExtentPreservingReservedStorage();

  static constexpr size_t NO_EVENT_INDEX = static_cast<size_t>(-1);

  static void initPool();
  static void resetPoolForTests();

  static uint16_t usedChunkCount();
  static uint16_t freeChunkCount();
  /// Number of chunk blocks currently backed by storage (RC-MEM-5 diagnostic). The pool holds
  /// `poolBackedBytes()`, not `POOL_CHUNK_COUNT * sizeof(EventChunk)`, until content reaches
  /// the blocks -- the admission ceiling is unchanged, only the backing storage is lazy.
  static uint16_t poolBlockCount();
  static size_t poolBackedBytes();
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
  /// Release and remove one owned tail reference; leaves list capacity reserved.
  static bool releaseLastCommittedChunkRef(CommittedChunkIdList& refs);

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
  bool append(const MidiEvent& evt, LoopEventStoreAppendDeny* denyOut);
  size_t size() const { return eventCount_; }
  bool empty() const { return size() == 0; }
  const MidiEvent& at(size_t globalIndex) const;

  /// Start replacing this store's fixed rows from an immutable source. The
  /// source count may shrink this store but never grow it. Shrinking releases
  /// one surplus chunk per later rewrite grain; canonicalization never
  /// allocates a chunk.
  bool ownsRewriteCursor(const LoopEventStoreRewriteCursor& cursor) const;
  bool beginRewriteFromOrderedSource(LoopEventStoreRewriteCursor& cursor,
                                     size_t sourceCount) const;
  /// Copy at most `eventQuantum` source rows selected by `orderedIndices`.
  /// `source` must remain immutable until Complete. The caller owns ordering;
  /// this method only performs the bounded in-place canonical-store rewrite.
  LoopEventStoreRewriteResult stepRewriteFromOrderedSource(
      LoopEventStoreRewriteCursor& cursor, const MidiEvent* source,
      const uint16_t* orderedIndices, size_t sourceCount, size_t sourceCapacity,
      size_t eventQuantum);

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
  /// Live pool chunk event count. 0 when the id is unused or out of range.
  static uint16_t chunkUsedCount(uint16_t id);
  /// Constant-work read: one sealed event or its end marker, without allocation.
  /// Caller retains the sealed chunk reference; out is untouched for an end marker.
  static bool readSealedChunkEvent(uint16_t id, size_t index, MidiEvent& out, bool& atEnd);
  /// O(1) binding, no allocation/refcount changes. Output must be disjoint from
  /// pool events and immutable published storage. An active cursor cannot restart.
  static bool beginReadSealedChunks(LoopEventStoreReadCursor& cursor,
      const uint16_t* refs, size_t refCount, MidiEvent* output, size_t capacity,
      const uint64_t& generation);
  /// At most quantum visits, each copying one event OR advancing one chunk.
  /// Invalid/unsealed refs and insufficient capacity deny the entire result.
  /// N events + K refs require max(1, ceil((N+K)/Q)) calls for positive Q.
  static LoopEventStoreReadResult stepReadSealedChunks(
      LoopEventStoreReadCursor& cursor, size_t quantum);
  static void appendChunkRefEventRange(uint16_t id, uint32_t begin, uint32_t endExclusive,
                                       MidiEventVec& out);
  static void appendChunkRefEventRange(
      uint16_t id, uint32_t begin, uint32_t endExclusive,
      std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out);
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

  /// Map NoteOns at or past loopLength into [0, loopLength) so a stop Off
  /// that already landed in-loop keeps its matching On. Then drop remaining
  /// overflow events.
  void mapNoteOnsAtOrBeyondTickIntoLoop(uint32_t loopLength);
  /// Tick projection only: closure/pair normalization remains the caller's job.
  /// Returns whether a row belongs to the finalized record interval.
  static bool projectRecordStopTick(MidiEvent& event, uint32_t loopLength) {
    if (loopLength == 0) return true;
    if (event.isNoteOn() && event.tick >= loopLength) event.tick %= loopLength;
    return event.tick < loopLength;
  }


  std::shared_ptr<LoopEventStore> cloneShared() const;

  const CaptureChunkIdList& chunkIds() const { return chunkIds_; }

  /// Move chunk ownership out of this store into dest (this store cleared). Used by take Seal.
  void detachChunksTo(CaptureChunkIdList& dest);

  /// Seal and move chunk refs into committed list (this store cleared). Fails without abort when
  /// extmem and internal heap cannot admit the committed vector.
  bool detachChunksToCommittedChunkIds(CommittedChunkIdList& dest);
  /// Exclusive prepared output only; never a borrowed raw CaptureRange. Scalar
  /// preflight requires prewarmCaptureDirectory() on a nonempty source (including
  /// persistence admission) and an empty, prewarmed destination. Caller must change the
  /// external generation before replacing either borrowed owner/container.
  bool beginCommittedChunkTransfer(LoopEventStoreCommitCursor& cursor,
                                   CommittedChunkIdList& destination,
                                   const uint64_t& generation);
  /// At most quantum reference moves or directory swaps. No event copies or
  /// allocations. Complete restores original chunk order. On cancellation each
  /// container releases only its own references through existing grain APIs.
  LoopEventStoreRewriteResult stepCommittedChunkTransfer(
      LoopEventStoreCommitCursor& cursor, size_t quantum);


  /// Take ownership of capture chunk refs from ids (ids cleared). CaptureBuilder only.
  void adoptChunkIds(CaptureChunkIdList& ids);

  /// Assign missing note ids on note-ons in place (no copyEventsTo/reload).
  template <typename AssignNoteIdFn>
  void assignMissingNoteIdsToNoteOns(AssignNoteIdFn assignNoteId);

  /// Assign missing note ids from `beginIndex` while `tick < includeUntil`.
  template <typename AssignNoteIdFn>
  void assignMissingNoteIdsToNoteOnsInTickWindow(size_t beginIndex, uint32_t includeUntil,
                                                 AssignNoteIdFn assignNoteId);

  using NoteIdSupplier = NoteId (*)(void* ctx);
  /// In-place fill on a chunk-id list. Chunk ids unchanged. Cold path (NOTE_EDIT open).
  static void assignMissingNoteIdsToNoteOnsInChunkIds(const uint16_t* chunkIds, size_t count,
                                                      NoteIdSupplier supplier, void* ctx);

 private:
  static bool hasHeadroomForCommittedChunkIdList(size_t count);
  static bool tryAssignCommittedChunkIds(CommittedChunkIdList& dest, const uint16_t* ids,
                                        size_t count);

  CaptureChunkIdList chunkIds_;
  uint64_t capturePoolLifetime_ = 0;
  uint64_t ownershipGeneration_ = 1;
  size_t eventCount_ = 0;
  mutable BarIndexVec barFirstIndices_;
  mutable bool barIndexDirty_ = true;
  uint32_t lastAppendedTick_ = 0;

  /// Lazily opened blocks of the flat chunk array: block `b` backs chunk ids
  /// [b * POOL_CHUNKS_PER_BLOCK, (b + 1) * POOL_CHUNKS_PER_BLOCK). Only `allocChunk` opens a
  /// block, and it opens before marking the id used, so any id whose `poolUsed_[id]` is true
  /// has an open block and `chunkSlot` is safe for it.
  static EventChunk* poolBlocks_[LoopEventStoreConfig::POOL_BLOCK_COUNT];
  static EventChunk* chunkBlock(uint16_t id) {
    return poolBlocks_[id / LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK];
  }
  static EventChunk& chunkSlot(uint16_t id) {
    return chunkBlock(id)[id % LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK];
  }
  static bool poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static ChunkLifecycleState poolLifecycle_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static uint16_t poolChunkRefCount_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static bool poolReady_;
  static uint64_t poolLifetime_;
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
  bool appendToTailChunk(const MidiEvent& evt, LoopEventStoreAppendDeny* denyOut = nullptr,
                         ChunkReservation* reservation = nullptr);
  void rebuildBarIndex() const;
  void markBarIndexDirty();
};

/// Physical capacity distinct from raw input ownership. Reserve before accepting
/// input; a canonical writer consumes it without touching sealed input chunks.
/// Store this fixed directory in cold/prewarmed owner storage, not on the stack
/// of a MIDI callback. One reservation never borrows another owner's credits.
class LoopEventStore::ChunkReservation {
 public:
  ChunkReservation() = default;
  ~ChunkReservation();
  ChunkReservation(const ChunkReservation&) = delete;
  ChunkReservation& operator=(const ChunkReservation&) = delete;
  /// Grow to `chunks` total credits. A failed request preserves prior credits.
  /// Each newly reserved chunk is one fixed pool allocation; caller bounds Q.
  bool reserveChunks(size_t chunks);
  size_t availableChunks() const;
  bool releaseGrain(size_t quantum);
 private:
  friend class LoopEventStore;
  std::array<uint16_t, LoopEventStoreConfig::POOL_CHUNK_COUNT> chunks_{};
  size_t count_ = 0;
  uint64_t lifetime_ = 0;
  uint16_t take();
};

/// Exclusive raw capture ownership. Consumers can only copy bounded rows; the
/// canonical worker must write its result elsewhere. Not a canonical pass or a
/// snapshot: no mutable chunk-id accessor and no implicit shared ownership.
class LoopEventStore::CaptureRange {
 public:
  CaptureRange() = default;
  ~CaptureRange();  // cold teardown only; active owners use releaseGrain()
  CaptureRange(const CaptureRange&) = delete;
  CaptureRange& operator=(const CaptureRange&) = delete;
  bool reserveTail();  // cold, after all prior consumers and releases finish
  bool ready() const;
  size_t size() const { return ready() ? eventCount_ : 0; }
  size_t chunkCount() const { return ready() ? storage_.chunkIds_.size() : 0; }
  uint64_t generation() const { return generation_; }
  /// One chunk row read, bounded by the captured end rather than a live extent.
  /// Generation must be the receipt from this seal, including after reuse.
  bool read(uint64_t generation, size_t chunkIndex, uint16_t eventIndex,
            MidiEvent& out, bool& atEnd) const;
  /// Invalidates reads before releasing at most quantum references. True when
  /// empty. A stale pool receipt never decrements a new pool occupant's refs.
  bool releaseGrain(size_t quantum);
 private:
  friend class LoopEventStore;
  LoopEventStore storage_;
  uint64_t generation_ = 0;
  size_t eventCount_ = 0;
  uint16_t tailEnd_ = 0;
  bool sealed_ = false;
  bool releasing_ = false;
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

template <typename AssignNoteIdFn>
void LoopEventStore::assignMissingNoteIdsToNoteOnsInTickWindow(size_t beginIndex,
                                                               uint32_t includeUntil,
                                                               AssignNoteIdFn assignNoteId) {
  size_t index = 0;
  for (uint16_t id : chunkIds_) {
    EventChunk& ec = chunk(id);
    for (uint16_t i = 0; i < ec.used; ++i, ++index) {
      if (index < beginIndex) {
        continue;
      }
      MidiEvent& evt = ec.events[i];
      if (evt.tick >= includeUntil) {
        return;
      }
      if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
        evt.noteId = assignNoteId();
      }
    }
  }
}
