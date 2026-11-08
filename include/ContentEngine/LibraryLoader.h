//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sets browser slice 2a (DEC-259): loading a milestone, returning to the checkpoint, restoring after a failed load.
//
// * `BlobReader` loads a set written by `LibraryWriter` (a milestone in the log, or the checkpoint) into a set whose
//   slots have no history: its header, then each slot's file through `LoadJob`, one library sector a grain.
// * `SetLoad` is the whole operation. A load first writes the working set as the recovery checkpoint (the writer's
//   transaction: a failure there changes nothing), then clears every slot's history, then loads the milestone. A load
//   that fails after the clear is undone by loading the checkpoint back (`Restored`); only when that fails as well is
//   the set left as far as it got (`Lost`). Return clears and loads the checkpoint, writing nothing.
// * The caller loads only with the transport stopped, no take and no NOTE_EDIT session open, and resets the tracks in
//   `clear` (pending launches dropped) before any history is touched; the surface state and tempo the blob carried are
//   the caller's to apply (`blob()`). No heap.

#pragma once

#include <cstdint>

#include "ContentEngine/Library.h"
#include "ContentEngine/LibraryWriter.h"
#include "ContentEngine/SetFile.h"

namespace ContentEngine {

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class BlobReader {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Failed };
  enum class Why : uint8_t { None, Read, BadBlob, Slot };
  using Reservations = RowReservations<Pool, Runs>;

  /// Bytes of one slot's file, from the library sectors that hold it.
  struct Source {
    BlobReader* r;
    bool read(uint8_t* d, uint32_t n) {
      if (n > r->left_) return false;  // past the slot's file
      r->left_ -= n;
      while (n != 0) {
        if (r->fill_ == library::kSector) {
          if (!r->card_->readLibrarySector(r->next_++, r->sector_)) return false;
          r->fill_ = 0;
        }
        const uint32_t k = n < library::kSector - r->fill_ ? n : library::kSector - r->fill_;
        for (uint32_t i = 0; i < k; ++i) d[i] = r->sector_[r->fill_ + i];
        r->fill_ = static_cast<uint16_t>(r->fill_ + k);
        d += k;
        n -= k;
      }
      return true;
    }
  };
  using Load = LoadJob<Pool, Runs, Manifests, HistoryT, Source, library::kSector>;

  /// Starts loading the set whose header is library sector `at`, written as sequence `sequence`.
  void start(Card& card, Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res,
             NoteIdSource& ids, uint32_t at, uint32_t sequence, uint8_t* scratch) {
    load_.cancel();
    card_ = &card;
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    res_ = &res;
    ids_ = &ids;
    scratch_ = scratch;
    at_ = at;
    sequence_ = sequence;
    slot_ = 0;
    loaded_ = 0;
    why_ = Why::None;
    header_ = true;
    status_ = Status::Running;
  }
  /// One grain: the header, or one sector's worth of a slot's file.
  Status step() {
    if (status_ != Status::Running) return status_;
    if (header_) {
      if (!card_->readLibrarySector(at_, sector_)) return fail(Why::Read);
      if (!library::decode(sector_, blob_) || blob_.sequence != sequence_) return fail(Why::BadBlob);
      slotAt_ = at_ + 1;
      header_ = false;
      return status_;
    }
    if (load_.status() == Load::Status::Running) {
      const typename Load::Status st = load_.step();
      if (st == Load::Status::Running) return status_;
      if (st != Load::Status::Done || left_ != 0) return fail(Why::Slot);
      ++loaded_;
      ++slot_;
      return status_;
    }
    while (slot_ < Slots && slot_ < library::kSlots && blob_.slotBytes[slot_] == 0) ++slot_;
    if (slot_ >= Slots || slot_ >= library::kSlots) return status_ = Status::Done;
    next_ = slotAt_;  // each slot's file starts on its own sector
    slotAt_ += (blob_.slotBytes[slot_] + library::kSector - 1) / library::kSector;
    left_ = blob_.slotBytes[slot_];
    fill_ = library::kSector;  // nothing buffered: the first read takes the slot's first sector
    source_.r = this;
    if (load_.start(*pool_, *runs_, *manifests_, *history_, *res_, *ids_, slot_, source_, scratch_) !=
        Load::Status::Running)
      return fail(Why::Slot);
    return status_;
  }
  void cancel() {
    load_.cancel();
    status_ = Status::Idle;
  }
  Status status() const { return status_; }
  Why why() const { return why_; }
  /// The slot that failed (`Why::Slot`), and how many slots loaded.
  uint8_t slot() const { return slot_; }
  uint8_t loaded() const { return loaded_; }
  /// The blob's header: tempo, surface state, note-id high-water mark.
  const library::BlobHeader& blob() const { return blob_; }

 private:
  Status fail(Why w) {
    load_.cancel();
    why_ = w;
    return status_ = Status::Failed;
  }

