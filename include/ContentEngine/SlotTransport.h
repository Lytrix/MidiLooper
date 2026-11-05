//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Slot transport of the content engine (fixed-memory-engine E9.1, plan § 8): the gestures that write — Record, overdub,
// Stop — and Undo / Redo, turned into writer calls, manifests, history states and playback selections.
//
// * One writer (owner 2026-11-05, E5.10b): a second Record or overdub while one is writing is refused (`Busy`).
// * Timeline: the caller's Clock tick. Each track keeps an origin (its slot's loop position 0); the writer works in Clock
//   ticks from the run's base, the playback track in ticks since the origin.
// * Record: Stop sets the loop length (stop − start); an empty take leaves the slot empty. The take is the slot's first
//   state and plays on the recording track from the first window boundary its window is ready for.
// * Overdub (DEC-038 session, E4.2): every non-empty wrap publishes `current runs + the wrapped run` as a session step
//   and selects it. **Owner 2026-11-05: publish at the wrap** — a run still closing (a note held across the loop end)
//   is published with what it holds, so the cycle's other notes play from the next cycle; when its last NoteOff
//   arrives and it seals, every state created before that holds a completion instead: a new manifest (new identity)
//   with the same runs, in place, no extra step. A manifest's content therefore never changes under its identity, which
//   checkpoints and tiles rely on. Stop publishes the active run and folds the session into one step. An empty wrap
//   publishes nothing. A wrap that would overfill the manifest's run list, or finds no run descriptor, ends the take
//   at that wrap (named); compaction is later work.
// * Live occupancy (E6.2f): an accepted overdub NoteOn occupies its pitch on the writing track; its NoteOff, or Stop's
//   closures, release it.
// * Undo / Redo: History moves (per slot; while overdubbing the session bounds its slot); every track playing the slot
//   selects the new current, or stops when the slot is empty.
// The writer's reference on each run is dropped once the run sealed and is listed by the manifests that need it.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/History.h"
#include "ContentEngine/Writer.h"

namespace ContentEngine {

template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Track, uint8_t Tracks>
class SlotTransport {
 public:
  using WriterT = Writer<Pool, Runs>;
  using Emit = typename Track::Emit;
  static constexpr uint8_t kNoSlot = 0xFF;

  enum class Result : uint8_t {
    Ok,
    Busy,            // another track records or overdubs (one writer)
    NotWriting,      // Stop or input with no take
    DeniedSlot,      // Record on a slot with content
    Empty,           // overdub on an empty slot, or a Stop that recorded nothing (the slot stays as it was)
    DeniedRun,       // no run descriptor for the take
    DeniedManifest,  // no manifest slot for the take's first state
    Refused,         // the writer refused the input (open-note cap, capacity, continuous limit, unmatched NoteOff)
    NoChange,        // nothing to undo / redo
  };
  enum class Mode : uint8_t { Idle, Recording, Overdubbing };

  struct Stats {
    uint32_t publishedWraps;   // session steps published at a wrap
    uint32_t emptyWraps;       // wraps with nothing recorded: no step
    uint32_t closingPublished; // wraps published with the run still closing
    uint32_t completions;      // states replaced in place when a closing run sealed
    uint32_t completionsDenied;  // completion found no manifest slot (the state keeps the earlier manifest)
    uint32_t takesEndedFull;   // takes ended at a wrap: run list or run descriptors full
    uint32_t emptyTakes;       // Stops that recorded nothing
  };

  void init(Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, WriterT& writer, Track* const* tracks) {
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    writer_ = &writer;
    for (uint8_t i = 0; i < Tracks; ++i) {
      tracks_[i] = tracks[i];
      slot_[i] = kNoSlot;
      origin_[i] = 0;
    }
    mode_ = Mode::Idle;
    wTrack_ = 0;
    wSlot_ = kNoSlot;
    wStart_ = 0;
    loop_ = 0;
    revision_ = 0;
    for (auto& h : held_) h = 0;
    stats_ = Stats{};
  }

  Result startRecord(uint8_t track, uint8_t slot, uint32_t clock) {
    if (mode_ != Mode::Idle) return Result::Busy;
    if (history_->current(slot) != kNoManifest) return Result::DeniedSlot;
    if (manifests_->freeCount() == 0) return Result::DeniedManifest;
    if (writer_->begin(slot, RunKind::kRecord, 0, clock) != WriterResult::Ok) return Result::DeniedRun;
    mode_ = Mode::Recording;
    wTrack_ = track;
    wSlot_ = slot;
    wStart_ = clock;
    return Result::Ok;
  }

