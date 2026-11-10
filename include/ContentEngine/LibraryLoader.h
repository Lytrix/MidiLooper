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
// * Get a loop (DEC-270): `StateReader` reads one slot of a milestone for another slot — its current state only, the
//   caller's to place (`takeGot`); no checkpoint, no clear, no history touched; a failed read or an empty source slot
//   changes nothing (`Unchanged`).
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

  /// Starts loading the set whose header is library sector `at`, written as sequence `sequence`: the slots in `slots`
  /// (bit s), or — `detached` (DEC-261) — only their current states, as manifests the caller owns (`detachedState`).
  void start(Card& card, Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res,
             NoteIdSource& ids, uint32_t at, uint32_t sequence, uint8_t* scratch, uint64_t slots = ~0ull,
             bool detached = false, const uint8_t* adoptSlot = nullptr, const ManifestId* adoptState = nullptr,
             uint8_t adoptCount = 0) {
    load_.cancel();
    adoptSlot_ = adoptSlot;
    adoptState_ = adoptState;
    adoptCount_ = adoptCount;
    slots_ = slots;
    detached_ = detached;
    for (auto& m : detachedState_) m = kNoManifest;
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
      if (detached_) detachedState_[slot_] = load_.detachedState();
      ++loaded_;
      ++slot_;
      return status_;
    }
    while (slot_ < Slots && slot_ < library::kSlots && (blob_.slotBytes[slot_] == 0 || !((slots_ >> slot_) & 1))) {
      slotAt_ += (blob_.slotBytes[slot_] + library::kSector - 1) / library::kSector;  // a skipped slot's sectors
      ++slot_;
    }
    if (slot_ >= Slots || slot_ >= library::kSlots) return status_ = Status::Done;
    next_ = slotAt_;  // each slot's file starts on its own sector
    slotAt_ += (blob_.slotBytes[slot_] + library::kSector - 1) / library::kSector;
    left_ = blob_.slotBytes[slot_];
    fill_ = library::kSector;  // nothing buffered: the first read takes the slot's first sector
    source_.r = this;
    ManifestId adopt = kNoManifest;  // a staged state for this slot (DEC-261)
    for (uint8_t k = 0; k < adoptCount_; ++k)
      if (adoptSlot_[k] == slot_) adopt = adoptState_[k];
    if (load_.start(*pool_, *runs_, *manifests_, *history_, *res_, *ids_, slot_, source_, scratch_, detached_, adopt) !=
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
  /// A detached load's state for `slot` (one reference, the caller's; kNoManifest: none).
  ManifestId detachedState(uint8_t slot) const { return slot < Slots ? detachedState_[slot] : kNoManifest; }

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
  bool header_ = false, detached_ = false;
  uint64_t slots_ = ~0ull;
  ManifestId detachedState_[Slots] = {};
  const uint8_t* adoptSlot_ = nullptr;
  const ManifestId* adoptState_ = nullptr;
  uint8_t adoptCount_ = 0;
  Why why_ = Why::None;
  Status status_ = Status::Idle;
};