  Card* card_ = nullptr;
  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  Reservations* res_ = nullptr;
  NoteIdSource* ids_ = nullptr;
  uint8_t* scratch_ = nullptr;
  Load load_;
  Source source_{};
  library::BlobHeader blob_{};
  uint8_t sector_[library::kSector] = {};
  uint32_t at_ = 0, sequence_ = 0, next_ = 0, slotAt_ = 0, left_ = 0;
  uint16_t fill_ = library::kSector;
  uint8_t slot_ = 0, loaded_ = 0;
  bool header_ = false;
  Why why_ = Why::None;
  Status status_ = Status::Idle;
};

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class SetLoad {
 public:
  using Writer = LibraryWriter<Card, Pool, Runs, Manifests, HistoryT, Slots>;
  using Reader = BlobReader<Card, Pool, Runs, Manifests, HistoryT, Slots>;
  using Reservations = RowReservations<Pool, Runs>;
  enum class Kind : uint8_t { Load, Return };
  enum class Result : uint8_t { Ok, NotMounted, Busy, NoMilestone, NoCheckpoint, TooLarge };
  /// Running; Done (loaded / returned); Unchanged (the checkpoint could not be written: nothing changed); Restored (the
  /// load failed, the working set is back from the checkpoint); Lost (that failed too: the set is as far as it got).
  enum class Status : uint8_t { Idle, Running, Done, Unchanged, Restored, Lost };
  /// Resets the tracks, then clears every slot's history (`HistoryT::clearSlot`); false: refused, nothing cleared.
  using ClearFn = bool (*)(void* ctx);

  void bind(Card& card, Writer& writer, Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history,
            Reservations& res, NoteIdSource& ids, uint8_t* scratch, ClearFn clear, void* ctx) {
    card_ = &card;
    writer_ = &writer;
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    res_ = &res;
    ids_ = &ids;
    scratch_ = scratch;
    clear_ = clear;
    ctx_ = ctx;
  }

  /// Starts loading milestone `index`. `c` describes the checkpoint written first (the working set's base, its set,
  /// the time, tempo and surface state); its `loaded` is set to the milestone's sequence here.
  Result beginLoad(uint16_t index, typename Writer::Checkpoint c) {
    if (!writer_->mounted()) return Result::NotMounted;
    if (busy() || writer_->busy()) return Result::Busy;
    if (!writer_->readRecord(index, record_)) return Result::NoMilestone;
    c.loaded = record_.sequence;
    const typename Writer::Result r =
        writer_->beginCheckpoint(*pool_, *runs_, *manifests_, *history_, ids_->peek(), c, scratch_);
    if (r == Writer::Result::TooLarge) return Result::TooLarge;
    if (r != Writer::Result::Ok) return Result::Busy;
    kind_ = Kind::Load;
    phase_ = Phase::Checkpoint;
    return start();
  }
  /// Starts returning to the checkpoint (the working set before the last load).
  Result beginReturn() {
    if (!writer_->mounted()) return Result::NotMounted;
    if (busy() || writer_->busy()) return Result::Busy;
    if (!writer_->checkpoint(checkpoint_)) return Result::NoCheckpoint;
    kind_ = Kind::Return;
    phase_ = Phase::Clear;
    return start();
  }

  /// One grain.
  Status step() {
    if (status_ != Status::Running) return status_;
    switch (phase_) {
      case Phase::Checkpoint: {
        const typename Writer::Status st = writer_->step();
        if (st == Writer::Status::Running) return status_;
        if (st != Writer::Status::Done || !writer_->checkpoint(checkpoint_)) return status_ = Status::Unchanged;
        phase_ = Phase::Clear;
        return status_;
      }
      case Phase::Clear:
        if (!clear_(ctx_)) return status_ = (kind_ == Kind::Load ? Status::Unchanged : Status::Lost);
        if (kind_ == Kind::Load)
          read(library::kLogBase + record_.logSector, record_.sequence);
        else
          read(library::checkpointBase(region()) + 1, checkpoint_.sequence);
        phase_ = Phase::Read;
        return status_;
      case Phase::Read: {
        const typename Reader::Status st = reader_.step();
        if (st == Reader::Status::Running) return status_;
        if (st == Reader::Status::Done) return status_ = restoring_ ? Status::Restored : Status::Done;
        if (kind_ == Kind::Return || restoring_) return status_ = Status::Lost;
        // The load failed after the clear: the working set back from the checkpoint written just before.
        failedWhy_ = reader_.why();
        failedSlot_ = reader_.slot();
        restoring_ = true;
        if (!clear_(ctx_)) return status_ = Status::Lost;
        read(library::checkpointBase(region()) + 1, checkpoint_.sequence);
        return status_;
      }
    }
    return status_;
  }

  bool busy() const { return status_ == Status::Running; }
  Status status() const { return status_; }
  Kind kind() const { return kind_; }
  /// The milestone being loaded, and the checkpoint written before it (or returned to).
  const library::Record& record() const { return record_; }
  const library::CheckpointHeader& checkpoint() const { return checkpoint_; }
  /// The set that was read last: its tempo and surface state (the milestone's, or the checkpoint's after a restore).
  const library::BlobHeader& blob() const { return reader_.blob(); }
  /// Why a load was restored: the reader's reason and slot.
  typename Reader::Why failedWhy() const { return failedWhy_; }
  uint8_t failedSlot() const { return failedSlot_; }

 private:
  enum class Phase : uint8_t { Checkpoint, Clear, Read };

  Result start() {
    restoring_ = false;
    failedWhy_ = Reader::Why::None;
    failedSlot_ = 0;
    status_ = Status::Running;
    return Result::Ok;
  }
  void read(uint32_t at, uint32_t sequence) {
    reader_.start(*card_, *pool_, *runs_, *manifests_, *history_, *res_, *ids_, at, sequence, scratch_);
  }
  uint8_t region() const { return writer_->checkpointRegion(); }

  Card* card_ = nullptr;
  Writer* writer_ = nullptr;
  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  Reservations* res_ = nullptr;
  NoteIdSource* ids_ = nullptr;
  uint8_t* scratch_ = nullptr;
  ClearFn clear_ = nullptr;
  void* ctx_ = nullptr;
  Reader reader_;
  library::Record record_{};
  library::CheckpointHeader checkpoint_{};
  Phase phase_ = Phase::Checkpoint;
  Kind kind_ = Kind::Load;
  bool restoring_ = false;
  typename Reader::Why failedWhy_ = Reader::Why::None;
  uint8_t failedSlot_ = 0;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