  /// Overdub on the slot `track` plays, from the cycle `clock` lies in.
  Result startOverdub(uint8_t track, uint32_t clock) {
    if (mode_ != Mode::Idle) return Result::Busy;
    const uint8_t slot = slot_[track];
    if (slot == kNoSlot || history_->current(slot) == kNoManifest) return Result::Empty;
    const ManifestId base = history_->current(slot);
    loop_ = manifests_->manifest(base).loopLengthTicks;
    if (manifests_->manifest(base).runCount + 1 > Manifests::kMaxRuns) return Result::DeniedRun;
    const uint32_t local = clock - origin_[track];
    const uint32_t cycleStart = clock - local % loop_;
    if (!history_->beginSession(SessionKind::Overdub, slot)) return Result::Busy;
    if (writer_->begin(slot, RunKind::kOverdub, loop_, cycleStart) != WriterResult::Ok) {
      history_->endSession(SessionKind::Overdub);
      return Result::DeniedRun;
    }
    mode_ = Mode::Overdubbing;
    wTrack_ = track;
    wSlot_ = slot;
    wStart_ = clock;
    return Result::Ok;
  }

  /// Ends the take at `clock`. `out` receives NoteOffs (a track stopped by an empty slot); it must hold `Track::T`.
  Result stop(uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (mode_ == Mode::Idle) return Result::NotWriting;
    return mode_ == Mode::Recording ? stopRecord(clock) : stopOverdub(clock, out, max, emitted);
  }

  /// Live input on the writing track. `out` receives the NoteOffs of composed notes the live NoteOn ends (send them
  /// before the live note).
  Result noteOn(uint32_t clock, uint8_t channel, uint8_t pitch, uint8_t velocity, Emit* out, uint8_t max,
                uint8_t& emitted) {
    emitted = 0;
    if (mode_ == Mode::Idle) return Result::NotWriting;
    if (writer_->noteOn(clock, channel, pitch, velocity) != WriterResult::Ok) return Result::Refused;
    ++held_[pitch & 0x7F];
    if (mode_ == Mode::Overdubbing) emitted = tracks_[wTrack_]->liveNoteOn(pitch, local(wTrack_, clock), out, max);
    return Result::Ok;
  }
  Result noteOff(uint32_t clock, uint8_t channel, uint8_t pitch, uint8_t velocity) {
    if (mode_ == Mode::Idle) return Result::NotWriting;
    const uint32_t before = manifests_->nextIdentity();
    if (writer_->noteOff(clock, channel, pitch, velocity) != WriterResult::Ok) return Result::Refused;
    release(pitch);
    drainSealed(before, clock);  // a closing run may have sealed
    return Result::Ok;
  }
  Result channelEvent(uint32_t clock, uint8_t status, uint8_t data1, uint8_t data2) {
    if (mode_ == Mode::Idle) return Result::NotWriting;
    return writer_->channelEvent(clock, status, data1, data2) == WriterResult::Ok ? Result::Ok : Result::Refused;
  }

  /// The Clock tick for `track`: the writing track wraps at its loop end first, then the track plays.
  uint8_t tick(uint8_t track, uint32_t clock, Emit* out, uint8_t max) {
    uint8_t n = 0;
    if (mode_ == Mode::Overdubbing && track == wTrack_ && clock != wStart_ && local(track, clock) % loop_ == 0)
      n = wrap(clock, out, max);
    return static_cast<uint8_t>(n + tracks_[track]->tick(local(track, clock), out + n, static_cast<uint8_t>(max - n)));
  }

  Result undo(uint8_t slot, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (!history_->undo(slot)) return Result::NoChange;
    emitted = selectSlot(slot, clock, out, max);
    return Result::Ok;
  }
  Result redo(uint8_t slot, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (!history_->redo(slot)) return Result::NoChange;
    emitted = selectSlot(slot, clock, out, max);
    return Result::Ok;
  }

  Mode mode() const { return mode_; }
  uint8_t playingSlot(uint8_t track) const { return slot_[track]; }
  uint32_t origin(uint8_t track) const { return origin_[track]; }
  const Stats& stats() const { return stats_; }

 private:
  uint32_t local(uint8_t track, uint32_t clock) const { return clock - origin_[track]; }

  void release(uint8_t pitch) {
    if (held_[pitch & 0x7F] == 0) return;
    --held_[pitch & 0x7F];
    if (mode_ == Mode::Overdubbing) tracks_[wTrack_]->liveNoteOff(pitch);
  }
  void releaseAllHeld() {
    for (uint8_t p = 0; p < 128; ++p)
      while (held_[p] != 0) release(p);
  }

  /// A new manifest: `base`'s runs (none for kNoManifest) followed by `extra` (kNoRun: none).
  ManifestId publish(ManifestId base, RunId extra) {
    RunId list[Manifests::kMaxRuns];
    uint8_t n = 0;
    if (base != kNoManifest) {
      const auto& b = manifests_->manifest(base);
      for (uint8_t i = 0; i < b.runCount; ++i) list[n++] = b.runs[i];
    }
    if (extra != kNoRun) list[n++] = extra;
    return manifests_->create(wSlot_, ++revision_, loop_, list, n, *runs_);
  }

  /// Every track playing `slot` follows its current state. Returns NoteOffs written to `out`.
  uint8_t selectSlot(uint8_t slot, uint32_t clock, Emit* out, uint8_t max) {
    uint8_t n = 0;
    const ManifestId m = history_->current(slot);
    for (uint8_t t = 0; t < Tracks; ++t) {
      if (slot_[t] != slot) continue;
      if (m == kNoManifest)
        n = static_cast<uint8_t>(n + tracks_[t]->stop(local(t, clock), out + n, static_cast<uint8_t>(max - n)));
      else
        tracks_[t]->select(slot, m, local(t, clock));
    }
    return n;
  }

