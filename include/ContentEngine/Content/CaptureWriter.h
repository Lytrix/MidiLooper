//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The fixed-page writer of one Record / overdub take (fixed-memory-engine E2, DEC-252).
//
// * NoteOn: one row now and its NoteOff reserved in the same run (CE-A1). Refused only here, at the first row of a new
//   note: open-note cap (128), or capacity.
// * NoteOff: pairs with the most recent open NoteOn on its (channel, pitch) lane (PAIR-1, LIFO) and is written into the
//   run that owes it. Never refused while the note is open: its row is reserved.
// * Wrap: the active run commits and a new run starts, always: a run never spans more than one loop cycle (E3 slice 3).
//   With no open notes the old run seals at once; with open notes it becomes a closing run that accepts only its own
//   NoteOffs and seals when the last one arrives (DEC-250 late companions belong to the originating record). Several
//   runs may be closing at once (each holds at least one open note, so at most 128); revision order is the manifest's
//   concern (DEC-069 owed commit), not the storage layout's. No descriptor for the next run: FinalInterval; the
//   following wrap ends the take.
// * Stop: a generated closure (flagged in `origin`) for every open note, into its own run, then every run seals.
//   Nothing it needs can be refused.
// * Continuous controllers (MidiClass.h): at most `Config::kMaxContinuousPerTick` per tick; later values on that tick
//   are not recorded (`Limited`, counted). Essential and parameter messages are never limited.
// Overlap consequences are not written here (CE-E, derived at composition). No allocation; `init()` writes every field.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/Content/MidiClass.h"
#include "ContentEngine/Content/PassEvent.h"
#include "ContentEngine/Content/RowReservations.h"
#include "ContentEngine/Content/RunTable.h"

namespace ContentEngine {

/// Engine-wide NoteId allocator: monotonic, never reused within a boot (persistence restores the high-water mark).
struct NoteIdSource {
  uint32_t next;
  void init(uint32_t first = 1) { next = first; }
  uint32_t peek() const { return next; }
  void advance() { ++next; }
};

enum class WriterResult : uint8_t {
  Ok,
  NotWriting,
  DeniedRun,        // no run descriptor for the take
  DeniedOpenNotes,  // 128 notes already open
  DeniedCapacity,   // the row and what it owes do not fit
  Unmatched,        // NoteOff with no open note on its lane: nothing written
  Limited,          // a continuous controller beyond the per-tick limit: nothing written (owner 2026-11-05)
  FinalInterval,    // wrap without a descriptor for the next run: the next wrap ends the take
  Stopped,          // the wrap ended a FinalInterval take
};

template <typename Pool, typename Runs>
class Writer {
 public:
  using Reservations = RowReservations<Pool, Runs>;
  /// The row reservations (a resize's fit run reserves through them, DEC-257).
  Reservations& reservations() { return *res_; }
  static constexpr uint16_t kMaxOpen = Config::kMaxOpenNotesPerWriter;
  static constexpr uint8_t kMaxClosing = Config::kMaxOpenNotesPerWriter;  // each closing run owes >= 1 NoteOff
  // Stop may seal every closing run and the active run at once.
  static constexpr uint8_t kSealedQueue = kMaxClosing + 2;

  void init(Pool& pool, Runs& runs, Reservations& res, NoteIdSource& ids) {
    pool_ = &pool;
    runs_ = &runs;
    res_ = &res;
    ids_ = &ids;
    state_ = State::Idle;
    active_ = kNoRun;
    lastRun_ = kNoRun;
    for (auto& c : closing_) c = kNoRun;
    closingCount_ = 0;
    finalInterval_ = false;
    for (auto& s : sealed_) s = kNoRun;
    sealedHead_ = sealedCount_ = 0;
    sealedOverflow_ = 0;
    seq_ = 0;
    loopLength_ = 0;
    activeBase_ = 0;
    slot_ = 0xFF;
    kind_ = 0;
    for (auto& o : open_) o = Open{};
    openCount_ = 0;
    openOrder_ = 0;
    ccTick_ = 0xFFFFFFFFu;
    ccCount_ = 0;
    continuousLimited_ = 0;
  }

