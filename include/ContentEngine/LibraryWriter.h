//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Writing milestones and the checkpoint into the library (sets browser slice 1c-ii; DEC-258).
//
// * `SetSnapshot` — the whole working set at one instant T0: every slot's history chain (states, current index, empty-
//   below flag) captured and every captured manifest pinned until `release`, so changes made while the set is written
//   are not in it. It computes each slot's set-file size exactly (`SaveJob`'s layout), the metrics (loops, shortest /
//   longest loop) and the mosaic (2 bits a slot: empty, or the slot's rows in thirds of the fullest slot's).
// * `LibraryWriter` — one job at a time, in grains of at most one sector write, each only once the card is ready for
//   it. A milestone: the size is checked first (`library::fits`: refused before anything is written), then the blob
//   (header, each slot's set file from the snapshot, sector-aligned) at the log's append point, programmed; then its
//   index record, programmed; then the index header into the sector that is not current, programmed — only then is the
//   milestone published. A checkpoint: the blob into the region that is not current, programmed, then its header,
//   programmed. A trash, restore or empty-trash record (DEC-268): the record, programmed, then the index header — no
//   blob, nothing older rewritten. A working-set change (DEC-269): the index header alone. Any failed write ends the job unpublished: the current header / checkpoint stay, the space is reused.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/Library.h"
#include "ContentEngine/SetFile.h"

