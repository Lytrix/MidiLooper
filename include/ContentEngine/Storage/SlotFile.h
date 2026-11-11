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
#include "ContentEngine/History/SlotUndoHistory.h"
#include "ContentEngine/Content/PassEvent.h"
#include "ContentEngine/Content/RowReservations.h"
#include "ContentEngine/Content/RunTable.h"
#include "ContentEngine/Content/CaptureWriter.h"

namespace ContentEngine {

constexpr uint32_t kSetFileMagic = 0x31454C4Du;  // "MLE1"
// Version 2 (owner 2026-11-08): a note's NoteOn row may carry `kVelocityMuted`. A version-1 file never does, so it
// loads as it is; older engine firmware refuses version 2 by name rather than sending the bit.
// Version 3 (DEC-257): a state may list a fit run (kind `kFit`), lookups that are not content; older firmware would
// compose it as notes.
// Version 4 (DEC-285): a fit run's lookups name a content run by its position in the state's run list, no longer by
// RunId (RunIds are reassigned on load). A version-3 file's lookups name RunIds of the session that saved it: they are
// dropped on load (`legacyFitRow`), its marker rows kept — such a loop plays its notes past a shortened end, as it did,
// until its length is set again.
// Version 5 (DEC-287): a state may list a groove pass (kind `kGroove`) whose earlier runs are its sources, not played;
// older firmware would play both.
constexpr uint16_t kSetFileVersion = 5;
constexpr uint16_t kSetFileOldestVersion = 1;
constexpr uint32_t kSetFileRowBytes = 16;
/// A version-3 fit lookup (filter, cross, held: RunId-keyed, DEC-285): not loaded.
inline bool legacyFitRow(bool legacy, uint8_t kind, uint8_t status) {
  return legacy && kind == RunKind::kFit && status >= kFitFilterStatus && status <= kFitHeldStatus;
}

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
    ManifestId chain[kMaxStates];
    int16_t current;
    bool emptyBelow;
    const uint8_t n = history.chain(slot, chain, kMaxStates, current, emptyBelow);
    for (uint8_t i = 0; i < n; ++i) manifests.pin(chain[i]);
    const Status st = startChain(pool, runs, manifests, slot, chain, n, current, emptyBelow, sink, noteIdHigh, scratch);
    ownPins_ = true;
    return st;
  }
  /// Starts saving `slot` from a captured chain (sets browser 1c-ii, DEC-258: a whole-set snapshot taken at one
  /// instant, whose owner holds the pins) instead of the live history. The job pins nothing itself.
  Status startChain(Pool& pool, Runs& runs, Manifests& manifests, uint8_t slot, const ManifestId* chain, uint8_t n,
                    int16_t current, bool emptyBelow, Sink& sink, uint32_t noteIdHigh, uint8_t* scratch) {
    cancel();
    ownPins_ = false;
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    sink_ = &sink;
    scratch_ = scratch;
    slot_ = slot;
    noteIdHigh_ = noteIdHigh;
    states_ = n > kMaxStates ? kMaxStates : n;
    for (uint8_t i = 0; i < states_; ++i) state_[i] = chain[i];
    current_ = current;
    emptyBelow_ = emptyBelow;
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
  /// The directory's run `i` (first-use order; DEC-254: the run-once store's sink names each run's record by it).
  RunId directoryRun(uint16_t i) const { return run_[i]; }
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
    if (ownPins_)
      for (uint8_t i = 0; i < states_; ++i) manifests_->unpin(state_[i], *runs_);
    states_ = 0;
    ownPins_ = false;
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
  bool ownPins_ = false;  // `start` pinned the chain (`startChain`: the snapshot's owner did)
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

  /// Starts loading into `slot`, which must have no history. `scratch` holds ChunkBytes. `detached` (sets browser 2b-ii,
  /// DEC-261: a switch's staged content): the slot's history is left as it is; only the file's current state is kept,
  /// as a manifest the caller owns (`detachedState`, one reference), and the other states are released. `adoptCurrent`
  /// (DEC-261): that staged state — the same content, already playing — becomes the slot's current state instead of a
  /// new manifest (History takes a reference of its own), so the track playing it continues untouched.
  Status start(Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res, NoteIdSource& ids,
               uint8_t slot, Source& source, uint8_t* scratch, bool detached = false,
               ManifestId adoptCurrent = kNoManifest) {
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
    detached_ = detached;
    detachedState_ = kNoManifest;
    adopt_ = adoptCurrent;
    ManifestId probe[1];
    int16_t cur;
    bool below;
    if (!detached && history.chain(slot, probe, 1, cur, below) != 0) return status_ = Status::DeniedSlot;
    return status_ = Status::Running;
  }

  /// One grain: the header and directory (then admission), or up to ChunkBytes of rows, or the states, or the check.
  Status step() {
    if (status_ != Status::Running) return status_;
    switch (phase_) {
      case Phase::Header: {
        uint8_t h[24];
        if (!read(h, sizeof h)) return fail(Status::IoError);
        if (get32(h) != kSetFileMagic || get16(h + 4) < kSetFileOldestVersion || get16(h + 4) > kSetFileVersion)
          return fail(Status::BadFormat);
        legacyFit_ = get16(h + 4) < 4;
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
            if (legacyFitRow(legacyFit_, runs_->run(run_[r_]).kind, e.status)) continue;  // forgiven at the seal
            res_->writeOwed(run_[r_], e);
          }
          row_ += n;
          budget -= n;
        }
        if (r_ == runCount_) phase_ = Phase::States;
        return status_;
      }
      case Phase::States: {
        for (uint16_t k = 0; k < runCount_; ++k) {  // every row is in: the runs are immutable
          res_->forgive(run_[k]);  // version-3 fit lookups not loaded (DEC-285)
          runs_->seal(run_[k]);
        }
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
          ManifestId m;
          if (i == current_ && adopt_ != kNoManifest) {
            manifests_->addRef(adopt_);  // the staged state, playing already: the slot's current
            m = adopt_;
          } else {
            m = manifests_->create(slot_, get32(h), get32(h + 4), list, count, *runs_);
          }
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
        if (detached_) {  // the current state only, the caller's; the others (and runs only they listed) released
          for (uint8_t i = 0; i < made_; ++i)
            if (i == current_) detachedState_ = made_list_[i];
            else manifests_->release(made_list_[i], *runs_);
          made_ = 0;
          while (ids_->peek() <= noteIdHigh_) ids_->advance();
          return status_ = Status::Done;
        }
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
  /// The run loaded for directory entry `i` (DEC-254: the run-once store maps it to its record once loaded).
  RunId directoryRun(uint16_t i) const { return run_[i]; }
  /// A detached load's current state (kNoManifest: the file's current is the empty slot).
  ManifestId detachedState() const { return detachedState_; }

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
  bool emptyBelow_ = true, detached_ = false;
  ManifestId detachedState_ = kNoManifest, adopt_ = kNoManifest;
  uint32_t loop_ = 0, crc_ = 0, noteIdHigh_ = 0, row_ = 0;
  uint16_t runCount_ = 0, r_ = 0;
  Phase phase_ = Phase::Header;
  bool legacyFit_ = false;  // a version-3 file (DEC-285); in the byte before `run_`'s alignment
  RunId run_[Runs::kRunCount];
  uint32_t rows_[Runs::kRunCount];
  ManifestId made_list_[kMaxStates];
  Status status_ = Status::Idle;
};

/// V1 import (E8.2; owner decision 2026-11-05: exact current content only). A V1 slot's current content — its resolved
/// notes and events, as the old firmware plays them (LSV1 `LoopSnapshot`) — becomes one take, one manifest and the
/// slot's only history state; no undo history from before the import. Notes come in start order and events in tick
/// order, each stream on its own (E9.7b: events take a run of their own, listed first in the manifest so an event on a
/// note's tick comes before it; a file stores all notes, then all events); a note whose end is before its
/// start is held across the loop end. Rows: NoteOffs due at or before a NoteOn's tick first (DEC-244), held notes'
/// NoteOffs in the wrap band. Notes get fresh ids. Admission and failure behave like `LoadJob`: every row is reserved
/// at `start` or the import is refused by name; any failure releases everything. Within one take same-pitch overlaps do
/// not occlude each other (Q-OVL-1), so the take composes back to exactly these notes.
template <typename Pool, typename Runs, typename Manifests, typename HistoryT, uint16_t MaxHeld = 256>
class ResolvedImport {
 public:
  enum class Status : uint8_t { Idle, Running, Done, DeniedCapacity, DeniedSlot, BadOrder, BadInput, TooManyHeld };
  using Reservations = RowReservations<Pool, Runs>;

  /// Admission for `notes` notes and `events` events into the empty `slot`.
  Status start(Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res, NoteIdSource& ids,
               uint8_t slot, uint32_t loopLength, uint32_t notes, uint32_t events) {
    cancel();
    bind(pool, runs, manifests, history, res, ids);
    return begin(slot, loopLength, notes, events);
  }
  /// `start` in two steps: the engine's tables (a reader binds them when it starts), then the admission once it has
  /// counted the rows.
  void bind(Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res, NoteIdSource& ids) {
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    res_ = &res;
    ids_ = &ids;
  }
  Status begin(uint8_t slot, uint32_t loopLength, uint32_t notes, uint32_t events) {
    cancel();
    Manifests& manifests = *manifests_;
    HistoryT& history = *history_;
    Runs& runs = *runs_;
    Reservations& res = *res_;
    slot_ = slot;
    loop_ = loopLength;
    last_ = lastEvent_ = 0;
    eventRun_ = kNoRun;
    passes_ = 0;
    held_ = 0;
    ManifestId probe[1];
    int16_t cur;
    bool below;
    if (history.chain(slot, probe, 1, cur, below) != 0) return status_ = Status::DeniedSlot;
    if (loopLength == 0 || manifests.freeCount() == 0) return status_ = Status::DeniedCapacity;
    run_ = runs.allocate(slot, RunKind::kRecord);
    if (run_ == kNoRun) return status_ = Status::DeniedCapacity;
    if (res.reserve(run_, 2 * notes) != Admission::Ok) return fail(Status::DeniedCapacity);
    if (events != 0) {  // E9.7b: events take their own run, so notes and events each arrive in their own tick order
      eventRun_ = runs.allocate(slot, RunKind::kRecord);
      if (eventRun_ == kNoRun || res.reserve(eventRun_, events) != Admission::Ok) return fail(Status::DeniedCapacity);
    }
    return status_ = Status::Running;
  }

  Status addNote(uint32_t start, uint32_t end, uint8_t pitch, uint8_t velocity, uint8_t channel) {
    if (status_ != Status::Running) return status_;
    if (start >= loop_ || end >= loop_ || pitch > 127 || velocity == 0 || velocity > 127 || channel > 15)
      return fail(Status::BadInput);
    if (start < last_) return fail(Status::BadOrder);
    if (held_ == MaxHeld) return fail(Status::TooManyHeld);
    flushUpTo(start);
    last_ = start;
    const uint32_t id = ids_->peek();
    ids_->advance();
    write(row(start, id, static_cast<uint8_t>(0x90 | channel), pitch, velocity));
    // Held across the loop end (end before start): the wrap band. A zero-length note keeps its NoteOff on its tick.
    const uint32_t off = end >= start ? end : end + loop_;
    push(Pending{off, id, static_cast<uint8_t>(0x80 | channel), pitch});
    return status_;
  }
  Status addEvent(uint32_t tick, uint8_t status, uint8_t d1, uint8_t d2) {
    if (status_ != Status::Running) return status_;
    if (tick >= loop_ || (status & 0x80) == 0 || (status & 0xF0) == 0x80 || (status & 0xF0) == 0x90)
      return fail(Status::BadInput);
    if (tick < lastEvent_ || eventRun_ == kNoRun) return fail(eventRun_ == kNoRun ? Status::BadInput : Status::BadOrder);
    lastEvent_ = tick;
    res_->writeOwed(eventRun_, row(tick, 0, status, d1, d2));
    return status_;
  }

  static constexpr uint8_t kMaxPassRuns = 16;  // runs a pass stream's passes may take

  /// E9.7c: the next recorded pass of a pass-stream V1 slot, with `notes` notes, as a run of its own — later passes
  /// occlude earlier ones at composition, exactly as an overdub does. The previous pass's NoteOffs are written first.
  Status nextPass(uint32_t notes) {
    if (status_ != Status::Running) return status_;
    if (passes_ == kMaxPasses) return fail(Status::BadInput);
    flushUpTo(0xFFFFFFFFu);
    res_->forgive(run_);
    runs_->seal(run_);
    passRuns_[passes_++] = run_;
    run_ = runs_->allocate(slot_, RunKind::kOverdub);
    if (run_ == kNoRun || res_->reserve(run_, 2 * notes) != Admission::Ok) return fail(Status::DeniedCapacity);
    last_ = 0;
    return status_;
  }

  /// Writes the remaining NoteOffs, seals the take and publishes it as the slot's only state.
  Status finish() {
    if (status_ != Status::Running) return status_;
    flushUpTo(0xFFFFFFFFu);
    res_->forgive(run_);  // rows the admission counted but the content did not use
    runs_->seal(run_);
    RunId list[kMaxPasses + 2];
    uint8_t n = 0;
    if (eventRun_ != kNoRun) {  // events first: a program change on a note's tick comes before it (pass order)
      res_->forgive(eventRun_);
      runs_->seal(eventRun_);
      list[n++] = eventRun_;  // admitted only with events to write (the reader counts the kept ones)
    }
    for (uint8_t k = 0; k < passes_; ++k) list[n++] = passRuns_[k];  // pass order: record, then overdubs
    list[n++] = run_;
    const ManifestId m = manifests_->create(slot_, 1, loop_, list, n, *runs_);
    if (m == kNoManifest) return fail(Status::DeniedCapacity);
    runs_->release(run_);  // the manifest holds the take
    run_ = kNoRun;
    if (eventRun_ != kNoRun) runs_->release(eventRun_);
    for (uint8_t k = 0; k < passes_; ++k) runs_->release(passRuns_[k]);
    passes_ = 0;
    eventRun_ = kNoRun;
    history_->restoreSlot(slot_, &m, 1, 0, true);
    return status_ = Status::Done;
  }

  void cancel() {
    if (status_ == Status::Running) fail(Status::Idle);
  }
  Status status() const { return status_; }
  uint16_t heldPeak() const { return heldPeak_; }

 private:
  struct Pending {
    uint32_t tick, noteId;
    uint8_t status, pitch;
  };

  static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2) {
    PassEvent e{};
    e.tick = tick;
    e.noteId = id;
    e.status = status;
    e.data1 = d1;
    e.data2 = d2;
    return e;
  }
  void write(const PassEvent& e) { res_->writeOwed(run_, e); }

  // A binary min-heap of pending NoteOffs by tick (then noteId, deterministic).
  static bool before(const Pending& a, const Pending& b) { return a.tick < b.tick || (a.tick == b.tick && a.noteId < b.noteId); }
  void push(const Pending& p) {
    uint16_t i = held_++;
    heap_[i] = p;
    while (i > 0 && before(heap_[i], heap_[(i - 1) / 2])) {
      const Pending t = heap_[i];
      heap_[i] = heap_[(i - 1) / 2];
      heap_[(i - 1) / 2] = t;
      i = static_cast<uint16_t>((i - 1) / 2);
    }
    if (held_ > heldPeak_) heldPeak_ = held_;
  }
  void flushUpTo(uint32_t tick) {
    while (held_ > 0 && heap_[0].tick <= tick) {
      const Pending p = heap_[0];
      heap_[0] = heap_[--held_];
      for (uint16_t i = 0;;) {
        const uint16_t l = static_cast<uint16_t>(2 * i + 1), r = static_cast<uint16_t>(l + 1);
        uint16_t m = i;
        if (l < held_ && before(heap_[l], heap_[m])) m = l;
        if (r < held_ && before(heap_[r], heap_[m])) m = r;
        if (m == i) break;
        const Pending t = heap_[i];
        heap_[i] = heap_[m];
        heap_[m] = t;
        i = m;
      }
      write(row(p.tick, p.noteId, p.status, p.pitch, 0));
    }
  }
  Status fail(Status s) {
    for (RunId* r : {&run_, &eventRun_}) {
      if (*r == kNoRun) continue;
      res_->forgive(*r);
      runs_->seal(*r);
      runs_->release(*r);
      *r = kNoRun;
    }
    for (uint8_t k = 0; k < passes_; ++k) runs_->release(passRuns_[k]);  // sealed, nothing owed
    passes_ = 0;
    held_ = 0;
    return status_ = s;
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  Reservations* res_ = nullptr;
  NoteIdSource* ids_ = nullptr;
  uint8_t slot_ = 0;
  static constexpr uint8_t kMaxPasses = kMaxPassRuns - 1;  // the runs before the current one
  uint32_t loop_ = 0, last_ = 0, lastEvent_ = 0;
  RunId run_ = kNoRun, eventRun_ = kNoRun;
  RunId passRuns_[kMaxPasses] = {};
  uint8_t passes_ = 0;
  Pending heap_[MaxHeld];
  uint16_t held_ = 0, heldPeak_ = 0;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
