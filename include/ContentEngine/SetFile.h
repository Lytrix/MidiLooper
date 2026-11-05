//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine's slot file (fixed-memory-engine E8.1; owner decision 2026-11-05: a new format in a new folder, V1 files
// imported read-only and never overwritten, nothing written to the card before the owner approves saving).
//
// One file per slot, little-endian:
//   header     magic "MLE1", version, slot, flags (bit 0: the empty slot lies below the oldest state), loop length,
//              run count, state count, current state index (-1: the empty slot), total rows, note-id high-water mark
//   directory  per run: kind, row count
//   rows       per run, its rows (16 B: tick, noteId, seq, status, data1, data2, origin)
//   states     per history state, oldest to newest: revision, loop length, run count, run indices into the directory
//   trailer    CRC-32 of everything before it
// Runs shared by several states are stored once. The file holds what was recorded (runs) and which revisions the slot's
// history selects (states); nothing derived.
//
// `SaveJob` writes a slot in persistence grains of at most `ChunkBytes` (one sink write per grain), pinning the
// manifests it reads until it ends. `LoadJob` is writer-class admission (plan § 5.5): after the directory it reserves
// every row against content quota or refuses by name, then appends the rows in grains, creates the manifests, and checks
// the CRC; only then does it publish the slot's history. Any failure (I/O, format, CRC, capacity) releases everything
// it took. `Sink::write(data, n)` / `Source::read(data, n)` return false on an I/O error. No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/History.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/RowReservations.h"
#include "ContentEngine/RunTable.h"
#include "ContentEngine/Writer.h"