namespace ContentEngine {

template <typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class SetSnapshot {
 public:
  static constexpr uint8_t kMaxStates = Config::kHistoryDepth;

  /// Captures and pins every slot's chain (call `release` when done). `noteIdHigh`: the note-id high-water mark of T0.
  void capture(Runs& runs, Manifests& manifests, const HistoryT& history, uint32_t noteIdHigh) {
    release(manifests, runs);
    runs_ = &runs;
    noteIdHigh_ = noteIdHigh;
    used_ = 0;
    for (uint8_t s = 0; s < Slots; ++s) {
      first_[s] = used_;
      count_[s] = history.chain(s, ids_ + used_, static_cast<uint8_t>(kMaxStates - used_), current_[s], below_[s]);
      for (uint8_t i = 0; i < count_[s]; ++i) manifests.pin(ids_[used_ + i]);
      used_ = static_cast<uint8_t>(used_ + count_[s]);
      bytes_[s] = count_[s] ? sizeOf(manifests, s) : 0;
    }
    metrics(manifests);
    held_ = true;
  }
  void release(Manifests& manifests, Runs& runs) {
    if (!held_) return;
    for (uint8_t i = 0; i < used_; ++i) manifests.unpin(ids_[i], runs);
    held_ = false;
    used_ = 0;
  }
  bool held() const { return held_; }

  uint8_t count(uint8_t s) const { return count_[s]; }
  const ManifestId* chain(uint8_t s) const { return ids_ + first_[s]; }
  int16_t current(uint8_t s) const { return current_[s]; }
  bool emptyBelow(uint8_t s) const { return below_[s]; }
  uint32_t bytes(uint8_t s) const { return bytes_[s]; }
  uint32_t noteIdHigh() const { return noteIdHigh_; }
  uint8_t loops() const { return loops_; }
  uint32_t shortest() const { return shortest_; }
  uint32_t longest() const { return longest_; }
  const uint8_t* mosaic() const { return mosaic_; }

 private:
  /// The slot's set file in bytes: header 24, each run once 8 + 16 a row, each state 9 + 2 a run, trailer 4.
  uint32_t sizeOf(const Manifests& manifests, uint8_t s) {
    for (auto& b : seen_) b = 0;
    uint32_t n = 24 + 4;
    for (uint8_t i = 0; i < count_[s]; ++i) {
      const auto& m = manifests.manifest(ids_[first_[s] + i]);
      n += 9u + 2u * m.runCount;
      for (uint8_t k = 0; k < m.runCount; ++k) {
        const RunId r = m.runs[k];
        if (seen_[r / 8] & (1u << (r % 8))) continue;
        seen_[r / 8] = static_cast<uint8_t>(seen_[r / 8] | (1u << (r % 8)));
        n += 8u + kSetFileRowBytes * runs_->run(r).rowCount;
      }
    }
    return n;
  }
  /// Loops, the shortest and longest loop, and the mosaic, from each slot's current state.
  void metrics(const Manifests& manifests) {
    uint32_t rows[Slots] = {};
    uint32_t most = 0;
    loops_ = 0;
    shortest_ = longest_ = 0;
    for (uint8_t s = 0; s < Slots; ++s) {
      if (count_[s] == 0 || current_[s] < 0) continue;
      const auto& m = manifests.manifest(ids_[first_[s] + current_[s]]);
      if (m.runCount == 0) continue;
      ++loops_;
      if (shortest_ == 0 || m.loopLengthTicks < shortest_) shortest_ = m.loopLengthTicks;
      if (m.loopLengthTicks > longest_) longest_ = m.loopLengthTicks;
      for (uint8_t k = 0; k < m.runCount; ++k) rows[s] += runs_->run(m.runs[k]).rowCount;
      if (rows[s] == 0) rows[s] = 1;  // an empty loop (DEC-256) still shows
      if (rows[s] > most) most = rows[s];
    }
    for (auto& b : mosaic_) b = 0;
    for (uint8_t s = 0; s < Slots && s < 64; ++s) {
      if (rows[s] == 0) continue;
      const uint8_t level = static_cast<uint8_t>(1 + (rows[s] * 3 - 1) / most);
      const uint8_t v = level > 3 ? 3 : level;
      mosaic_[s / 4] = static_cast<uint8_t>(mosaic_[s / 4] | (v << (2 * (s % 4))));
    }
  }

  Runs* runs_ = nullptr;
  ManifestId ids_[kMaxStates] = {};
  uint8_t first_[Slots] = {}, count_[Slots] = {};
  int16_t current_[Slots] = {};
  bool below_[Slots] = {};
  uint32_t bytes_[Slots] = {};
  uint8_t used_ = 0;
  bool held_ = false;
  uint32_t noteIdHigh_ = 0;
  uint8_t loops_ = 0;
  uint32_t shortest_ = 0, longest_ = 0;
  uint8_t mosaic_[16] = {};
  uint8_t seen_[(Config::kRunCount + 7) / 8] = {};
};

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class LibraryWriter {
 public:
  using Snapshot = SetSnapshot<Pool, Runs, Manifests, HistoryT, Slots>;
  enum class Result : uint8_t { Ok, NotMounted, Busy, IndexFull, LogFull, TooLarge, Failed };
  enum class Status : uint8_t { Idle, Running, Done, Failed };

  struct Milestone {
    uint16_t set = 0;
    library::Stage stage = library::Stage::Sketch;
    uint8_t stageNumber = 0;
    uint32_t parent = 0;
    uint32_t wallTime = 0;
    uint16_t tempoTenths = 0;
    uint8_t surface[library::kSurfaceBytes] = {};
  };
  struct Checkpoint {
    uint32_t wallTime = 0;
    uint32_t base = 0;
    uint16_t set = 0;
    uint32_t loaded = 0;
    uint16_t tempoTenths = 0;
    uint8_t surface[library::kSurfaceBytes] = {};
  };

  /// Reads the index header and both checkpoint headers (the card has the library open). False: a read failed.
  bool mount(Card& card) {
    card_ = &card;
    mounted_ = false;
    uint8_t a[library::kSector], b[library::kSector];
    if (!card.readLibrarySector(0, a) || !card.readLibrarySector(1, b)) return false;
    header_ = library::currentHeader(a, b, which_);
    if (!card.readLibrarySector(library::checkpointBase(0), a) || !card.readLibrarySector(library::checkpointBase(1), b))
      return false;
    haveCheckpoint_ = library::currentCheckpoint(a, b, checkpoint_, region_);
    mounted_ = true;
    return true;
  }
  bool mounted() const { return mounted_; }
  const library::IndexHeader& header() const { return header_; }
  bool checkpoint(library::CheckpointHeader& out) const {
    if (haveCheckpoint_) out = checkpoint_;
    return haveCheckpoint_;
  }
  bool readRecord(uint16_t i, library::Record& r) {
    uint8_t s[library::kSector];
    return mounted_ && i < header_.count && card_->readLibrarySector(library::recordSector(i), s) &&
           library::decode(s + library::recordOffset(i), r);
  }
  /// The region (0 / 1) holding the current checkpoint.
  uint8_t checkpointRegion() const { return region_; }
  bool busy() const { return status_ == Status::Running; }
  Status status() const { return status_; }
  /// The last job published the working set alone (`beginWorking`).
  bool workingJob() const { return headerOnly_; }
  /// The record of the milestone the last job published.
  const library::Record& published() const { return record_; }

  /// Captures the set and starts writing it as a milestone; refused by name before anything is written.
  Result beginMilestone(Pool& pool, Runs& runs, Manifests& manifests, const HistoryT& history, uint32_t noteIdHigh,
                        const Milestone& m, uint8_t* scratch) {
    if (!mounted_) return Result::NotMounted;
    if (busy()) return Result::Busy;
    bind(pool, runs, manifests, scratch);
    snap_.capture(runs, manifests, history, noteIdHigh);
    const uint32_t sectors = blobSectorsOfSnapshot();
    switch (library::fits(header_, sectors)) {
      case library::Fit::IndexFull: return refuse(Result::IndexFull);
      case library::Fit::LogFull: return refuse(Result::LogFull);
      case library::Fit::TooLarge: return refuse(Result::TooLarge);
      case library::Fit::Ok: break;
    }
    checkpointJob_ = false;
    headerOnly_ = false;
    record_ = library::Record{};
    record_.sequence = header_.nextSequence;
    record_.set = m.set;
    record_.stage = m.stage;
    record_.stageNumber = m.stageNumber;
    record_.parent = m.parent;
    record_.wallTime = m.wallTime;
    record_.tempoTenths = m.tempoTenths;
    record_.loops = snap_.loops();
    record_.shortestLoop = snap_.shortest();
    record_.longestLoop = snap_.longest();
    record_.logSector = header_.appendSector;
    record_.logSectors = sectors;
    for (int i = 0; i < 16; ++i) record_.mosaic[i] = snap_.mosaic()[i];
    start(library::kLogBase + header_.appendSector, record_.sequence, m.tempoTenths, m.surface);
    return Result::Ok;
  }
  /// Starts publishing a trash-side record (DEC-268): `kind` library::kTrash (`target` a milestone's sequence, or 0 for
  /// `set`'s shown milestones so far), kRestore (`target` the trash record's sequence) or kEmptyTrash. Only the record
  /// and the header are written; refused by name when the index is full.
  Result beginMark(uint8_t kind, uint16_t set, uint32_t target, uint32_t wallTime) {
    if (!mounted_) return Result::NotMounted;
    if (busy()) return Result::Busy;
    if (header_.count >= library::kIndexRecords) return Result::IndexFull;
    checkpointJob_ = false;
    headerOnly_ = false;
    record_ = library::Record{};
    record_.sequence = header_.nextSequence;
    record_.set = set;
    record_.kind = kind;
    record_.parent = target;
    record_.wallTime = wallTime;
    phase_ = Phase::Record;
    status_ = Status::Running;
    return Result::Ok;
  }
  /// Starts publishing `set` (base milestone `base`, 0 none) as the working set (DEC-269): the index header alone, so it
  /// needs no index record; a failed write leaves the previous header — and working set — current.
  Result beginWorking(uint16_t set, uint32_t base) {
    if (!mounted_) return Result::NotMounted;
    if (busy()) return Result::Busy;
    checkpointJob_ = false;
    headerOnly_ = true;
    record_ = library::Record{};  // no record written: it only carries the working set to the header
    record_.set = set;
    record_.parent = base;
    phase_ = Phase::Header;
    status_ = Status::Running;
    return Result::Ok;
  }
  /// Captures the set and starts writing it as the checkpoint, into the region that is not current.
  Result beginCheckpoint(Pool& pool, Runs& runs, Manifests& manifests, const HistoryT& history, uint32_t noteIdHigh,
                         const Checkpoint& c, uint8_t* scratch) {
    if (!mounted_) return Result::NotMounted;
    if (busy()) return Result::Busy;
    bind(pool, runs, manifests, scratch);
    snap_.capture(runs, manifests, history, noteIdHigh);
    const uint32_t sectors = blobSectorsOfSnapshot();
    if (sectors > library::kMaxSetSectors) return refuse(Result::TooLarge);
    checkpointJob_ = true;
    headerOnly_ = false;
    target_ = haveCheckpoint_ ? static_cast<uint8_t>(region_ ^ 1) : 0;
    pending_ = library::CheckpointHeader{};
    pending_.sequence = (haveCheckpoint_ ? checkpoint_.sequence : 0) + 1;
    pending_.wallTime = c.wallTime;
    pending_.base = c.base;
    pending_.set = c.set;
    pending_.loaded = c.loaded;
    pending_.sectors = sectors;
    start(library::checkpointBase(target_) + 1, pending_.sequence, c.tempoTenths, c.surface);
    return Result::Ok;
  }

  /// One grain: at most one sector written, only when the card is ready for it.
  Status step() {
    if (status_ != Status::Running) return status_;
    switch (phase_) {
      case Phase::BlobHeader: {
        if (!card_->libraryReady(base_)) return status_;
        uint8_t sector[library::kSector];
        library::encode(blob_, sector);
        if (!card_->writeLibrarySector(base_, sector)) return fail();
        sink_.next = base_ + 1;
        slot_ = 0;
        phase_ = Phase::NextSlot;
        return status_;
      }
      case Phase::NextSlot:
        while (slot_ < Slots && snap_.bytes(slot_) == 0) ++slot_;
        if (slot_ == Slots) {
          phase_ = Phase::WaitBlob;
          return status_;
        }
        sink_.bytes = 0;
        if (save_.startChain(*pool_, *runs_, *manifests_, slot_, snap_.chain(slot_), snap_.count(slot_),
                             snap_.current(slot_), snap_.emptyBelow(slot_), sink_, snap_.noteIdHigh(), scratch_) !=
            Save::Status::Running)
          return fail();
        phase_ = Phase::Body;
        return status_;
      case Phase::Body: {
        if (!card_->libraryReady(sink_.next)) return status_;
        const typename Save::Status st = save_.step();
        if (st == Save::Status::Running) return status_;
        if (st != Save::Status::Done || sink_.bytes != snap_.bytes(slot_)) return fail();  // the size was exact
        phase_ = Phase::Pad;
        return status_;
      }
      case Phase::Pad:
        if (sink_.fill != 0) {
          if (!card_->libraryReady(sink_.next)) return status_;
          for (uint32_t i = sink_.fill; i < library::kSector; ++i) sink_.buf[i] = 0;
          if (!card_->writeLibrarySector(sink_.next++, sink_.buf)) return fail();
          sink_.fill = 0;
        }
        ++slot_;
        phase_ = Phase::NextSlot;
        return status_;
      case Phase::WaitBlob:
        if (!card_->libraryReady(Card::kIdle)) return status_;
        phase_ = checkpointJob_ ? Phase::CheckpointHeader : Phase::Record;
        return status_;
      case Phase::Record: {
        const uint16_t i = header_.count;
        if (!card_->libraryReady(library::recordSector(i))) return status_;
        uint8_t sector[library::kSector];
        if (i % library::kRecordsPerSector == 0) {
          for (auto& x : sector) x = 0;  // a fresh sector: whatever the card held there is not read
        } else if (!card_->readLibrarySector(library::recordSector(i), sector)) {
          return fail();
        }
        library::encode(record_, sector + library::recordOffset(i));
        if (!card_->writeLibrarySector(library::recordSector(i), sector)) return fail();
        phase_ = Phase::WaitRecord;
        return status_;
      }
      case Phase::WaitRecord:
        if (!card_->libraryReady(Card::kIdle)) return status_;
        phase_ = Phase::Header;
        return status_;
      case Phase::Header: {
        const uint8_t other = static_cast<uint8_t>(which_ ^ 1);
        if (!card_->libraryReady(other)) return status_;
        next_ = header_;  // the working set carried, unless this names it
        next_.sequence = header_.sequence + 1;
        if (headerOnly_) {
          next_.hasWorking = true;
          next_.workingSet = record_.set;
          next_.workingBase = record_.parent;
        } else {
          next_.count = static_cast<uint16_t>(header_.count + 1);
          next_.appendSector = header_.appendSector + record_.logSectors;
          next_.nextSequence = header_.nextSequence + 1;
          if (record_.kind == library::kMilestone) {  // a save: its set is the working one, descending from it
            next_.hasWorking = true;
            next_.workingSet = record_.set;
            next_.workingBase = record_.sequence;
          }
        }
        uint8_t sector[library::kSector];
        library::encode(next_, sector);
        if (!card_->writeLibrarySector(other, sector)) return fail();
        phase_ = Phase::WaitHeader;
        return status_;
      }
      case Phase::WaitHeader:
        if (!card_->libraryReady(Card::kIdle)) return status_;
        header_ = next_;  // published
        which_ ^= 1;
        return done();
      case Phase::CheckpointHeader: {
        const uint32_t at = library::checkpointBase(target_);
        if (!card_->libraryReady(at)) return status_;
        uint8_t sector[library::kSector];
        library::encode(pending_, sector);
        if (!card_->writeLibrarySector(at, sector)) return fail();
        phase_ = Phase::WaitCheckpoint;
        return status_;
      }
      case Phase::WaitCheckpoint:
        if (!card_->libraryReady(Card::kIdle)) return status_;
        checkpoint_ = pending_;  // the new checkpoint is the current one
        haveCheckpoint_ = true;
        region_ = target_;
        return done();
    }
    return status_;
  }

 private:
  struct Sink {
    Card* card = nullptr;
    uint8_t buf[library::kSector] = {};
    uint32_t fill = 0, next = 0, bytes = 0;
    bool write(const uint8_t* d, uint32_t n) {
      bytes += n;
      while (n != 0) {
        const uint32_t k = n < library::kSector - fill ? n : library::kSector - fill;
        for (uint32_t i = 0; i < k; ++i) buf[fill + i] = d[i];
        fill += k;
        d += k;
        n -= k;
        if (fill == library::kSector) {
          if (!card->writeLibrarySector(next++, buf)) return false;
          fill = 0;
        }
      }
      return true;
    }
  };
  using Save = SaveJob<Pool, Runs, Manifests, HistoryT, Sink, library::kSector>;
  enum class Phase : uint8_t {
    BlobHeader, NextSlot, Body, Pad, WaitBlob, Record, WaitRecord, Header, WaitHeader, CheckpointHeader, WaitCheckpoint
  };

  void bind(Pool& pool, Runs& runs, Manifests& manifests, uint8_t* scratch) {
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    scratch_ = scratch;
    sink_.card = card_;
  }
  uint32_t blobSectorsOfSnapshot() const {
    uint32_t bytes[library::kSlots] = {};
    for (uint8_t s = 0; s < Slots && s < library::kSlots; ++s) bytes[s] = snap_.bytes(s);
    return library::blobSectors(bytes);
  }
  void start(uint32_t base, uint32_t sequence, uint16_t tempoTenths, const uint8_t* surface) {
    base_ = base;
    blob_ = library::BlobHeader{};
    blob_.sequence = sequence;
    blob_.noteIdHigh = snap_.noteIdHigh();
    blob_.tempoTenths = tempoTenths;
    for (uint8_t i = 0; i < library::kSurfaceBytes; ++i) blob_.surface[i] = surface[i];
    for (uint8_t s = 0; s < Slots && s < library::kSlots; ++s) blob_.slotBytes[s] = snap_.bytes(s);
    sink_.fill = 0;
    phase_ = Phase::BlobHeader;
    status_ = Status::Running;
  }
  Result refuse(Result r) {
    snap_.release(*manifests_, *runs_);
    return r;
  }
  void releaseSnapshot() {  // a trash-side record captured nothing
    if (snap_.held()) snap_.release(*manifests_, *runs_);
  }
  Status fail() {
    save_.cancel();
    releaseSnapshot();
    return status_ = Status::Failed;  // nothing published: the current header / checkpoint stay
  }
  Status done() {
    releaseSnapshot();
    return status_ = Status::Done;
  }

  Card* card_ = nullptr;
  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  uint8_t* scratch_ = nullptr;
  Snapshot snap_;
  Save save_;
  Sink sink_;
  library::IndexHeader header_, next_;
  uint8_t which_ = 1;
  library::CheckpointHeader checkpoint_, pending_;
  bool haveCheckpoint_ = false;
  uint8_t region_ = 1, target_ = 0;
  bool mounted_ = false, checkpointJob_ = false, headerOnly_ = false;
  library::Record record_;
  library::BlobHeader blob_;
  uint32_t base_ = 0;
  uint8_t slot_ = 0;
  Phase phase_ = Phase::BlobHeader;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