  /// Pops every run the writer sealed. A run sealed by this call (`before`: the manifest identity watermark taken
  /// before the writer call) completes every state of the slot created while it was writing; then the writer's
  /// reference goes (the manifests hold the run, or nothing does and it is reclaimed).
  void drainSealed(uint32_t before, uint32_t clock) {
    RunId r;
    while (writer_->takeSealed(r)) {
      complete(r, before, clock);
      runs_->release(r);
    }
  }
  void complete(RunId run, uint32_t before, uint32_t clock) {
    ManifestId states[HistoryT::kDepth];
    int16_t cur;
    bool below;
    const uint8_t n = history_->chain(wSlot_, states, HistoryT::kDepth, cur, below);
    bool currentChanged = false;
    for (uint8_t i = 0; i < n; ++i) {
      const auto& m = manifests_->manifest(states[i]);
      if (m.identity >= before || !lists(m, run)) continue;
      const ManifestId fresh = manifests_->create(wSlot_, m.revision, m.loopLengthTicks, m.runs, m.runCount, *runs_);
      if (fresh == kNoManifest) {
        ++stats_.completionsDenied;
        continue;
      }
      history_->replaceState(wSlot_, states[i], fresh);
      ++stats_.completions;
      if (i == cur) currentChanged = true;
    }
    if (currentChanged) selectSlot(wSlot_, clock, nullptr, 0);  // a current with content: no NoteOffs
  }
  template <typename M>
  static bool lists(const M& m, RunId run) {
    for (uint8_t i = 0; i < m.runCount; ++i)
      if (m.runs[i] == run) return true;
    return false;
  }

  Result stopRecord(uint32_t clock) {
    const uint32_t before = manifests_->nextIdentity();
    writer_->stop(clock);
    releaseAllHeld();
    const RunId r0 = writer_->lastRun();
    loop_ = clock - wStart_;
    mode_ = Mode::Idle;
    Result result = Result::Ok;
    if (loop_ == 0 || runs_->run(r0).rowCount == 0) {
      ++stats_.emptyTakes;
      result = Result::Empty;
    } else {
      const ManifestId m = publish(kNoManifest, r0);  // admitted at Record start
      history_->commit(wSlot_, m);
      slot_[wTrack_] = wSlot_;
      origin_[wTrack_] = wStart_;
      tracks_[wTrack_]->select(wSlot_, m, local(wTrack_, clock));
    }
    drainSealed(before, clock);
    return result;
  }

  Result stopOverdub(uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    const uint32_t before = manifests_->nextIdentity();
    writer_->stop(clock);
    releaseAllHeld();
    const RunId last = writer_->lastRun();
    if (runs_->run(last).rowCount != 0) commitStep(last, clock);
    drainSealed(before, clock);
    history_->endSession(SessionKind::Overdub);
    mode_ = Mode::Idle;
    emitted = selectSlot(wSlot_, clock, out, max);  // the folded step is the current (unchanged identity: no transition)
    return Result::Ok;
  }

  /// A session step on top of the slot's current: published, committed and selected.
  void commitStep(RunId run, uint32_t clock) {
    const ManifestId m = publish(history_->current(wSlot_), run);
    if (m == kNoManifest) return;  // W budgets a manifest per writer commit (EngineConfig)
    history_->commit(wSlot_, m);
    selectSlot(wSlot_, clock, nullptr, 0);
  }

  uint8_t wrap(uint32_t clock, Emit* out, uint8_t max) {
    const uint32_t before = manifests_->nextIdentity();
    const RunId wrapped = writer_->activeRun();
    const ManifestId cur = history_->current(wSlot_);
    const bool lastFits = manifests_->manifest(cur).runCount + 2 <= Manifests::kMaxRuns;  // room for the run after it
    const WriterResult w = lastFits ? writer_->wrap(clock) : WriterResult::FinalInterval;
    if (w != WriterResult::Ok) {  // run list or descriptors full: the take ends at this wrap
      ++stats_.takesEndedFull;
      uint8_t n = 0;
      stopOverdub(clock, out, max, n);
      return n;
    }
    if (runs_->run(wrapped).rowCount == 0) {
      ++stats_.emptyWraps;
    } else {
      if (runs_->run(wrapped).state == RunState::Writing) ++stats_.closingPublished;
      commitStep(wrapped, clock);
      ++stats_.publishedWraps;
    }
    drainSealed(before, clock);
    return 0;
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  WriterT* writer_ = nullptr;
  Track* tracks_[Tracks] = {};
  uint8_t slot_[Tracks] = {};
  uint32_t origin_[Tracks] = {};
  Mode mode_ = Mode::Idle;
  uint8_t wTrack_ = 0, wSlot_ = kNoSlot;
  uint32_t wStart_ = 0, loop_ = 0, revision_ = 0;
  uint8_t held_[128] = {};
  Stats stats_{};
};

}  // namespace ContentEngine
