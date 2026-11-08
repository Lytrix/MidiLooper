//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Slot transport of the content engine (fixed-memory-engine E9.1, plan § 8): the gestures that write — Record, overdub,
// Stop — and Undo / Redo, turned into writer calls, manifests, history states and playback selections.
//
// * One writer (owner 2026-11-05, E5.10b): a second Record or overdub while one is writing is refused (`Busy`).
// * Timeline: the caller's Clock tick. Each track keeps an origin (its slot's loop position 0); the writer works in Clock
//   ticks from the run's base, the playback track in ticks since the origin.
//   Input at `clock` arrives after tick `clock` was played; a gesture at `clock` (Stop, a wrap) takes effect before
//   tick `clock` is played: a Record Stop at `clock` makes `clock` cycle 1's position 0, and a wrap at `clock` puts
//   that tick's input in the next run.
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
// * Handoff without a gap (E9.1b, plan § 8.3): the next cycle's window 0 is composed before the gesture's boundary.
//   Overdub: at `prepPosition()` in each cycle (`kLeadTicks` before the loop end, half the loop for a shorter one)
//   the writing track selects a provisional source — the slot's current runs + the run being written, as recorded so
//   far — for the wrap boundary; at the wrap the committed state is selected for the next boundary (window 1). The
//   provisional source is never a history state, checkpointed or tiled; rows recorded after it was taken reach window
//   0 only as live effects (held notes occupy their pitch), the accepted live tolerance. Taken earlier in the cycle it
//   is adopted at an earlier boundary: harmless, as the run being written has no rows yet in the windows after it.
//   Record: once the take passes window 0, a track that plays nothing composes window 0 of the take's cycle 1 from the
//   prefix under a provisional loop. One pass: no overlap rule reaches across passes, so window 0 depends only on its
//   own rows; notes still held then are open and end where the take says, carried by the take's next window. Stop
//   makes it the transition for the Stop tick (`PlaybackTrack::handoff`). A shorter take composes window 0 at Stop,
//   aimed at the Stop tick, and plays from the first boundary it is ready for. Within window 0 the prefix is also
//   prepared at each bar line (`setBarTicks`), so a take trimmed to one of them has its window 0 (E9.8i).
// * Trimmed Record Stop (E9.8i, legacy `RecordStopLength`, owner 2026-11-06: rows stay immutable): `stop` with a
//   `length` shorter than the take ends the loop there; the take's rows past it are kept and are the loop's wrap
//   stream — notes played after the bar line sound from the loop start, a note held across it ends in the next cycle.
//   Playback goes on in phase: cycle 1 began at the bar line, so the take joins at its position now.
// * Pickups (E9.8j, owner 2026-11-06: the note-off wrap in reverse): notes played shortly before a Record take's tick 0
//   (`stop`'s `pickups`, gathered by `TrackControl`) are written at Stop, when the loop is known, as a second run of the
//   take: NoteOn at `loop - before`, NoteOff where the note really ended (past the loop end: the wrap stream), never
//   longer than the loop. From the second cycle they sound just before each downbeat.
// * Live occupancy (E6.2f): an accepted overdub NoteOn occupies its pitch on the writing track; its NoteOff, or Stop's
//   closures, release it.
// * Undo / Redo: History moves (per slot; while overdubbing the session bounds its slot); every track playing the slot
//   selects the new current, or stops when the slot is empty.
// * E9.4 (owner 2026-11-05: one sounding slot per track; Clear is undoable):
//   - Clear commits an empty state (a manifest with no runs): the slot is empty for every gesture (`contentOf`), its
//     tracks stop, and Undo brings the content back.
//   - Launch: a track starts a slot from its loop start, fresh (nothing carried), at a tick the caller commits; its
//     window 0 composes from `launch` on while what plays continues (`PlaybackTrack::launch`), and at `commitLaunch` the
//     track's origin moves to that tick and what sounded ends.
//   - A track plays (`playing`) from a Record Stop or a committed launch until `stopTrack`; Undo / Redo / Clear select
//     only on playing tracks.
// The writer's reference on each run is dropped once the run sealed and is listed by the manifests that need it.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/FitRun.h"
#include "ContentEngine/History.h"
#include "ContentEngine/Writer.h"