  /// `loopLength` (0 when not yet known, e.g. a Record take) marks NoteOffs of notes held a full loop (Q-LEN-1).
  /// Each run stores ticks relative to its own start (`startTick`, then each wrap tick), so a run's main rows are loop
  /// positions and a held note's NoteOff lands at `loop + position` in its originating run.
  /// A sealed Edit run of HideNote rows hiding the notes `ids` (E9.8i amendment, owner 2026-11-09: a late Stop's
  /// repeated notes); the caller lists it after the run it hides in and holds the writer's reference. kNoRun: refused
  /// (a run is being written, no run left, or no capacity) — nothing taken.
  RunId hideRun(uint8_t slot, const uint32_t* ids, uint8_t n) {
    if (state_ == State::Writing || n == 0) return kNoRun;
    const RunId run = runs_->allocate(slot, RunKind::kEdit);
    if (run == kNoRun) return kNoRun;
    for (uint8_t i = 0; i < n; ++i) {
      PassEvent e{};
      e.noteId = ids[i];
      e.status = kHideNoteStatus;
      if (res_->accept(run, e, 0) != Admission::Ok) {
        res_->forgive(run);
        runs_->seal(run);
        runs_->release(run);
        return kNoRun;
      }
    }
    runs_->seal(run);
    return run;
  }
  WriterResult begin(uint8_t slot, uint8_t kind, uint32_t loopLength = 0, uint32_t startTick = 0) {
    if (state_ == State::Writing) return WriterResult::NotWriting;
    const RunId run = runs_->allocate(slot, kind);
    if (run == kNoRun) return WriterResult::DeniedRun;
    loopLength_ = loopLength;
    activeBase_ = startTick;
    active_ = run;
    slot_ = slot;
    kind_ = kind;
    closingCount_ = 0;
    finalInterval_ = false;
    state_ = State::Writing;
    return WriterResult::Ok;
  }

  WriterResult noteOn(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity) {
    if (state_ != State::Writing) return WriterResult::NotWriting;
    if (openCount_ >= kMaxOpen) return WriterResult::DeniedOpenNotes;
    const uint32_t id = ids_->peek();
    const Admission a =
        res_->accept(active_, row(tick - activeBase_, id, statusOf(0x90, channel), pitch, velocity, 0), 1);
    if (a != Admission::Ok) return WriterResult::DeniedCapacity;
    ids_->advance();
    for (auto& o : open_)
      if (!o.used) {
        o = Open{id, ++openOrder_, tick, activeBase_, active_, channel, pitch, true};
        break;
      }
    ++openCount_;
    return WriterResult::Ok;
  }

  WriterResult noteOff(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity) {
    if (state_ != State::Writing) return WriterResult::NotWriting;
    Open* match = nullptr;
    for (auto& o : open_)
      if (o.used && o.channel == channel && o.pitch == pitch && (match == nullptr || o.order > match->order)) match = &o;
    if (match == nullptr) return WriterResult::Unmatched;
    closeOpen(*match, row(tick - match->base, match->noteId, statusOf(0x80, channel), pitch, velocity, heldFlag(*match, tick)));
    return WriterResult::Ok;
  }

  /// CC, pitch bend, program change, aftertouch: one row, nothing owed. Continuous controllers are limited per tick.
  WriterResult channelEvent(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2) {
    if (state_ != State::Writing) return WriterResult::NotWriting;
    if (controlClass(status, data1) == ControlClass::Continuous) {
      if (tick != ccTick_) {
        ccTick_ = tick;
        ccCount_ = 0;
      }
      if (ccCount_ >= Config::kMaxContinuousPerTick) {
        ++continuousLimited_;
        return WriterResult::Limited;
      }
    }
    if (res_->accept(active_, row(tick - activeBase_, 0, status, data1, data2, 0), 0) != Admission::Ok)
      return WriterResult::DeniedCapacity;
    if (controlClass(status, data1) == ControlClass::Continuous) ++ccCount_;
    return WriterResult::Ok;
  }
  /// Continuous controller values not recorded because their tick already had the limit.
  uint32_t continuousLimited() const { return continuousLimited_; }

  WriterResult wrap(uint32_t wrapTick = 0) {
    if (state_ != State::Writing) return WriterResult::NotWriting;
    if (finalInterval_) {
      stopAt(wrapTick);
      return WriterResult::Stopped;
    }
    const bool holds = res_->outstanding(active_) > 0;
    const RunId next = runs_->allocate(slot_, kind_);
    if (next == kNoRun) {
      finalInterval_ = true;
      return WriterResult::FinalInterval;
    }
    if (holds) {
      closing_[closingCount_++] = active_;
    } else {
      sealAndQueue(active_);
    }
    active_ = next;
    activeBase_ = wrapTick;
    return WriterResult::Ok;
  }