/// Get a loop (DEC-270; plan §6.2 fix 1: a read must not take what live takes rely on): one slot file of a set written
/// by `LibraryWriter`, its current state made for slot `into` — only the runs that state lists — detached (one
/// reference, the caller's: `take`). Pass 1 streams the whole file once for its CRC, keeping the run directory and the
/// current state's run list, no rows; pass 2 reads just those runs' rows (admitted before any is read, written as owed
/// rows). A bad file, an empty current state or no room fails with nothing left taken. At most `kGrainBytes` read a
/// grain. No heap.
template <typename Card, typename Pool, typename Runs, typename Manifests>
class StateReader {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Failed };
  enum class Why : uint8_t { None, Read, BadBlob, BadFile, Empty, Capacity };
  using Reservations = RowReservations<Pool, Runs>;
  static constexpr uint32_t kGrainBytes = 2048;

  /// Starts reading slot `from` of the set whose header is library sector `at` (written as `sequence`) for `into`.
  /// `preview` (plan §6.2 fix 1): into preview runs, within the preview partition — larger fails `Capacity`.
  void start(Card& card, Pool& pool, Runs& runs, Manifests& manifests, Reservations& res, NoteIdSource& ids,
             uint32_t at, uint32_t sequence, uint8_t from, uint8_t into, bool preview = false) {
    cancel();
    if (got_ != kNoManifest) manifests.release(got_, runs);
    got_ = kNoManifest;
    card_ = &card;
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    res_ = &res;
    ids_ = &ids;
    at_ = at;
    sequence_ = sequence;
    from_ = from;
    into_ = into;
    preview_ = preview;
    taken_ = 0;
    cached_ = kNoSector;
    why_ = Why::None;
    phase_ = Phase::Blob;
    status_ = Status::Running;
  }
  /// One grain.
  Status step() {
    if (status_ != Status::Running) return status_;
    switch (phase_) {
      case Phase::Blob: {
        uint8_t h[library::kSector];
        library::BlobHeader b;
        if (!card_->readLibrarySector(at_, h)) return fail(Why::Read);
        if (!library::decode(h, b) || b.sequence != sequence_) return fail(Why::BadBlob);
        if (from_ >= library::kSlots || b.slotBytes[from_] == 0) return fail(Why::Empty);
        base_ = at_ + 1;
        for (uint8_t s = 0; s < from_; ++s) base_ += (b.slotBytes[s] + library::kSector - 1) / library::kSector;
        bytes_ = b.slotBytes[from_];
        pos_ = 0;
        crc_ = 0;
        uint8_t f[24];
        if (bytes_ < sizeof f + 4 || !read(f, sizeof f, true)) return fail(Why::BadFile);
        if (get32(f) != kSetFileMagic || get16(f + 4) < kSetFileOldestVersion || get16(f + 4) > kSetFileVersion)
          return fail(Why::BadFile);
        legacyFit_ = get16(f + 4) < 4;
        runCount_ = get16(f + 12);
        states_ = f[14];
        current_ = static_cast<int8_t>(f[15]);
        noteIdHigh_ = get32(f + 20);
        if (runCount_ > Runs::kRunCount) return fail(Why::BadFile);
        r_ = 0;
        rowsTotal_ = 0;
        phase_ = Phase::Directory;
        return status_;
      }
      case Phase::Directory:
        for (uint32_t n = 0; n < kGrainBytes / 8 && r_ < runCount_; ++n, ++r_) {
          uint8_t d[8];
          if (!read(d, sizeof d, true)) return fail(Why::BadFile);
          kind_[r_] = d[0];
          rows_[r_] = get32(d + 4);
          rowsTotal_ += rows_[r_];
        }
        if (r_ < runCount_) return status_;
        rowsAt_ = pos_;
        if (static_cast<uint64_t>(rowsAt_) + rowsTotal_ * kSetFileRowBytes + 4 > bytes_) return fail(Why::BadFile);
        rowsEnd_ = rowsAt_ + static_cast<uint32_t>(rowsTotal_) * kSetFileRowBytes;
        phase_ = Phase::Skip;
        return status_;
      case Phase::Skip: {  // pass 1: every row into the CRC, none kept
        uint32_t budget = kGrainBytes;
        while (budget != 0 && pos_ < rowsEnd_) {
          const uint32_t off = pos_ % library::kSector;
          uint32_t span = library::kSector - off;
          if (span > rowsEnd_ - pos_) span = rowsEnd_ - pos_;
          if (span > budget) span = budget;
          if (!fill(pos_)) return fail(Why::Read);
          crc_ = crc32Update(crc_, sector_ + off, span);
          pos_ += span;
          budget -= span;
        }
        if (pos_ < rowsEnd_) return status_;
        i_ = 0;
        count_ = 0;
        phase_ = Phase::States;
        return status_;
      }
      case Phase::States: {
        for (; i_ < states_; ++i_) {
          uint8_t h[9];
          if (!read(h, sizeof h, true)) return fail(Why::BadFile);
          const uint8_t count = h[8];
          if (count > Manifests::kMaxRuns) return fail(Why::BadFile);
          uint16_t list[Manifests::kMaxRuns];
          for (uint8_t k = 0; k < count; ++k) {
            uint8_t ix[2];
            if (!read(ix, 2, true)) return fail(Why::BadFile);
            list[k] = get16(ix);
            if (list[k] >= runCount_) return fail(Why::BadFile);
          }
          if (i_ != current_) continue;
          revision_ = get32(h);
          loop_ = get32(h + 4);
          count_ = count;
          for (uint8_t k = 0; k < count; ++k) list_[k] = list[k];
        }
        uint8_t t[4];
        if (!read(t, sizeof t, false) || get32(t) != crc_) return fail(Why::BadFile);
        if (current_ < 0 || current_ >= static_cast<int8_t>(states_)) return fail(Why::Empty);
        phase_ = Phase::Admit;
        return status_;
      }
      case Phase::Admit: {  // only the current state's runs: allocated, every row reserved before any is read
        if (count_ > (preview_ ? runs_->previewFreeCount() : runs_->freeCount()) || manifests_->freeCount() == 0)
          return fail(Why::Capacity);
        uint32_t at = rowsAt_;
        for (uint16_t j = 0; j < runCount_; ++j) {
          for (uint8_t k = 0; k < count_; ++k)
            if (list_[k] == j) off_[k] = at;
          at += rows_[j] * kSetFileRowBytes;
        }
        for (uint8_t k = 0; k < count_; ++k) {
          run_[k] = preview_ ? runs_->allocatePreview(into_, kind_[list_[k]]) : runs_->allocate(into_, kind_[list_[k]]);
          if (run_[k] == kNoRun) return fail(Why::Capacity);
          taken_ = static_cast<uint8_t>(k + 1);
          if (res_->reserve(run_[k], rows_[list_[k]]) != Admission::Ok) return fail(Why::Capacity);
        }
        k_ = 0;
        row_ = 0;
        phase_ = Phase::Rows;
        return status_;
      }
      case Phase::Rows: {  // pass 2: the listed runs' rows (pass 1 checked them)
        uint32_t budget = kGrainBytes / kSetFileRowBytes;
        while (budget != 0 && k_ < count_) {
          if (row_ == rows_[list_[k_]]) {
            ++k_;
            row_ = 0;
            continue;
          }
          pos_ = off_[k_] + row_ * kSetFileRowBytes;
          uint8_t b[kSetFileRowBytes];
          if (!read(b, sizeof b, false)) return fail(Why::Read);
          PassEvent e{};
          e.tick = get32(b);
          e.noteId = get32(b + 4);
          e.seq = get32(b + 8);
          e.status = b[12];
          e.data1 = b[13];
          e.data2 = b[14];
          e.origin = b[15];
          ++row_;
          --budget;
          if (legacyFitRow(legacyFit_, runs_->run(run_[k_]).kind, e.status)) continue;  // DEC-285: forgiven at the seal
          if (!res_->writeOwed(run_[k_], e)) return fail(Why::BadFile);
        }
        if (k_ < count_) return status_;
        for (uint8_t k = 0; k < count_; ++k) {
          res_->forgive(run_[k]);  // version-3 fit lookups not loaded (DEC-285)
          runs_->seal(run_[k]);
        }
        const ManifestId m = manifests_->create(into_, revision_, loop_, run_, count_, *runs_);
        if (m == kNoManifest) return fail(Why::Capacity);
        for (uint8_t k = 0; k < count_; ++k) runs_->release(run_[k]);  // the state holds them now
        taken_ = 0;
        got_ = m;
        while (ids_->peek() <= noteIdHigh_) ids_->advance();  // ids are never reused
        return status_ = Status::Done;
      }
    }
    return status_;
  }
  void cancel() {
    if (status_ == Status::Running) fail(Why::None);
    status_ = Status::Idle;
  }
  Status status() const { return status_; }
  Why why() const { return why_; }
  /// The state read (one reference, now the caller's), once; kNoManifest: none.
  ManifestId take() {
    const ManifestId m = got_;
    got_ = kNoManifest;
    return m;
  }

 private:
  enum class Phase : uint8_t { Blob, Directory, Skip, States, Admit, Rows };
  static constexpr uint32_t kNoSector = 0xFFFFFFFFu;

  /// The sector holding byte `pos` of the slot file, in `sector_`.
  bool fill(uint32_t pos) {
    const uint32_t s = base_ + pos / library::kSector;
    if (s == cached_) return true;
    if (!card_->readLibrarySector(s, sector_)) return false;
    cached_ = s;
    return true;
  }
  bool read(uint8_t* d, uint32_t n, bool crc) {
    if (pos_ + n > bytes_) return false;
    for (uint32_t i = 0; i < n; ++i, ++pos_) {
      if (!fill(pos_)) return false;
      d[i] = sector_[pos_ % library::kSector];
    }
    if (crc) crc_ = crc32Update(crc_, d, n);
    return true;
  }
  /// Releases everything taken: the runs (their rows reach reclaim) and their reservations.
  Status fail(Why w) {
    for (uint8_t k = 0; k < taken_; ++k) {
      res_->forgive(run_[k]);
      runs_->seal(run_[k]);
      runs_->release(run_[k]);
    }
    taken_ = 0;
    why_ = w;
    return status_ = Status::Failed;
  }
  static uint16_t get16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
  static uint32_t get32(const uint8_t* b) { return get16(b) | (static_cast<uint32_t>(get16(b + 2)) << 16); }

  Card* card_ = nullptr;
  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  Reservations* res_ = nullptr;
  NoteIdSource* ids_ = nullptr;
  uint8_t sector_[library::kSector] = {};
  uint32_t at_ = 0, sequence_ = 0, base_ = 0, bytes_ = 0, pos_ = 0, crc_ = 0, cached_ = kNoSector;
  uint32_t rowsAt_ = 0, rowsEnd_ = 0, noteIdHigh_ = 0, revision_ = 0, loop_ = 0, row_ = 0;
  uint64_t rowsTotal_ = 0;
  uint16_t runCount_ = 0, r_ = 0;
  uint8_t states_ = 0, i_ = 0, count_ = 0, k_ = 0, taken_ = 0, from_ = 0, into_ = 0;
  int8_t current_ = -1;
  bool preview_ = false;
  uint8_t kind_[Runs::kRunCount] = {};
  uint32_t rows_[Runs::kRunCount] = {};
  uint16_t list_[Manifests::kMaxRuns] = {};
  uint32_t off_[Manifests::kMaxRuns] = {};
  RunId run_[Manifests::kMaxRuns] = {};
  ManifestId got_ = kNoManifest;
  Why why_ = Why::None;
  Phase phase_ = Phase::Blob;
  Status status_ = Status::Idle;
  bool legacyFit_ = false;  // a version-3 slot file (DEC-285); in the class's tail padding
};

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class SetLoad {
 public:
  using Writer = LibraryWriter<Card, Pool, Runs, Manifests, HistoryT, Slots>;
  using Reader = BlobReader<Card, Pool, Runs, Manifests, HistoryT, Slots>;
  using Reservations = RowReservations<Pool, Runs>;
  enum class Kind : uint8_t { Load, Return, Get };
  enum class Result : uint8_t { Ok, NotMounted, Busy, NoMilestone, NoCheckpoint, TooLarge, NoSlot };
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
  /// `atSwitch` (a live load, DEC-260): after the checkpoint the load waits for `release` — the switch point — before
  /// anything is cleared. `stageTracks` (bit t, DEC-261): for each, the milestone's selected slot of that track is
  /// staged first — its current state loaded beside the playing set (`staged`), for a switch without a gap.
  Result beginLoad(uint16_t index, typename Writer::Checkpoint c, bool atSwitch = false, uint8_t stageTracks = 0) {
    if (!writer_->mounted()) return Result::NotMounted;
    if (busy() || writer_->busy()) return Result::Busy;
    if (!writer_->readRecord(index, record_) || record_.kind != library::kMilestone) return Result::NoMilestone;
    c.loaded = record_.sequence;
    const typename Writer::Result r =
        writer_->beginCheckpoint(*pool_, *runs_, *manifests_, *history_, ids_->peek(), c, scratch_);
    if (r == Writer::Result::TooLarge) return Result::TooLarge;
    if (r != Writer::Result::Ok) return Result::Busy;
    kind_ = Kind::Load;
    phase_ = Phase::Checkpoint;
    hold_ = atSwitch;
    stageTracks_ = atSwitch ? stageTracks : 0;
    return start();
  }
  /// Get a loop (DEC-270): starts reading slot `from` of milestone `index` for slot `into` — its current state only,
  /// detached (`takeGot` once Done). Unchanged: the read failed or the slot was empty there; nothing changed.
  /// `preview` (plan §6.2 fix 1): into the preview partition (a picker's preview, never kept as it is).
  Result beginGet(uint16_t index, uint8_t from, uint8_t into, bool preview = false) {
    if (!writer_->mounted()) return Result::NotMounted;
    if (busy() || writer_->busy()) return Result::Busy;
    if (from >= Slots || into >= Slots || from >= library::kSlots) return Result::NoSlot;
    if (!writer_->readRecord(index, record_) || record_.kind != library::kMilestone) return Result::NoMilestone;
    kind_ = Kind::Get;
    getFrom_ = from;
    hold_ = false;
    stageTracks_ = 0;
    start();
    getter_.start(*card_, *pool_, *runs_, *manifests_, *res_, *ids_, setAt(), setSequence(), from, into, preview);
    phase_ = Phase::Get;
    return Result::Ok;
  }
  /// The state a get read (one reference, now the caller's), once; kNoManifest: none.
  ManifestId takeGot() { return getter_.take(); }
  /// Why a get changed nothing.
  typename StateReader<Card, Pool, Runs, Manifests>::Why getWhy() const { return getter_.why(); }
  /// Starts returning to the checkpoint (the working set before the last load).
  Result beginReturn(bool atSwitch = false, uint8_t stageTracks = 0) {
    if (!writer_->mounted()) return Result::NotMounted;
    if (busy() || writer_->busy()) return Result::Busy;
    if (!writer_->checkpoint(checkpoint_)) return Result::NoCheckpoint;
    kind_ = Kind::Return;
    hold_ = atSwitch;
    stageTracks_ = atSwitch ? stageTracks : 0;
    phase_ = stageTracks_ != 0 ? Phase::Stage : Phase::Clear;
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
        phase_ = stageTracks_ != 0 ? Phase::Stage : Phase::Clear;
        return status_;
      }
      case Phase::Stage:
        return stage();
      case Phase::Clear:
        if (hold_) return status_;  // until the switch point
        if (!clear_(ctx_)) return status_ = (kind_ == Kind::Load ? Status::Unchanged : Status::Lost);
        read(setAt(), setSequence());
        return status_;
      case Phase::Reclaim:
        // The cleared set's runs back in the pool before the load admits its rows (its pages go back in grains).
        if (runs_->reclaimPendingRuns() != 0) {
          runs_->reclaim(*pool_, kReclaimUnits);
          return status_;
        }
        // The staged states become their slots' current ones (not on a restore: that loads the checkpoint).
        reader_.start(*card_, *pool_, *runs_, *manifests_, *history_, *res_, *ids_, readAt_, readSequence_, scratch_, ~0ull,
                      false, stageSlot_, staged_, restoring_ ? 0 : kTracks);
        phase_ = Phase::Read;
        return status_;
      case Phase::Get: {  // nothing else is touched: a failure leaves the set as it is
        const auto st = getter_.step();
        if (st == StateReader<Card, Pool, Runs, Manifests>::Status::Running) return status_;
        return status_ = st == StateReader<Card, Pool, Runs, Manifests>::Status::Done ? Status::Done
                                                                                       : Status::Unchanged;
      }
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
  /// DEC-261: track `t`'s staged content once staging ended (kNoManifest: none) and its slot; the caller launches it
  /// and, when the load has ended, gives the references back (`releaseStaged`).
  ManifestId staged(uint8_t t) const { return t < kTracks ? staged_[t] : kNoManifest; }
  uint8_t stagedSlot(uint8_t t) const { return t < kTracks ? stageSlot_[t] : kNoSlot; }
  bool stagingDone() const { return !(status_ == Status::Running && (phase_ == Phase::Checkpoint || phase_ == Phase::Stage)); }
  void releaseStaged() {
    for (uint8_t t = 0; t < kTracks; ++t) {
      if (staged_[t] != kNoManifest) manifests_->release(staged_[t], *runs_);
      staged_[t] = kNoManifest;
      stageSlot_[t] = kNoSlot;
    }
  }
  /// Ready to clear, waiting for the switch point (`release`).
  bool waiting() const { return status_ == Status::Running && phase_ == Phase::Clear && hold_; }
  void release() { hold_ = false; }
  /// Diagnostics: 0 checkpoint, 1 stage, 2 clear (or waiting), 3 reclaim, 4 read, 5 get; a restore reads again.
  uint8_t phase() const { return static_cast<uint8_t>(phase_); }
  Status status() const { return status_; }
  Kind kind() const { return kind_; }
  /// The milestone being loaded (or got from), and the checkpoint written before it (or returned to).
  const library::Record& record() const { return record_; }
  const library::CheckpointHeader& checkpoint() const { return checkpoint_; }
  /// The set that was read last: its tempo and surface state (the milestone's, or the checkpoint's after a restore).
  const library::BlobHeader& blob() const { return reader_.blob(); }
  /// Why a load was restored: the reader's reason and slot.
  typename Reader::Why failedWhy() const { return failedWhy_; }
  uint8_t failedSlot() const { return failedSlot_; }

 private:
  enum class Phase : uint8_t { Checkpoint, Stage, Clear, Reclaim, Read, Get };
  static constexpr uint8_t kTracks = 8, kNoSlot = 0xFF;
  static constexpr uint32_t kReclaimUnits = 64;  // as the reclaim job's grain

  /// The set this operation reads: the milestone's blob (a load or a get), or the checkpoint's (a return, a restore).
  uint32_t setAt() const {
    return kind_ != Kind::Return && !restoring_ ? library::kLogBase + record_.logSector
                                                : library::checkpointBase(region()) + 1;
  }
  uint32_t setSequence() const {
    return kind_ != Kind::Return && !restoring_ ? record_.sequence : checkpoint_.sequence;
  }
  /// One staging grain (DEC-261): the set's header (which slot each staged track selects), then those slots' current
  /// states, detached. Staging that fails leaves nothing staged: those tracks take the gap.
  Status stage() {
    if (!stageStarted_) {
      stageStarted_ = true;
      uint8_t h[library::kSector];
      library::BlobHeader b;
      if (!card_->readLibrarySector(setAt(), h) || !library::decode(h, b) || b.sequence != setSequence()) {
        phase_ = Phase::Clear;
        return status_;
      }
      uint64_t mask = 0;
      for (uint8_t t = 0; t < kTracks; ++t) {
        const uint8_t sel = b.surface[1 + t];
        if (!((stageTracks_ >> t) & 1) || sel / 8 != t || sel >= Slots || b.slotBytes[sel] == 0) continue;
        stageSlot_[t] = sel;
        mask |= 1ull << sel;
      }
      if (mask == 0) {
        phase_ = Phase::Clear;
        return status_;
      }
      reader_.start(*card_, *pool_, *runs_, *manifests_, *history_, *res_, *ids_, setAt(), setSequence(), scratch_, mask,
                    true);
      return status_;
    }
    const typename Reader::Status st = reader_.step();
    if (st == Reader::Status::Running) return status_;
    for (uint8_t t = 0; t < kTracks; ++t) {
      if (stageSlot_[t] == kNoSlot) continue;
      const ManifestId m = reader_.detachedState(stageSlot_[t]);
      if (st == Reader::Status::Done && m != kNoManifest) {
        staged_[t] = m;
        continue;
      }
      if (m != kNoManifest) manifests_->release(m, *runs_);
      stageSlot_[t] = kNoSlot;
    }
    phase_ = Phase::Clear;
    return status_;
  }

  Result start() {
    stageStarted_ = false;
    for (uint8_t t = 0; t < kTracks; ++t) {
      staged_[t] = kNoManifest;
      stageSlot_[t] = kNoSlot;
    }
    restoring_ = false;
    failedWhy_ = Reader::Why::None;
    failedSlot_ = 0;
    status_ = Status::Running;
    return Result::Ok;
  }
  /// Reads the set at `at` once the cleared set is reclaimed.
  void read(uint32_t at, uint32_t sequence) {
    readAt_ = at;
    readSequence_ = sequence;
    phase_ = Phase::Reclaim;
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
  bool restoring_ = false, hold_ = false, stageStarted_ = false;
  uint8_t stageTracks_ = 0;
  uint8_t stageSlot_[kTracks] = {};
  ManifestId staged_[kTracks] = {};
  uint32_t readAt_ = 0, readSequence_ = 0;
  StateReader<Card, Pool, Runs, Manifests> getter_;
  uint8_t getFrom_ = 0;
  typename Reader::Why failedWhy_ = Reader::Why::None;
  uint8_t failedSlot_ = 0;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