namespace ContentEngine {

template <typename Pool, typename Runs, typename Manifests, typename HistoryT, typename Track, uint8_t Tracks>
class SlotTransport {
 public:
  using WriterT = Writer<Pool, Runs>;
  using Emit = typename Track::Emit;
  static constexpr uint8_t kNoSlot = 0xFF;
  /// The provisional source is taken at least this long before the wrap: the time one window has to compose under load
  /// (≤ 100 grains of 810 µs, with the Transition class's ≥ 1 grain in 2: ≈ 160 ms ≈ 160 ticks at 300 BPM), doubled.
  static constexpr uint32_t kLeadTicks = 384;
  /// A Record take's provisional loop: longer than any take, so the prefix has no wrap band.
  static constexpr uint32_t kProvisionalLoop = 0x40000000u;

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
    uint32_t provisionalSelections;  // wraps prepared with a provisional source (E9.1b)
    uint32_t recordPrepares;         // Record takes whose window 0 was composed ahead
    uint32_t trimmedTakes;           // Record takes ended at the bar line before their Stop (E9.8i)
    uint32_t pickupNotes;            // notes played before a take's tick 0, written at its Stop (E9.8j)
    uint32_t pickupsDropped;         // pickups not written (no run descriptor)
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
      playing_[i] = false;
      launchSlot_[i] = kNoSlot;
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
    if (contentOf(slot) != kNoManifest) return Result::DeniedSlot;
    if (manifests_->freeCount() == 0) return Result::DeniedManifest;
    if (writer_->begin(slot, RunKind::kRecord, 0, clock) != WriterResult::Ok) return Result::DeniedRun;
    mode_ = Mode::Recording;
    wTrack_ = track;
    wSlot_ = slot;
    wStart_ = clock;
    preparedLen_ = 0;
    return Result::Ok;
  }