  WriterResult stop(uint32_t tick) {
    if (state_ != State::Writing) return WriterResult::NotWriting;
    stopAt(tick);
    return WriterResult::Ok;
  }

  /// Pops one run that sealed (wrap without held notes, closing run completed, Stop). The caller publishes it.
  bool takeSealed(RunId& out) {
    if (sealedCount_ == 0) return false;
    out = sealed_[sealedHead_];
    sealedHead_ = static_cast<uint8_t>((sealedHead_ + 1) % kSealedQueue);
    --sealedCount_;
    return true;
  }

  RunId activeRun() const { return active_; }
  RunId lastRun() const { return lastRun_; }
  uint8_t closingCount() const { return closingCount_; }
  uint16_t openCount() const { return openCount_; }
  bool isFinalInterval() const { return finalInterval_; }
  uint32_t sealedOverflow() const { return sealedOverflow_; }

 private:
  enum class State : uint8_t { Idle, Writing, Stopped };
  struct Open {
    uint32_t noteId = 0;
    uint32_t order = 0;
    uint32_t onTick = 0;  // caller timeline
    uint32_t base = 0;    // its run's start tick (rows store tick - base)
    RunId run = kNoRun;
    uint8_t channel = 0;
    uint8_t pitch = 0;
    bool used = false;
  };

  static uint8_t statusOf(uint8_t type, uint8_t channel) {
    return static_cast<uint8_t>(type | ((channel - 1u) & 0x0Fu));
  }

  PassEvent row(uint32_t tick, uint32_t noteId, uint8_t status, uint8_t d1, uint8_t d2, uint8_t origin) {
    PassEvent e;
    e.tick = tick;
    e.noteId = noteId;
    e.seq = seq_++;
    e.status = status;
    e.data1 = d1;
    e.data2 = d2;
    e.origin = origin;
    return e;
  }

  void closeOpen(Open& o, const PassEvent& off) {
    const RunId run = o.run;
    res_->writeOwed(run, off);  // cannot fail: reserved at the NoteOn
    o = Open{};
    --openCount_;
    for (uint8_t i = 0; i < closingCount_; ++i) {
      if (closing_[i] != run || res_->outstanding(run) != 0) continue;
      sealAndQueue(run);
      for (uint8_t k = i; k + 1 < closingCount_; ++k) closing_[k] = closing_[k + 1];
      closing_[--closingCount_] = kNoRun;
      break;
    }
  }

  uint8_t heldFlag(const Open& o, uint32_t offTick) const {
    return loopLength_ != 0 && offTick - o.onTick >= loopLength_ ? kOriginHeldLoop : 0;
  }

  void stopAt(uint32_t tick) {
    for (auto& o : open_)
      if (o.used)
        closeOpen(o, row(tick - o.base, o.noteId, statusOf(0x80, o.channel), o.pitch, 0,
                         static_cast<uint8_t>(kOriginGeneratedClosure | heldFlag(o, tick))));
    for (uint8_t i = 0; i < closingCount_; ++i) sealAndQueue(closing_[i]);  // already empty after closures
    closingCount_ = 0;
    sealAndQueue(active_);
    lastRun_ = active_;
    active_ = kNoRun;
    finalInterval_ = false;
    state_ = State::Stopped;
  }

  void sealAndQueue(RunId run) {
    if (run == kNoRun || runs_->run(run).state != RunState::Writing) return;
    runs_->seal(run);
    if (sealedCount_ == kSealedQueue) {
      ++sealedOverflow_;
      return;
    }
    sealed_[(sealedHead_ + sealedCount_) % kSealedQueue] = run;
    ++sealedCount_;
  }

  Pool* pool_;
  Runs* runs_;
  Reservations* res_;
  NoteIdSource* ids_;
  State state_;
  RunId active_;
  RunId lastRun_;
  RunId closing_[kMaxClosing];
  uint8_t closingCount_;
  bool finalInterval_;
  RunId sealed_[kSealedQueue];
  uint8_t sealedHead_, sealedCount_;
  uint32_t sealedOverflow_;
  uint32_t seq_;
  uint32_t ccTick_;  // the tick the continuous count below belongs to
  uint8_t ccCount_;
  uint32_t continuousLimited_;
  uint32_t loopLength_;
  uint32_t activeBase_;
  uint8_t slot_, kind_;
  Open open_[kMaxOpen];
  uint16_t openCount_;
  uint32_t openOrder_;
};

}  // namespace ContentEngine