namespace ContentEngine {

constexpr uint32_t kSetFileMagic = 0x31454C4Du;  // "MLE1"
constexpr uint16_t kSetFileVersion = 1;
constexpr uint32_t kSetFileRowBytes = 16;

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, uint32_t n) {
  crc = ~crc;
  for (uint32_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Sink,
          uint32_t ChunkBytes = 16384>
class SaveJob {
 public:
  enum class Status : uint8_t { Idle, Running, Done, IoError };
  static constexpr uint8_t kMaxStates = Config::kHistoryDepth;

  /// Starts saving `slot`. Pins every manifest of the slot's history until the job ends.
  Status start(Pool& pool, Runs& runs, Manifests& manifests, const HistoryT& history, uint8_t slot, Sink& sink,
               uint32_t noteIdHigh, uint8_t* scratch) {
    cancel();
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    sink_ = &sink;
    scratch_ = scratch;
    slot_ = slot;
    noteIdHigh_ = noteIdHigh;
    states_ = history.chain(slot, state_, kMaxStates, current_, emptyBelow_);
    for (uint8_t i = 0; i < states_; ++i) manifests.pin(state_[i]);
    // The directory: each run once, in first-use order.
    for (auto& x : index_) x = kNoIndex;
    runCount_ = 0;
    totalRows_ = 0;
    loop_ = states_ ? manifests.manifest(state_[0]).loopLengthTicks : 0;
    for (uint8_t i = 0; i < states_; ++i) {
      const auto& m = manifests.manifest(state_[i]);
      for (uint8_t k = 0; k < m.runCount; ++k)
        if (index_[m.runs[k]] == kNoIndex) {
          index_[m.runs[k]] = runCount_;
          run_[runCount_++] = m.runs[k];
          totalRows_ += runs.run(m.runs[k]).rowCount;
        }
    }
    crc_ = 0;
    used_ = 0;
    phase_ = Phase::Header;
    r_ = 0;
    row_ = 0;
    status_ = Status::Running;
    return status_;
  }

  /// One grain: fills the scratch (at most ChunkBytes) and writes it once.
  Status step() {
    if (status_ != Status::Running) return status_;
    used_ = 0;
    while (used_ + kRecordMax <= ChunkBytes && phase_ != Phase::Finished) produce();
    if (phase_ == Phase::Finished) {
      put32(crc32Update(crc_, scratch_, used_));  // the trailer: the CRC of everything before it
    } else {
      crc_ = crc32Update(crc_, scratch_, used_);
    }
    if (!sink_->write(scratch_, used_)) return end(Status::IoError);
    return phase_ == Phase::Finished ? end(Status::Done) : status_;
  }

  void cancel() {
    if (status_ == Status::Running) end(Status::Idle);
  }
  Status status() const { return status_; }
  uint32_t totalRows() const { return totalRows_; }

 private:
  enum class Phase : uint8_t { Header, Directory, Rows, States, Finished };
  static constexpr uint16_t kNoIndex = 0xFFFF;
  static constexpr uint32_t kRecordMax = 256;  // the largest record (a state: 9 B + 63 run indices x 2 B) + the trailer
  static_assert(ChunkBytes >= 2 * kRecordMax, "a chunk holds at least one record");

  void produce() {
    switch (phase_) {
      case Phase::Header:
        put32(kSetFileMagic);
        put16(kSetFileVersion);
        put8(slot_);
        put8(emptyBelow_ ? 1 : 0);
        put32(loop_);
        put16(runCount_);
        put8(states_);
        put8(static_cast<uint8_t>(static_cast<int8_t>(current_)));
        put32(totalRows_);
        put32(noteIdHigh_);
        phase_ = Phase::Directory;
        break;
      case Phase::Directory:
        if (r_ == runCount_) {
          phase_ = Phase::Rows;
          r_ = 0;
          row_ = 0;
          break;
        }
        put8(runs_->run(run_[r_]).kind);
        put8(0);
        put16(0);
        put32(runs_->run(run_[r_]).rowCount);
        ++r_;
        break;
      case Phase::Rows: {
        if (r_ == runCount_) {
          phase_ = Phase::States;
          r_ = 0;
          break;
        }
        if (row_ == runs_->run(run_[r_]).rowCount) {
          ++r_;
          row_ = 0;
          break;
        }
        const PassEvent& e = runs_->rowAt(run_[r_], row_++, *pool_);
        put32(e.tick);
        put32(e.noteId);
        put32(e.seq);
        put8(e.status);
        put8(e.data1);
        put8(e.data2);
        put8(e.origin);
        break;
      }
      case Phase::States: {
        if (r_ == states_) {
          phase_ = Phase::Finished;
          break;
        }
        const auto& m = manifests_->manifest(state_[r_++]);
        put32(m.revision);
        put32(m.loopLengthTicks);
        put8(m.runCount);
        for (uint8_t k = 0; k < m.runCount; ++k) put16(index_[m.runs[k]]);
        break;
      }
      case Phase::Finished:
        break;
    }
  }

  void put8(uint8_t v) { scratch_[used_++] = v; }
  void put16(uint16_t v) {
    put8(static_cast<uint8_t>(v));
    put8(static_cast<uint8_t>(v >> 8));
  }
  void put32(uint32_t v) {
    put16(static_cast<uint16_t>(v));
    put16(static_cast<uint16_t>(v >> 16));
  }

  Status end(Status s) {
    for (uint8_t i = 0; i < states_; ++i) manifests_->unpin(state_[i], *runs_);
    states_ = 0;
    return status_ = s;
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  Sink* sink_ = nullptr;
  uint8_t* scratch_ = nullptr;
  uint8_t slot_ = 0, states_ = 0;
  int16_t current_ = -1;
  bool emptyBelow_ = true;
  uint32_t noteIdHigh_ = 0, loop_ = 0, totalRows_ = 0, crc_ = 0, used_ = 0, row_ = 0;
  uint16_t runCount_ = 0, r_ = 0;
  Phase phase_ = Phase::Header;
  ManifestId state_[kMaxStates];
  RunId run_[Runs::kRunCount];
  uint16_t index_[Runs::kRunCount];
  Status status_ = Status::Idle;
};

template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Source,
          uint32_t ChunkBytes = 16384>
class LoadJob {
 public:
  enum class Status : uint8_t { Idle, Running, Done, IoError, BadFormat, BadCrc, DeniedCapacity, DeniedSlot };
  static constexpr uint8_t kMaxStates = Config::kHistoryDepth;
  using Reservations = RowReservations<Pool, Runs>;

  /// Starts loading into `slot`, which must have no history. `scratch` holds ChunkBytes.
  Status start(Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res, NoteIdSource& ids,
               uint8_t slot, Source& source, uint8_t* scratch) {
    cancel();
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    res_ = &res;
    ids_ = &ids;
    slot_ = slot;
    source_ = &source;
    scratch_ = scratch;
    crc_ = 0;
    runCount_ = 0;
    made_ = 0;
    r_ = 0;
    row_ = 0;
    phase_ = Phase::Header;
    ManifestId probe[1];
    int16_t cur;
    bool below;
    if (history.chain(slot, probe, 1, cur, below) != 0) return status_ = Status::DeniedSlot;
    return status_ = Status::Running;
  }

  /// One grain: the header and directory (then admission), or up to ChunkBytes of rows, or the states, or the check.
  Status step() {
    if (status_ != Status::Running) return status_;
    switch (phase_) {
      case Phase::Header: {
        uint8_t h[24];
        if (!read(h, sizeof h)) return fail(Status::IoError);
        if (get32(h) != kSetFileMagic || get16(h + 4) != kSetFileVersion) return fail(Status::BadFormat);
        emptyBelow_ = (h[7] & 1) != 0;
        loop_ = get32(h + 8);
        runCount_ = get16(h + 12);
        states_ = h[14];
        current_ = static_cast<int8_t>(h[15]);
        noteIdHigh_ = get32(h + 20);
        if (runCount_ > Runs::kRunCount || states_ > kMaxStates) return fail(Status::BadFormat);
        if (runCount_ > runs_->freeCount() || states_ > manifests_->freeCount()) return fail(Status::DeniedCapacity);
        for (uint16_t i = 0; i < runCount_; ++i) {
          uint8_t d[8];
          if (!read(d, sizeof d)) return fail(Status::IoError);
          const uint32_t rows = get32(d + 4);
          run_[i] = runs_->allocate(slot_, d[0]);
          rows_[i] = rows;
          // Writer-class admission: every row of the file is reserved before any is read.
          if (res_->reserve(run_[i], rows) != Admission::Ok) {
            runCount_ = static_cast<uint16_t>(i + 1);
            return fail(Status::DeniedCapacity);
          }
        }
        phase_ = Phase::Rows;
        return status_;
      }
      case Phase::Rows: {
        uint32_t budget = ChunkBytes / kSetFileRowBytes;
        while (budget > 0 && r_ < runCount_) {
          if (row_ == rows_[r_]) {
            ++r_;
            row_ = 0;
            continue;
          }
          uint32_t n = rows_[r_] - row_;
          if (n > budget) n = budget;
          if (!read(scratch_, n * kSetFileRowBytes)) return fail(Status::IoError);
          for (uint32_t k = 0; k < n; ++k) {
            const uint8_t* b = scratch_ + k * kSetFileRowBytes;
            PassEvent e{};
            e.tick = get32(b);
            e.noteId = get32(b + 4);
            e.seq = get32(b + 8);
            e.status = b[12];
            e.data1 = b[13];
            e.data2 = b[14];
            e.origin = b[15];
            res_->writeOwed(run_[r_], e);
          }
          row_ += n;
          budget -= n;
        }
        if (r_ == runCount_) phase_ = Phase::States;
        return status_;
      }
      case Phase::States: {
        for (uint16_t k = 0; k < runCount_; ++k) runs_->seal(run_[k]);  // every row is in: the runs are immutable
        for (uint8_t i = 0; i < states_; ++i) {
          uint8_t h[9];
          if (!read(h, sizeof h)) return fail(Status::IoError);
          const uint8_t count = h[8];
          if (count > Manifests::kMaxRuns) return fail(Status::BadFormat);
          RunId list[Manifests::kMaxRuns];
          for (uint8_t k = 0; k < count; ++k) {
            uint8_t ix[2];
            if (!read(ix, 2)) return fail(Status::IoError);
            const uint16_t index = get16(ix);
            if (index >= runCount_) return fail(Status::BadFormat);
            list[k] = run_[index];
          }
          const ManifestId m = manifests_->create(slot_, get32(h), get32(h + 4), list, count, *runs_);
          if (m == kNoManifest) return fail(Status::DeniedCapacity);
          made_list_[made_++] = m;
        }
        phase_ = Phase::Check;
        return status_;
      }
      case Phase::Check: {
        const uint32_t computed = crc_;
        uint8_t t[4];
        if (!source_->read(t, 4)) return fail(Status::IoError);
        if (get32(t) != computed) return fail(Status::BadCrc);
        for (uint16_t k = 0; k < runCount_; ++k) runs_->release(run_[k]);  // the manifests hold the runs now
        runCount_ = 0;
        if (!history_->restoreSlot(slot_, made_list_, made_, current_, emptyBelow_)) {
          for (uint8_t i = 0; i < made_; ++i) manifests_->release(made_list_[i], *runs_);
          made_ = 0;
          return status_ = Status::BadFormat;
        }
        made_ = 0;
        while (ids_->peek() <= noteIdHigh_) ids_->advance();  // ids are never reused
        return status_ = Status::Done;
      }
    }
    return status_;
  }

  void cancel() {
    if (status_ == Status::Running) fail(Status::Idle);
  }
  Status status() const { return status_; }

 private:
  enum class Phase : uint8_t { Header, Rows, States, Check };

  bool read(uint8_t* data, uint32_t n) {
    if (!source_->read(data, n)) return false;
    crc_ = crc32Update(crc_, data, n);
    return true;
  }
  static uint16_t get16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
  static uint32_t get32(const uint8_t* b) { return get16(b) | (static_cast<uint32_t>(get16(b + 2)) << 16); }

  /// Releases everything taken: created manifests, the runs (their rows reach reclaim) and their reservations.
  Status fail(Status s) {
    for (uint8_t i = 0; i < made_; ++i) manifests_->release(made_list_[i], *runs_);
    made_ = 0;
    for (uint16_t k = 0; k < runCount_; ++k) {
      if (run_[k] == kNoRun) continue;
      res_->forgive(run_[k]);
      runs_->seal(run_[k]);
      runs_->release(run_[k]);
    }
    runCount_ = 0;
    return status_ = s;
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  Reservations* res_ = nullptr;
  NoteIdSource* ids_ = nullptr;
  Source* source_ = nullptr;
  uint8_t* scratch_ = nullptr;
  uint8_t slot_ = 0, states_ = 0, made_ = 0;
  int16_t current_ = -1;
  bool emptyBelow_ = true;
  uint32_t loop_ = 0, crc_ = 0, noteIdHigh_ = 0, row_ = 0;
  uint16_t runCount_ = 0, r_ = 0;
  Phase phase_ = Phase::Header;
  RunId run_[Runs::kRunCount];
  uint32_t rows_[Runs::kRunCount];
  ManifestId made_list_[kMaxStates];
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