  /// Overdub on the slot `track` plays, from the cycle `clock` lies in.
  Result startOverdub(uint8_t track, uint32_t clock) {
    if (mode_ != Mode::Idle) return Result::Busy;
    const uint8_t slot = slot_[track];
    if (slot == kNoSlot || !playing_[track] || contentOf(slot) == kNoManifest) return Result::Empty;
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
  /// A note played before a Record take's tick 0 (E9.8j): `before` ticks before it (1 or more); `off` where it ended,
  /// relative to tick 0 (negative: before it).
  struct Pickup {
    uint32_t before;
    int32_t off;
    uint8_t channel, pitch, velocity;
  };
  static constexpr uint8_t kMaxPickups = 16;

  /// `length` (a Record take; 0: the take's length): the loop ends there, before `clock` (E9.8i) — at most a loop
  /// earlier, so the take's rows past it fit the loop's wrap stream. `pickups` (a Record take, at most `kMaxPickups`):
  /// notes played before its tick 0 (E9.8j).
  Result stop(uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted, uint32_t length = 0,
              const Pickup* pickups = nullptr, uint8_t pickupCount = 0) {
    emitted = 0;
    if (mode_ == Mode::Idle) return Result::NotWriting;
    return mode_ == Mode::Recording ? stopRecord(clock, out, max, emitted, length, pickups, pickupCount)
                                    : stopOverdub(clock, out, max, emitted);
  }
  /// The bar for preparing a Record take's window 0 at bar lines (0: at the window end only).
  void setBarTicks(uint32_t ticks) { barTicks_ = ticks; }

  /// Live input on the writing track. `out` receives the NoteOffs of composed notes the live NoteOn ends (send them
  /// before the live note).
  Result noteOn(uint32_t clock, uint8_t channel, uint8_t pitch, uint8_t velocity, Emit* out, uint8_t max,
                uint8_t& emitted) {
    emitted = 0;
    if (mode_ == Mode::Idle) return Result::NotWriting;
    if (writer_->noteOn(clock, channel, pitch, velocity) != WriterResult::Ok) return Result::Refused;
    ++held_[pitch & 0x7F];
    if (mode_ == Mode::Overdubbing)
      emitted = stamp(wTrack_, out, tracks_[wTrack_]->liveNoteOn(pitch, local(wTrack_, clock), out, max));
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

  /// The Clock tick for `track`: the track plays (adopting a prepared window at a boundary), then the writing track
  /// wraps at its loop end, takes the provisional source before it, or prepares a Record take's window 0.
  uint8_t tick(uint8_t track, uint32_t clock, Emit* out, uint8_t max) {
    uint8_t n = stamp(track, out, tracks_[track]->tick(local(track, clock), out, max));
    if (track != wTrack_) return n;
    if (mode_ == Mode::Overdubbing) {
      const uint32_t pos = local(track, clock) % loop_;
      if (pos == 0 && clock != wStart_) return static_cast<uint8_t>(n + wrap(clock, out + n, static_cast<uint8_t>(max - n)));
      if (pos == prepPosition() && pos != 0) selectProvisional(clock);
    } else if (mode_ == Mode::Recording) {  // rows up to `len` are in: at the window end, and at bar lines inside it
      const uint32_t len = clock - wStart_, w = tracks_[track]->windowTicks();
      if (len > preparedLen_ && len <= w && (len == w || (barTicks_ != 0 && len % barTicks_ == 0))) prepareRecord(len);
    }
    return n;
  }

  /// The loop position at which the provisional source for the coming wrap is taken.
  uint32_t prepPosition() const { return loop_ - (loop_ / 2 < kLeadTicks ? loop_ / 2 : kLeadTicks); }

  Result undo(uint8_t slot, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (!history_->undo(slot)) return Result::NoChange;
    emitted = selectSlot(slot, clock, false, out, max);
    return Result::Ok;
  }
  Result redo(uint8_t slot, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (!history_->redo(slot)) return Result::NoChange;
    emitted = selectSlot(slot, clock, false, out, max);
    return Result::Ok;
  }

  Mode mode() const { return mode_; }
  uint8_t playingSlot(uint8_t track) const { return slot_[track]; }
  bool playing(uint8_t track) const { return playing_[track]; }
  /// The slot's current state with content, or kNoManifest: empty, never recorded, or cleared (E9.4).
  ManifestId contentOf(uint8_t slot) const {
    const ManifestId m = history_->current(slot);
    return m == kNoManifest || manifests_->manifest(m).runCount == 0 ? kNoManifest : m;
  }
  /// The track that records or overdubs, or 0xFF.
  uint8_t writingTrack() const { return mode_ == Mode::Idle ? 0xFF : wTrack_; }
  uint8_t writingSlot() const { return mode_ == Mode::Idle ? kNoSlot : wSlot_; }
  /// Where the take is at `clock`: the loop position while overdubbing, the take's length so far while recording.
  uint32_t writePosition(uint32_t clock) const {
    return mode_ == Mode::Overdubbing ? local(wTrack_, clock) % loop_ : clock - wStart_;
  }

  /// Clear (owner 2026-11-05: undoable): an empty state on top of the slot's history; its playing tracks stop.
  Result clear(uint8_t slot, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (mode_ != Mode::Idle && wSlot_ == slot) return Result::Busy;
    if (contentOf(slot) == kNoManifest) return Result::NoChange;
    const ManifestId empty = manifests_->create(slot, ++revision_, 0, nullptr, 0, *runs_);
    if (empty == kNoManifest) return Result::DeniedManifest;
    history_->commit(slot, empty);
    emitted = selectSlot(slot, clock, false, out, max);
    return Result::Ok;
  }

  /// An empty loop (owner 2026-11-08: a slot's long press, for step entry in NOTE_EDIT): one sealed run without rows
  /// and loop `loop` ticks, on top of the slot's history (undoable). A slot holds content while its state lists a run,
  /// so the loop plays, is edited and saved like a recorded one. Only on an empty slot.
  Result createEmpty(uint8_t slot, uint32_t loop) {
    if (mode_ != Mode::Idle && wSlot_ == slot) return Result::Busy;
    if (contentOf(slot) != kNoManifest) return Result::DeniedSlot;
    if (loop == 0) return Result::NoChange;
    const RunId r = runs_->allocate(slot, RunKind::kRecord);
    if (r == kNoRun) return Result::DeniedRun;
    runs_->seal(r);
    const ManifestId m = manifests_->create(slot, ++revision_, loop, &r, 1, *runs_);
    runs_->release(r);  // the manifest holds it
    if (m == kNoManifest) return Result::DeniedManifest;
    history_->commit(slot, m);
    return Result::Ok;
  }

  /// Loop length (DEC-257, owner 2026-11-08: a play window): a state of the slot's content runs at loop `loop`, with a
  /// fit run re-wrapping them there (the previous one left out); notes keep their lengths, those past a shorter end are
  /// kept and not played. One undo step; tracks playing the slot follow from the next boundary.
  Result resize(uint8_t slot, uint32_t loop, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (mode_ != Mode::Idle && wSlot_ == slot) return Result::Busy;
    const ManifestId base = contentOf(slot);
    if (base == kNoManifest) return Result::Empty;
    const auto& b = manifests_->manifest(base);
    if (loop == 0 || loop == b.loopLengthTicks) return Result::NoChange;
    RunId list[Manifests::kMaxRuns];
    uint8_t n = 0;
    for (uint8_t i = 0; i < b.runCount; ++i)
      if (runs_->run(b.runs[i]).kind != RunKind::kFit) list[n++] = b.runs[i];
    RunId fit = kNoRun;
    const FitResult f = fit_.build(*pool_, *runs_, writer_->reservations(), slot, list, n, loop, fit);
    if (f == FitResult::DeniedRun) return Result::DeniedRun;
    if (f != FitResult::Ok && f != FitResult::None) return Result::Refused;
    if (fit != kNoRun) {
      if (n == Manifests::kMaxRuns) {
        runs_->release(fit);
        return Result::Refused;
      }
      list[n++] = fit;
    }
    const ManifestId m = manifests_->create(slot, ++revision_, loop, list, n, *runs_);
    if (fit != kNoRun) runs_->release(fit);  // the manifest holds it
    if (m == kNoManifest) return Result::DeniedManifest;
    history_->commit(slot, m);
    emitted = selectSlot(slot, clock, false, out, max);
    return Result::Ok;
  }

  /// Loop start (owner 2026-11-08: rotate): a state whose content runs are the slot's rotated `shift` ticks earlier
  /// (`FitBuilder::rotate`: notes keep their lengths, one crossing the new loop start wraps), with a fit run rebuilt for
  /// them. Lossless — rotating by `loop − shift` gives the content back. One undo step; tracks playing the slot follow
  /// from the next boundary.
  Result rotate(uint8_t slot, uint32_t shift, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    if (mode_ != Mode::Idle && wSlot_ == slot) return Result::Busy;
    const ManifestId base = contentOf(slot);
    if (base == kNoManifest) return Result::Empty;
    const auto& b = manifests_->manifest(base);
    const uint32_t loop = b.loopLengthTicks;
    shift %= loop;
    if (shift == 0) return Result::NoChange;
    RunId list[Manifests::kMaxRuns];
    uint8_t n = 0;
    Result result = Result::Ok;
    for (uint8_t i = 0; i < b.runCount && result == Result::Ok; ++i) {
      if (runs_->run(b.runs[i]).kind == RunKind::kFit) continue;
      RunId r;
      const FitResult f = fit_.rotate(*pool_, *runs_, writer_->reservations(), slot, b.runs[i], loop, shift, r);
      if (f == FitResult::Ok)
        list[n++] = r;
      else
        result = f == FitResult::DeniedRun ? Result::DeniedRun : Result::Refused;
    }
    RunId fit = kNoRun;
    if (result == Result::Ok) {
      const FitResult f = fit_.build(*pool_, *runs_, writer_->reservations(), slot, list, n, loop, fit);
      if (f == FitResult::DeniedRun) result = Result::DeniedRun;
      else if (f != FitResult::Ok && f != FitResult::None) result = Result::Refused;
      else if (fit != kNoRun && n == Manifests::kMaxRuns) result = Result::Refused;
      else if (fit != kNoRun) list[n++] = fit;
    }
    ManifestId m = kNoManifest;
    if (result == Result::Ok) {
      m = manifests_->create(slot, ++revision_, loop, list, n, *runs_);
      if (m == kNoManifest) result = Result::DeniedManifest;
    }
    for (uint8_t i = 0; i < n; ++i) runs_->release(list[i]);  // the manifest holds them, or nothing does
    if (fit != kNoRun && (n == 0 || list[n - 1] != fit)) runs_->release(fit);
    if (result != Result::Ok) return result;
    history_->commit(slot, m);
    emitted = selectSlot(slot, clock, false, out, max);
    return Result::Ok;
  }

  /// Starts composing `slot` for `track` from its loop start; `commitLaunch` makes it play.
  Result launch(uint8_t track, uint8_t slot) {
    if (mode_ != Mode::Idle && wTrack_ == track) return Result::Busy;
    const ManifestId m = contentOf(slot);
    if (m == kNoManifest) return Result::Empty;
    tracks_[track]->launch(slot, m);
    launchSlot_[track] = slot;
    return Result::Ok;
  }
  /// The launched slot plays on `track` from Clock tick `clock` (before that tick is played): its loop position 0.
  /// What sounded ends now (`out`, at least T). Without a launch in progress (an Undo superseded it), one starts now.
  Result commitLaunch(uint8_t track, uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted) {
    emitted = 0;
    const uint8_t slot = launchSlot_[track];
    if (slot == kNoSlot) return Result::NoChange;
    if (mode_ != Mode::Idle && wTrack_ == track) return Result::Busy;
    launchSlot_[track] = kNoSlot;
    const ManifestId m = contentOf(slot);
    if (m == kNoManifest) {  // cleared meanwhile
      emitted = stopTrack(track, clock, out, max);
      return Result::Empty;
    }
    if (!tracks_[track]->launchPending()) tracks_[track]->launch(slot, m);
    emitted = stamp(track, out, tracks_[track]->commitLaunch(out, max));
    slot_[track] = slot;
    origin_[track] = clock;
    playing_[track] = true;
    return Result::Ok;
  }
  uint8_t launchingSlot(uint8_t track) const { return launchSlot_[track]; }
  /// A launch in progress for `track` is dropped (a queued switch cancelled or replaced).
  void stopLaunch(uint8_t track) {
    if (launchSlot_[track] == kNoSlot) return;
    launchSlot_[track] = kNoSlot;
    tracks_[track]->cancelLaunch();
  }
  uint32_t loopLength(ManifestId m) const { return manifests_->manifest(m).loopLengthTicks; }
  /// The slot's state changed outside the transport (a NOTE_EDIT publish): its playing tracks follow inside the window
  /// playing (`PlaybackTrack::edit`; an empty slot stops them, NoteOffs in `out`).
  uint8_t follow(uint8_t slot, uint32_t clock, Emit* out, uint8_t max) {
    uint8_t n = 0;
    const ManifestId m = contentOf(slot);
    for (uint8_t t = 0; t < Tracks; ++t) {
      if (slot_[t] != slot || !playing_[t]) continue;
      if (m == kNoManifest)
        n = static_cast<uint8_t>(
            n + stamp(t, out + n, tracks_[t]->stop(local(t, clock), out + n, static_cast<uint8_t>(max - n))));
      else
        tracks_[t]->edit(slot, m, local(t, clock));
    }
    return n;
  }
  /// A stopped track takes `slot` (e.g. an empty one, ready to record) without playing it.
  void assign(uint8_t track, uint8_t slot) {
    if (!playing_[track]) slot_[track] = slot;
  }
  /// The track stops playing (its slot stays assigned); what sounded ends (`out`, at least T).
  uint8_t stopTrack(uint8_t track, uint32_t clock, Emit* out, uint8_t max) {
    playing_[track] = false;
    launchSlot_[track] = kNoSlot;
    return stamp(track, out, tracks_[track]->stop(local(track, clock), out, max));
  }
  uint32_t origin(uint8_t track) const { return origin_[track]; }
  const Stats& stats() const { return stats_; }

 private:
  uint32_t local(uint8_t track, uint32_t clock) const { return clock - origin_[track]; }
  /// Emits carry their track (E9.3): the player does not know it.
  static uint8_t stamp(uint8_t track, Emit* out, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) out[i].track = track;
    return n;
  }

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

  /// Every track playing `slot` follows its current state from the first boundary after `clock` — at `clock` itself
  /// when that tick is not played yet (`played` false: a gesture). Returns NoteOffs written to `out`.
  uint8_t selectSlot(uint8_t slot, uint32_t clock, bool played, Emit* out, uint8_t max) {
    uint8_t n = 0;
    const ManifestId m = contentOf(slot);
    for (uint8_t t = 0; t < Tracks; ++t) {
      if (slot_[t] != slot || !playing_[t]) continue;
      if (m == kNoManifest)
        n = static_cast<uint8_t>(
            n + stamp(t, out + n, tracks_[t]->stop(local(t, clock), out + n, static_cast<uint8_t>(max - n))));
      else
        tracks_[t]->select(slot, m, played || local(t, clock) == 0 ? local(t, clock) : local(t, clock) - 1);
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
    if (currentChanged) selectSlot(wSlot_, clock, true, nullptr, 0);  // input-time; a current with content: no NoteOffs
  }
  template <typename M>
  static bool lists(const M& m, RunId run) {
    for (uint8_t i = 0; i < m.runCount; ++i)
      if (m.runs[i] == run) return true;
    return false;
  }

  /// The writing track plays `current + active run` (as recorded so far) from the coming wrap.
  void selectProvisional(uint32_t clock) {
    const RunId active = writer_->activeRun();
    if (active == kNoRun || runs_->run(active).rowCount == 0) return;  // nothing new: the current plays on
    const ManifestId p = publish(history_->current(wSlot_), active);
    if (p == kNoManifest) return;
    tracks_[wTrack_]->select(wSlot_, p, local(wTrack_, clock));
    manifests_->release(p, *runs_);  // the track's lease holds it while it is composed and played
    ++stats_.provisionalSelections;
  }
  /// E9.8j: the take's pickups as one run of loop positions — NoteOn at `loop - before`, NoteOff at `loop + off`
  /// (the wrap stream when past the loop end), at least a tick and less than a loop long — written in tick order.
  RunId writePickups(const Pickup* pk, uint8_t n) {
    if (n > kMaxPickups) n = kMaxPickups;
    struct Ev {
      uint32_t tick;
      uint8_t i;
      bool on;
    };
    Ev ev[2 * kMaxPickups];
    uint8_t m = 0;
    for (uint8_t i = 0; i < n; ++i) {
      if (pk[i].before == 0 || pk[i].before >= loop_) continue;
      const uint32_t on = loop_ - pk[i].before;
      const int64_t want = static_cast<int64_t>(loop_) + pk[i].off;
      const uint32_t off = want <= on ? on + 1 : want >= on + loop_ ? on + loop_ - 1 : static_cast<uint32_t>(want);
      ev[m++] = Ev{on, i, true};
      ev[m++] = Ev{off, i, false};
    }
    if (m == 0 || writer_->begin(wSlot_, RunKind::kRecord, loop_, 0) != WriterResult::Ok) {
      stats_.pickupsDropped += n;
      return kNoRun;
    }
    for (uint8_t a = 1; a < m; ++a)  // by tick; a NoteOff before a NoteOn on the same tick
      for (uint8_t b = a; b > 0 && (ev[b].tick < ev[b - 1].tick || (ev[b].tick == ev[b - 1].tick && !ev[b].on && ev[b - 1].on)); --b) {
        const Ev e = ev[b];
        ev[b] = ev[b - 1];
        ev[b - 1] = e;
      }
    uint32_t last = 0;
    for (uint8_t k = 0; k < m; ++k) {
      const Pickup& p = pk[ev[k].i];
      if (ev[k].on) {
        if (writer_->noteOn(ev[k].tick, p.channel, p.pitch, p.velocity) == WriterResult::Ok) ++stats_.pickupNotes;
      } else {
        writer_->noteOff(ev[k].tick, p.channel, p.pitch, 0);
      }
      last = ev[k].tick;
    }
    writer_->stop(last);
    const RunId r = writer_->lastRun();
    return runs_->run(r).rowCount != 0 ? r : kNoRun;
  }

  void prepareRecord(uint32_t len) {
    preparedLen_ = len;
    const RunId r0 = writer_->activeRun();
    if (runs_->run(r0).rowCount == 0 || tracks_[wTrack_]->playingIdentity() != 0 ||
        (tracks_[wTrack_]->transitionPending() && !tracks_[wTrack_]->prepared()))  // an earlier bar's: composed again
      return;
    const ManifestId p = manifests_->create(wSlot_, ++revision_, kProvisionalLoop, &r0, 1, *runs_);
    if (p == kNoManifest) return;
    tracks_[wTrack_]->prepare(wSlot_, p);
    manifests_->release(p, *runs_);
    ++stats_.recordPrepares;
  }

  Result stopRecord(uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted, uint32_t length, const Pickup* pickups,
                    uint8_t pickupCount) {
    const uint32_t before = manifests_->nextIdentity();
    writer_->stop(clock);
    releaseAllHeld();
    const RunId r0 = writer_->lastRun();
    const uint32_t raw = clock - wStart_;
    loop_ = length != 0 && length < raw && raw - length < length ? length : raw;
    if (loop_ != raw) ++stats_.trimmedTakes;
    mode_ = Mode::Idle;
    const RunId rp = loop_ != 0 && pickupCount != 0 ? writePickups(pickups, pickupCount) : kNoRun;
    const bool takeRows = runs_->run(r0).rowCount != 0;
    Result result = Result::Ok;
    if (loop_ == 0 || (!takeRows && rp == kNoRun)) {
      ++stats_.emptyTakes;
      result = Result::Empty;
    } else {
      RunId list[2];
      uint8_t n = 0;
      if (takeRows) list[n++] = r0;
      if (rp != kNoRun) list[n++] = rp;
      const ManifestId m = manifests_->create(wSlot_, ++revision_, loop_, list, n, *runs_);  // admitted at Record start
      history_->commit(wSlot_, m);
      slot_[wTrack_] = wSlot_;
      origin_[wTrack_] = wStart_;
      playing_[wTrack_] = true;
      const uint32_t w = tracks_[wTrack_]->windowTicks();  // the prepared window 0 holds the rows before `preparedLen_`
      tracks_[wTrack_]->handoff(wSlot_, m, local(wTrack_, clock), raw - loop_, preparedLen_ >= (loop_ < w ? loop_ : w));
    }
    drainSealed(before, clock);
    return result;
  }

  /// `played`: tick `clock` was played already (a take ended by its wrap), else a Stop gesture before it.
  Result stopOverdub(uint32_t clock, Emit* out, uint8_t max, uint8_t& emitted, bool played = false) {
    const uint32_t before = manifests_->nextIdentity();
    writer_->stop(clock);
    releaseAllHeld();
    const RunId last = writer_->lastRun();
    if (runs_->run(last).rowCount != 0) commitStep(last, clock, played);
    drainSealed(before, clock);
    history_->endSession(SessionKind::Overdub);
    mode_ = Mode::Idle;
    emitted = selectSlot(wSlot_, clock, played, out, max);  // the folded step is the current (unchanged identity: no transition)
    return Result::Ok;
  }

  /// A session step on top of the slot's current: published, committed and selected.
  void commitStep(RunId run, uint32_t clock, bool played) {
    const ManifestId m = publish(history_->current(wSlot_), run);
    if (m == kNoManifest) return;  // W budgets a manifest per writer commit (EngineConfig)
    history_->commit(wSlot_, m);
    selectSlot(wSlot_, clock, played, nullptr, 0);
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
      stopOverdub(clock, out, max, n, true);
      return n;
    }
    if (runs_->run(wrapped).rowCount == 0) {
      ++stats_.emptyWraps;
    } else {
      if (runs_->run(wrapped).state == RunState::Writing) ++stats_.closingPublished;
      commitStep(wrapped, clock, true);
      ++stats_.publishedWraps;
    }
    drainSealed(before, clock);
    return 0;
  }

  Pool* pool_ = nullptr;
  FitBuilder<Pool, Runs, typename WriterT::Reservations> fit_;  // a resize's scratch (DEC-257)
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  WriterT* writer_ = nullptr;
  Track* tracks_[Tracks] = {};
  uint8_t slot_[Tracks] = {};
  bool playing_[Tracks] = {};
  uint8_t launchSlot_[Tracks] = {};  // a launch composing for the track (kNoSlot: none)
  uint32_t origin_[Tracks] = {};
  Mode mode_ = Mode::Idle;
  uint8_t wTrack_ = 0, wSlot_ = kNoSlot;
  uint32_t wStart_ = 0, loop_ = 0, revision_ = 0;
  uint32_t preparedLen_ = 0;  // the Record take's length when its window 0 was last prepared (or could not be)
  uint32_t barTicks_ = 0;
  uint8_t held_[128] = {};
  Stats stats_{};
};

}  // namespace ContentEngine
