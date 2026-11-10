//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// One track's playback windows (fixed-memory-engine E6.1, plan revision 3 § 9.1). Three window buffers per track:
// the window playing, the next window of the same source (steady), and the transition window of a new source. Two
// compose cursors: the steady composer continues the playing source window after window; a transition composer is
// positioned by a `ColdStart` (checkpoint or warm-up, E5) at the next boundary when the slot's source changes.
//
// * Windows lie on a fixed grid: position % W == 0 within each loop cycle (the last one ends at the loop end), so a
//   boundary is a timeline tick whose loop position is a multiple of W. A buffer is stamped with its source identity
//   and timeline start; a `Complete` buffer is immutable.
// * Succession is deterministic (§ 9.1). At a boundary the track takes the `Complete` window stamped with the wanted
//   source and starting at the boundary; otherwise the next window of the source already playing, and counts a late
//   source switch; otherwise nothing (an underrun, counted: notes already sounding still end on time). A window that starts
//   before the boundary is stale. A composer (steady or transition) that missed its boundary moves its target to the
//   next boundary and composes, then discards, windows that start before it; composing a window takes less than a
//   window's time, so it catches up.
// * Work is grains (`work`): the steady window first, then the transition (cold-start replay, then its window). The
//   caller runs them as hard jobs with the boundary as deadline. `tick` does no composition and no page read.
// No allocation; `init()` writes every field.

#pragma once

#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/MidiClass.h"
#include "ContentEngine/SpanWindow.h"

namespace ContentEngine {

template <typename Composer, typename Table, typename Window, typename Manifests, typename Runs, uint16_t T>
class PlaybackTrack {
 public:
  using Cold = ColdStart<Composer, Table, Window, Manifests, Runs>;
  using Player = WindowPlayer<T>;
  using Emit = typename Player::Emit;
  static constexpr uint8_t kNone = 0xFF;

  struct Stats {
    uint32_t switches;       // transitions taken at their boundary or later
    uint32_t lateSourceSwitches;  // boundaries where the selected source was not ready and the previous one continued
    uint32_t accepted;            // source selections accepted (a selection superseding an unfinished one included)
    uint32_t lastLatencyTicks;    // acceptance to audible, the latest adopted selection
    uint32_t maxLatencyTicks;     // acceptance to audible, worst adopted selection
    uint32_t lastBoundaries;      // eligible window boundaries from acceptance to audible adoption (the adopting one
    uint32_t maxBoundaries;       // included, missed ones counted): 1 = the first boundary after acceptance
    uint32_t lastTransitionGrains;  // compose grains the latest adopted selection took (replay, window, catch-up)
    uint32_t checkpointStarts;      // cold starts that resumed from a checkpoint set of their manifest
    uint32_t warmupStarts;          // cold starts that found none and warmed up from the loop start
    uint32_t underruns;      // boundaries with no window to play
    uint32_t staleWindows;   // windows composed after their boundary had passed, discarded to catch up
    uint32_t refusedStarts;  // cold starts refused (e.g. index capacity): the old source keeps playing
    uint32_t preparedHandoffs;  // Record Stops that played a window composed ahead (E9.1b)
    uint32_t launches;          // slot launches committed (E9.4)
    uint32_t lateJoins;         // transitions adopted inside their window after missing its boundary (bounded)
    uint32_t lateJoinsMissed;   // transitions whose window came after the late-join limit: the boundary rule
    uint32_t lastLateTicks, maxLateTicks;  // ticks after the boundary a late join became audible
    uint32_t joinOverflow;      // join-state entries beyond the prepared lists (not started / not sent)
    uint32_t trimmedHandoffs;   // Record takes handed off inside cycle 1 (trimmed to the bar line before, E9.8i)
    uint32_t editJoins;         // NOTE_EDIT publishes joined inside the playing window
  };

  void init(Composer& a, Composer& b, Window* buffers[3], const Table& table, Manifests& manifests, Runs& runs,
            Player& player, uint32_t windowTicks) {
    comp_[0] = &a;
    comp_[1] = &b;
    cold_[0] = Cold{};  // RAM2 is not zeroed at reset: a poisoned lease would be "closed" by the first start
    cold_[1] = Cold{};
    for (uint8_t i = 0; i < 3; ++i) buf_[i] = Buffer{buffers[i], State::Free, 0, 0};
    table_ = &table;
    manifests_ = &manifests;
    runs_ = &runs;
    player_ = &player;
    window_ = windowTicks;
    loop_ = 0;
    steady_ = kNone;
    playing_ = next_ = trans_ = kNone;
    playingIdentity_ = 0;
    wanted_ = kNoManifest;
    wantedIdentity_ = 0;
    transActive_ = false;
    transComposing_ = false;
    prepared_ = false;
    launching_ = false;
    launchLoop_ = 0;
    launchStart_ = 0;
    launchJoin_ = false;
    handoffFinal_ = kNoManifest;
    late_ = joinUnbounded_ = false;
    joinBeat_ = 0;
    transComp_ = 0;
    transTarget_ = steadyTarget_ = 0;
    stats_ = Stats{};
  }

  /// The slot's current source changed to `manifest` (or it starts playing) at timeline tick `now`. The transition
  /// targets the next boundary after `now`; meanwhile the playing source continues.
  void select(uint8_t slot, ManifestId manifest, uint32_t now) { transition(slot, manifest, now, false); }

  /// Get a loop's audition (DEC-271 amendment 1; owner: "Need beat line"): `manifest` heard from the first beat line
  /// after `now` (`beatTicks`, 0: the next boundary, as `select`). The window playing is composed from it and joined
  /// there late: a note it sounds across the join starts at the join (heard as if it had been playing), what the old
  /// source sounded ends; a join the composition misses moves to the next beat. A beat that is the window's end is the
  /// boundary: the ordinary switch.
  void audition(uint8_t slot, ManifestId manifest, uint32_t now, uint32_t beatTicks) {
    const uint32_t loop = manifests_->manifest(manifest).loopLengthTicks;
    if (beatTicks == 0 || loop == 0) {
      select(slot, manifest, now);
      return;
    }
    const uint32_t start = now / loop * loop + now % loop / window_ * window_;
    const uint32_t end = start + window_ < (now / loop + 1) * loop ? start + window_ : (now / loop + 1) * loop;
    if ((now / beatTicks + 1) * beatTicks >= end) {
      select(slot, manifest, now);
      return;
    }
    transition(slot, manifest, now, false, beatTicks);
  }

  /// A NOTE_EDIT publish while playing (owner 2026-11-07: changes heard at once, not from the next boundary): the
  /// window playing is composed from `manifest` and joined at the first tick after `now` it is ready for. Nothing
  /// starts late — a note whose start has passed is heard from its next start (a note moved just behind the playhead
  /// is not heard this cycle; one moved just ahead plays); a sounding note that the edited source sounds there on the
  /// same pitch and ending inside the window keeps sounding (its own instance, the new end); the others end at the join
  /// (one sounding past the window's end too: heard again from its next start). Events of the passed span are not sent
  /// again. A join the window's end overtakes joins inside the next window.
  void edit(uint8_t slot, ManifestId manifest, uint32_t now) {
    const uint32_t next = now + 1;
    if (loop_ == 0 || playing_ == kNone || (next % loop_) % window_ == 0) {  // an edit keeps the loop's length
      select(slot, manifest, now);
      return;
    }
    transition(slot, manifest, now, true);
  }

 private:
  void transition(uint8_t slot, ManifestId manifest, uint32_t now, bool editJoin, uint32_t beat = 0) {
    const auto& m = manifests_->manifest(manifest);
    wanted_ = manifest;
    wantedIdentity_ = m.identity;
    if (playing_ != kNone && wantedIdentity_ == playingIdentity_) {  // back to what plays: no transition
      cancelTransition();
      return;
    }
    cancelTransition();
    ++stats_.accepted;
    selectTick_ = now;
    transBoundaries_ = 0;
    transGrains_ = 0;
    loop_ = m.loopLengthTicks;
    const bool join = editJoin || beat != 0;  // an edit, or an audition on the beat: the window playing
    const uint32_t b = join ? now / loop_ * loop_ + now % loop_ / window_ * window_ : nextBoundary(now);
    const uint8_t tc = steady_ == kNone ? 0 : static_cast<uint8_t>(1 - steady_);
    trans_ = freeBuffer();
    buf_[trans_].state = State::Filling;
    transComp_ = tc;
    transActive_ = true;
    transTarget_ = b;
    transComposing_ = false;
    if (cold_[tc].start(*comp_[tc], *table_, *manifests_, *runs_, slot, manifest, b % loop_, b / loop_, window_) ==
        Cold::Status::Refused) {
      ++stats_.refusedStarts;
      cancelTransition();
      return;
    }
    if (join) {
      late_ = true;
      lateBoundary_ = b;
      joinUnbounded_ = true;
      editJoin_ = editJoin;
      joinBeat_ = beat;
    }
  }

 public:

  /// E9.1b, a Record take: window 0 of the take's cycle 1, composed ahead while recording from a provisional source
  /// (the take's prefix, under a loop longer than any of its rows). A single pass carries nothing across its loop end
  /// that is known before Stop, so the provisional source's window 0 of cycle 0 is that window, composed without the
  /// cold start's warm-up (which would replay the whole provisional loop first). Only on a track that plays nothing; the
  /// track keeps no loop grid until `handoff`.
  void prepare(uint8_t slot, ManifestId provisional) {
    if (steady_ != kNone || playing_ != kNone) return;
    cancelTransition();
    trans_ = freeBuffer();
    buf_[trans_].state = State::Filling;
    transComp_ = 0;
    transActive_ = true;
    transTarget_ = 0;  // cycle 0, position 0 (moved to the Stop tick by `handoff`)
    transComposing_ = false;
    prepared_ = true;
    if (cold_[0].start(*comp_[0], *table_, *manifests_, *runs_, slot, provisional, 0, 0, window_, false) ==
        Cold::Status::Refused) {
      ++stats_.refusedStarts;
      cancelTransition();
    }
  }

  /// E9.1b, Record Stop before timeline tick `t` is played (`t`: cycle 1, position `position` of `final`, whose loop
  /// is now known): a complete prepared window becomes the transition for cycle 1's start; when it is adopted, `final`
  /// is selected for the next boundary and the prepared window's cursor ends (its loop was provisional), so until
  /// `final` is adopted there is no steady window. `position` 0: adopted at `t`, a boundary. Otherwise (E9.8i, a take
  /// trimmed to the bar line before `t`) cycle 1 began `position` ticks before `t`: the window joins late at `t` —
  /// what lies before is not played, a note still sounding there starts — with no late-join limit (no boundary was
  /// missed). Without a complete prepared window, or when `usePrepared` is false (it does not cover `final`'s loop):
  /// `final` is selected from `t`.
  void handoff(uint8_t slot, ManifestId final, uint32_t t, uint32_t position = 0, bool usePrepared = true) {
    if (!usePrepared || !prepared_ || !transActive_ || buf_[trans_].state != State::Complete) {
      select(slot, final, t == 0 ? 0 : t - 1);
      return;
    }
    Buffer& b = buf_[trans_];
    const uint32_t start = t - position;  // cycle 1's start
    b.w->cycleBase = start - b.w->start;  // positions stay; this cycle starts there
    b.start = start;
    transTarget_ = start;
    loop_ = manifests_->manifest(final).loopLengthTicks;
    prepared_ = false;
    handoffSlot_ = slot;
    handoffFinal_ = final;
    ++stats_.preparedHandoffs;
    if (position != 0) {
      late_ = true;
      lateBoundary_ = start;
      joinUnbounded_ = true;
      beginJoin(t);
      ++stats_.trimmedHandoffs;
    }
  }
  /// A Record take's window 0 is prepared (`prepare`) and not yet handed off.
  bool prepared() const { return prepared_; }

  /// E9.4, a slot launch: `manifest` will start from its loop start — a fresh start, nothing carried — when the caller
  /// commits it. Its window 0 composes now, without warm-up, while what plays continues; the transition takes no part
  /// in succession until `commitLaunch`.
  /// `position` (surface slice C, a step or bar button; < the loop): it starts there instead — the caller's timeline
  /// restarts at `launchStart()` (cycle 1, as a mid-loop cold start: notes crossing into it carried). The window holding
  /// it composes now (from a checkpoint, else a warm-up), and the join at `position` is prepared as a late join:
  /// what sounds across it starts there, heard as if it had been playing.
  void launch(uint8_t slot, ManifestId manifest, uint32_t position = 0) {
    cancelTransition();
    const auto& m = manifests_->manifest(manifest);
    if (position != 0 && position < m.loopLengthTicks) {
      launchFrom(slot, manifest, position);
      return;
    }
    const uint8_t tc = steady_ == kNone ? 0 : static_cast<uint8_t>(1 - steady_);
    trans_ = freeBuffer();
    buf_[trans_].state = State::Filling;
    transComp_ = tc;
    transActive_ = true;
    transTarget_ = 0;
    transComposing_ = false;
    launching_ = true;
    launchLoop_ = m.loopLengthTicks;
    wanted_ = manifest;
    wantedIdentity_ = m.identity;
    ++stats_.accepted;
    selectTick_ = 0;
    transBoundaries_ = 0;
    transGrains_ = 0;
    if (cold_[tc].start(*comp_[tc], *table_, *manifests_, *runs_, slot, manifest, 0, 0, window_, false) ==
        Cold::Status::Refused) {
      ++stats_.refusedStarts;
      cancelTransition();
    }
  }
  bool launchPending() const { return launching_ && transActive_; }
  /// The launch's first timeline tick (`commitLaunch` restarts the caller's timeline there): 0, or a position's in cycle 1.
  uint32_t launchStart() const { return launchPending() ? launchStart_ : 0; }
  /// A launch in progress is dropped; what plays continues.
  void cancelLaunch() {
    if (launching_) cancelTransition();
  }
  /// The launch takes effect: the caller's timeline for this track restarts, its next tick is 0. Everything sounding
  /// ends now (returned, `out` must hold T) and the old cursors end; the launched window is adopted at tick 0 when it
  /// is ready, else at the first boundary it is ready for (silence until then, no note left sounding).
  uint8_t commitLaunch(Emit* out, uint8_t max) {
    if (!launchPending()) return 0;
    if (steady_ != kNone) cold_[steady_].release();
    steady_ = kNone;
    if (next_ != kNone) buf_[next_].state = State::Free;
    next_ = kNone;
    if (playing_ != kNone) buf_[playing_].state = State::Free;
    playing_ = kNone;
    playingIdentity_ = 0;
    loop_ = launchLoop_;
    launching_ = false;
    steadyTarget_ = 0;  // the timeline restarts: no old target may mark the new windows stale
    ++stats_.launches;
    const uint32_t start = launchStart_;
    launchStart_ = 0;
    if (launchJoin_) {  // a position inside its window: joined late there (prepared already when the window is ready)
      launchJoin_ = false;
      late_ = true;
      lateBoundary_ = start / window_ * window_;
      joinUnbounded_ = true;
      lastTick_ = start - 1;
    }
    return player_->stopAll(start, out, max);
  }

  uint32_t windowTicks() const { return window_; }
  /// Bounded late join (owner ruling 2026-11-05): a transition that misses its boundary may still become audible
  /// inside that window up to `ticks` later — the caller passes min(24 ticks, 25 ms at the tempo). 0: off.
  void setLateJoinLimit(uint32_t ticks) { lateLimit_ = ticks; }

  /// One grain of compose work (`maxRows` row units): the steady window first, then the transition. False when there
  /// is nothing to do.
  bool work(uint32_t maxRows) { return workSteady(maxRows) || workTransition(maxRows); }

  /// The steady window for the next boundary (a hard job: its deadline is that boundary).
  bool steadyPending() const { return steady_ != kNone && (next_ == kNone || buf_[next_].state == State::Filling); }
  bool workSteady(uint32_t maxRows) {
    if (!steadyPending()) return false;
    composeSteady(maxRows);
    return true;
  }

  /// The transition: cold-start replay, then its window. Late is allowed (the old source keeps playing).
  bool transitionWorkPending() const {
    return transActive_ && (buf_[trans_].state != State::Complete || ((late_ || launchJoin_) && !joinReady_));
  }
  bool workTransition(uint32_t maxRows) {
    if (!transitionWorkPending()) return false;
    ++transGrains_;
    if (buf_[trans_].state == State::Complete) {  // late: prepare the join state, bounded per grain
      scanJoin(maxRows);
      return true;
    }
    if (!transComposing_) {
      const typename Cold::Status st = cold_[transComp_].step(*buf_[trans_].w, maxRows);
      if (st == Cold::Status::Done) {
        transComposing_ = true;
        ++(cold_[transComp_].fromCheckpoint() ? stats_.checkpointStarts : stats_.warmupStarts);
      } else if (st == Cold::Status::Refused) {
        ++stats_.refusedStarts;
        cancelTransition();
      }
      return true;
    }
    Composer& c = *comp_[transComp_];
    Window& w = *buf_[trans_].w;
    if (!c.windowOpen() && !c.openWindow(w, window_)) {
      c.nextCycle();
      c.openWindow(w, window_);
    }
    if (c.composeRows(w, maxRows)) {
      if (w.cycleBase + w.start < transTarget_) {
        ++stats_.staleWindows;  // composed only to advance the cursor: discard, compose the next one
      } else {
        complete(trans_);
        if (late_) beginJoin(joinAt(lastTick_ + 1));
        else if (launchJoin_) beginJoin(launchStart_);  // a launch at a position: its join, before the commit
      }
    }
    return true;
  }

  /// The first boundary after timeline tick `now` on this track's grid (0 before any source).
  uint32_t boundaryAfter(uint32_t now) const { return loop_ == 0 ? 0 : nextBoundary(now); }

  /// One Clock tick at timeline tick `t`: succession at a boundary, then the window's emits.
  uint8_t tick(uint32_t t, Emit* out, uint8_t max) {
    uint8_t n = 0;
    lastTick_ = t;
    if (loop_ != 0 && (t % loop_) % window_ == 0)
      n = succession(t, out, max);
    else if (late_)
      n = lateStep(t, out, max);
    return static_cast<uint8_t>(n + player_->tick(t, out + n, static_cast<uint8_t>(max - n)));
  }

  /// The track stops at tick `t`: every sounding note ends now (returned in `out`, which must hold T), a pending
  /// transition is cancelled, and both compose cursors end, releasing their source pins.
  uint8_t stop(uint32_t t, Emit* out, uint8_t max) {
    cancelTransition();
    if (steady_ != kNone) cold_[steady_].release();
    steady_ = kNone;
    for (auto& b : buf_) b.state = State::Free;
    playing_ = next_ = kNone;
    playingIdentity_ = 0;
    loop_ = 0;
    return player_->stopAll(t, out, max);
  }

  /// Live input recorded on this track (plan § 6.2 "live effect"): the writer accepted a NoteOn on `pitch` at `t`. The
  /// track's sounding instances on the pitch end now (returned, to send before the live NoteOn) and composed NoteOns
  /// on it are suppressed until `liveNoteOff`. From the next cycle the stored run produces the composed equivalent.
  uint8_t liveNoteOn(uint8_t pitch, uint32_t t, Emit* out, uint8_t max) { return player_->liveOccupy(pitch, t, out, max); }
  void liveNoteOff(uint8_t pitch) { player_->liveRelease(pitch); }

  /// The boundary a pending transition aims at (moves on when it is missed).
  uint32_t transitionTarget() const { return transTarget_; }

  uint32_t playingIdentity() const { return playing_ == kNone ? 0 : playingIdentity_; }
  bool transitionPending() const { return transActive_; }
  const Stats& stats() const { return stats_; }

 private:
  enum class State : uint8_t { Free, Filling, Complete, Playing };
  struct Buffer {
    Window* w;
    State state;
    uint32_t identity;
    uint32_t start;  // timeline tick
  };

  uint32_t nextBoundary(uint32_t now) const {
    const uint32_t cycle = now / loop_, pos = now % loop_;
    const uint32_t nextPos = (pos / window_ + 1) * window_;
    return nextPos >= loop_ ? (cycle + 1) * loop_ : cycle * loop_ + nextPos;
  }
  uint8_t freeBuffer() const {
    for (uint8_t i = 0; i < 3; ++i)
      if (buf_[i].state == State::Free) return i;
    return kNone;  // unreachable: playing + next + transition is three
  }
  void complete(uint8_t i) {
    Window& w = *buf_[i].w;
    buf_[i].state = State::Complete;
    buf_[i].identity = w.identity;
    buf_[i].start = w.cycleBase + w.start;
  }
  void composeSteady(uint32_t maxRows) {
    if (next_ == kNone) {
      next_ = freeBuffer();
      buf_[next_].state = State::Filling;
    }
    Composer& c = *comp_[steady_];
    Window& w = *buf_[next_].w;
    if (!c.windowOpen() && !c.openWindow(w, window_)) {
      c.nextCycle();
      c.openWindow(w, window_);
    }
    if (c.composeRows(w, maxRows)) {
      if (w.cycleBase + w.start < steadyTarget_) {
        ++stats_.staleWindows;  // composed only to advance the cursor
      } else {
        complete(next_);
      }
    }
  }
  void cancelTransition() {
    if (!transActive_) return;
    cold_[transComp_].cancel();
    buf_[trans_].state = State::Free;
    trans_ = kNone;
    transActive_ = false;
    prepared_ = false;
    launching_ = false;
    late_ = false;
    joinUnbounded_ = false;
    editJoin_ = false;
    joinBeat_ = 0;
    handoffFinal_ = kNoManifest;
    launchJoin_ = false;
    launchStart_ = 0;
  }
  /// The launch at `position` (`launch`): cycle 1's window holding it, from a checkpoint or a warm-up.
  void launchFrom(uint8_t slot, ManifestId manifest, uint32_t position) {
    const auto& m = manifests_->manifest(manifest);
    const uint32_t loop = m.loopLengthTicks, start = loop + position, boundary = loop + position / window_ * window_;
    const uint8_t tc = steady_ == kNone ? 0 : static_cast<uint8_t>(1 - steady_);
    trans_ = freeBuffer();
    buf_[trans_].state = State::Filling;
    transComp_ = tc;
    transActive_ = true;
    transTarget_ = boundary;
    transComposing_ = false;
    launching_ = true;
    launchLoop_ = loop;
    launchStart_ = start;
    launchJoin_ = start != boundary;
    joinReady_ = false;
    wanted_ = manifest;
    wantedIdentity_ = m.identity;
    ++stats_.accepted;
    selectTick_ = start;
    transBoundaries_ = 0;
    transGrains_ = 0;
    if (cold_[tc].start(*comp_[tc], *table_, *manifests_, *runs_, slot, manifest, boundary - loop, 1, window_) ==
        Cold::Status::Refused) {
      ++stats_.refusedStarts;
      cancelTransition();
    }
  }
  /// The join tick for a join ready at `tick`: that tick, or — an audition on the beat — the first beat line from it.
  uint32_t joinAt(uint32_t tick) const { return joinBeat_ == 0 ? tick : (tick + joinBeat_ - 1) / joinBeat_ * joinBeat_; }

  uint8_t succession(uint32_t t, Emit* out, uint8_t max) {
    // No complete window is ever stale here: a composer that missed a boundary discards every window before its target.
    uint8_t take = kNone;
    bool transition = false;
    const bool trans = transActive_ && !launching_;  // a launch waits for its commit
    if (trans && buf_[trans_].state == State::Complete && buf_[trans_].start == t) {
      take = trans_;
      transition = true;
    } else if (next_ != kNone && buf_[next_].state == State::Complete && buf_[next_].start == t) {
      take = next_;
      if (trans) ++stats_.lateSourceSwitches;
    }
    if (playing_ != kNone) buf_[playing_].state = State::Free;
    playing_ = kNone;
    if (trans && !transition) {
      if (lateLimit_ != 0 && !late_ && transTarget_ == t) {  // missed by a little, maybe: the late-join window opens
        late_ = true;
        lateBoundary_ = t;
        joinReady_ = false;
      } else {  // missed: counted, and aim at the next boundary
        ++transBoundaries_;
        transTarget_ = nextBoundary(t);
        if (editJoin_) {  // an edit join the window's end overtook: it joins inside this window instead
          transTarget_ = t;
          lateBoundary_ = t;
          joinReady_ = false;
          if (buf_[trans_].state == State::Complete) buf_[trans_].state = State::Filling;  // compose on to it
        }
      }
    }
    if (steady_ != kNone && !transition && take == kNone) steadyTarget_ = nextBoundary(t);
    if (take == kNone) {
      if (steady_ != kNone || trans) ++stats_.underruns;
      return 0;
    }
    if (transition) {
      if (steady_ != kNone) cold_[steady_].release();
      if (next_ != kNone) buf_[next_].state = State::Free;
      steady_ = transComp_;  // an older steady target lies before this boundary: no reset needed
      next_ = kNone;
      trans_ = kNone;
      transActive_ = false;
      ++stats_.switches;
      stats_.lastLatencyTicks = t - selectTick_;
      stats_.lastTransitionGrains = transGrains_;
      stats_.lastBoundaries = transBoundaries_ + 1;
      if (stats_.lastBoundaries > stats_.maxBoundaries) stats_.maxBoundaries = stats_.lastBoundaries;
      if (stats_.lastLatencyTicks > stats_.maxLatencyTicks) stats_.maxLatencyTicks = stats_.lastLatencyTicks;
    } else {
      next_ = kNone;
    }
    playing_ = take;
    buf_[take].state = State::Playing;
    playingIdentity_ = buf_[take].identity;
    const uint8_t n = player_->beginWindow(*buf_[take].w, out, max);
    if (transition && handoffFinal_ != kNoManifest) {  // a prepared Record window: the take follows at the next boundary
      cold_[steady_].release();
      steady_ = kNone;
      const ManifestId f = handoffFinal_;
      handoffFinal_ = kNoManifest;
      select(handoffSlot_, f, t);
    }
    return n;
  }

  // ---- Bounded late join ----
  /// The join state for timeline tick `tick`, prepared from the window's start by `scanJoin`.
  void beginJoin(uint32_t tick) {
    joinTick_ = tick;
    scanIdx_ = 0;
    joinIndex_ = kNoIndex;
    soundCount_ = 0;
    eventCount_ = 0;
    const uint8_t carried = buf_[trans_].w->carryCount;
    carryAlive_ = carried >= 32 ? 0xFFFFFFFFu : (1u << carried) - 1u;
    joinReady_ = false;
  }
  /// The join point moved on (the Clock passed it first): the scan continues from where it left the old point. O(2T).
  void extendJoin(uint32_t tick) {
    const Window& w = *buf_[trans_].w;
    joinTick_ = tick;
    if (joinIndex_ != kNoIndex) scanIdx_ = joinIndex_;
    joinIndex_ = kNoIndex;
    const uint32_t joinPos = tick - w.cycleBase;
    for (uint8_t i = 0; i < soundCount_;) {  // notes that ended in the extended span drop out
      const SpanEntry& e = w.entries[sound_[i]];
      if (e.length != kOpenLength && e.tick + e.length <= joinPos)
        sound_[i] = sound_[--soundCount_];
      else
        ++i;
    }
    joinReady_ = false;
  }
  /// One grain of join preparation: at most `maxRows` window entries before the join point.
  void scanJoin(uint32_t maxRows) {
    const Window& w = *buf_[trans_].w;
    const uint32_t joinPos = joinTick_ - w.cycleBase;
    for (uint32_t k = 0; k < maxRows && scanIdx_ < w.count; ++k, ++scanIdx_) {
      const SpanEntry& e = w.entries[scanIdx_];
      if (e.tick > joinPos) break;
      if (e.terminal) {  // an end in the missed span (a TERMINAL may sit one tick late in the entry order)
        if (e.tick < joinPos) endJoinInstance(w, e.noteId, e.runKey);
        continue;
      }
      if (e.tick == joinPos) {  // played normally at the join tick
        if (joinIndex_ == kNoIndex) joinIndex_ = scanIdx_;
        continue;
      }
      const uint8_t type = e.status & 0xF0;
      if (type == 0x90 && e.data2 != 0) {
        if (e.length != kOpenLength && e.tick + e.length <= joinPos) continue;  // wholly missed: not emitted
        if (soundCount_ < 2 * T)
          sound_[soundCount_++] = scanIdx_;
        else
          ++stats_.joinOverflow;
      } else if (type != 0x80 && type != 0x90 && controlClass(e.status, e.data1) != ControlClass::Continuous) {
        if (eventCount_ < kJoinEvents)  // essential state (sustain, program, ...) of the missed span is kept
          events_[eventCount_++] = scanIdx_;
        else
          ++stats_.joinOverflow;
      }
    }
    if (scanIdx_ >= w.count || w.entries[scanIdx_].tick > joinPos) {
      if (joinIndex_ == kNoIndex) joinIndex_ = scanIdx_;
      joinReady_ = true;
    }
  }
  void endJoinInstance(const Window& w, uint32_t noteId, uint16_t runKey) {
    for (uint8_t i = 0; i < soundCount_; ++i) {
      const SpanEntry& s = w.entries[sound_[i]];
      if (s.noteId == noteId && runIdOf(s.runKey) == runIdOf(runKey)) {
        sound_[i] = sound_[--soundCount_];
        return;
      }
    }
    for (uint8_t k = 0; k < w.carryCount && k < 32; ++k)
      if (w.carry[k].noteId == noteId && runIdOf(w.carry[k].runKey) == runIdOf(runKey)) carryAlive_ &= ~(1u << k);
  }
  /// Between boundaries while late: adopt the prepared join at its tick, or give up past the limit.
  uint8_t lateStep(uint32_t t, Emit* out, uint8_t max) {
    if (!transActive_) {
      late_ = joinUnbounded_ = false;
      return 0;
    }
    if (t > lateBoundary_ + (joinUnbounded_ ? window_ : lateLimit_)) {  // the boundary rule: counted, aim on
      late_ = joinUnbounded_ = false;
      ++stats_.lateJoinsMissed;
      ++transBoundaries_;
      transTarget_ = nextBoundary(t);
      if (buf_[trans_].state == State::Complete) buf_[trans_].state = State::Filling;  // compose on to that boundary
      return 0;
    }
    if (buf_[trans_].state != State::Complete || !joinReady_) return 0;
    if (joinTick_ != t) {
      if (joinTick_ < t) extendJoin(joinAt(t + 1));
      return 0;
    }
    return adoptLate(t, out, max);
  }
  uint8_t adoptLate(uint32_t t, Emit* out, uint8_t max) {
    Window& w = *buf_[trans_].w;
    typename TerminalScheduler<T>::Carry keep[3 * T];
    uint8_t nKeep = 0;
    for (uint8_t k = 0; k < w.carryCount && k < 32; ++k)
      if (carryAlive_ & (1u << k)) keep[nKeep++] = {w.carry[k].noteId, runIdOf(w.carry[k].runKey), kOpenEnd};
    const bool edit = editJoin_;
    for (uint8_t i = 0; i < soundCount_; ++i) {
      const SpanEntry& e = w.entries[sound_[i]];
      if (edit) {  // the instance sounding (an edited note's earlier copy included) goes on; nothing starts late
        // Not one whose end lies past the window: the end would come as the edited copy's, which the old instance never
        // matches (the next window of the same source closes nothing) — it would sound on.
        const int32_t r = e.length == kOpenLength ? -1 : player_->soundingRun(e.noteId, e.data1);
        if (r >= 0) keep[nKeep++] = {e.noteId, static_cast<uint16_t>(r), w.cycleBase + e.tick + e.length};
        continue;
      }
      keep[nKeep++] = {e.noteId, runIdOf(e.runKey),
                       e.length == kOpenLength ? kOpenEnd : w.cycleBase + e.tick + e.length};
    }
    if (steady_ != kNone) cold_[steady_].release();
    if (next_ != kNone) buf_[next_].state = State::Free;
    if (playing_ != kNone) buf_[playing_].state = State::Free;
    steady_ = transComp_;
    next_ = kNone;
    playing_ = trans_;
    buf_[playing_].state = State::Playing;
    playingIdentity_ = buf_[playing_].identity;
    trans_ = kNone;
    transActive_ = false;
    late_ = false;
    joinUnbounded_ = false;
    editJoin_ = false;
    joinBeat_ = 0;
    steadyTarget_ = nextBoundary(t);
    ++stats_.switches;
    ++(edit ? stats_.editJoins : stats_.lateJoins);
    stats_.lastLateTicks = t - lateBoundary_;
    if (stats_.lastLateTicks > stats_.maxLateTicks) stats_.maxLateTicks = stats_.lastLateTicks;
    stats_.lastLatencyTicks = t - selectTick_;
    stats_.lastTransitionGrains = transGrains_;
    stats_.lastBoundaries = transBoundaries_ + 1;
    if (stats_.lastBoundaries > stats_.maxBoundaries) stats_.maxBoundaries = stats_.lastBoundaries;
    if (stats_.lastLatencyTicks > stats_.maxLatencyTicks) stats_.maxLatencyTicks = stats_.lastLatencyTicks;
    const uint8_t n =
        player_->beginWindowLate(w, joinIndex_, keep, nKeep, sound_, edit ? 0 : soundCount_, events_,
                                 edit ? 0 : eventCount_, t, out, max);
    if (handoffFinal_ != kNoManifest) {
      cold_[steady_].release();
      steady_ = kNone;
      const ManifestId f = handoffFinal_;
      handoffFinal_ = kNoManifest;
      select(handoffSlot_, f, t);
    }
    return n;
  }

  Composer* comp_[2];
  Cold cold_[2];
  Buffer buf_[3];
  const Table* table_ = nullptr;
  Manifests* manifests_ = nullptr;
  Runs* runs_ = nullptr;
  Player* player_ = nullptr;
  uint32_t window_ = 0, loop_ = 0;
  uint8_t steady_ = kNone, transComp_ = 0;
  uint8_t playing_ = kNone, next_ = kNone, trans_ = kNone;
  uint32_t playingIdentity_ = 0;
  ManifestId wanted_ = kNoManifest;
  uint32_t wantedIdentity_ = 0;
  bool transActive_ = false, transComposing_ = false, prepared_ = false;
  bool launching_ = false;   // a launch composes; succession ignores it until `commitLaunch`
  // Bounded late join.
  static constexpr uint16_t kNoIndex = 0xFFFF;
  static constexpr uint8_t kJoinEvents = 16;
  uint32_t lateLimit_ = 0, lateBoundary_ = 0, lastTick_ = 0, joinTick_ = 0;
  bool late_ = false, joinReady_ = false;
  bool joinUnbounded_ = false;  // a trimmed take's handoff joins late with no limit (E9.8i)
  bool editJoin_ = false;       // the join is a NOTE_EDIT publish's (`edit`): nothing starts late
  uint32_t joinBeat_ = 0;       // DEC-271: an audition joins on this beat grid (0: at the first tick ready)
  uint16_t scanIdx_ = 0, joinIndex_ = kNoIndex;
  uint16_t sound_[2 * T];  // START entries begun before the join and sounding at it
  uint8_t soundCount_ = 0;
  uint32_t carryAlive_ = 0;  // bit k: the window's carried instance k still sounds at the join
  uint16_t events_[kJoinEvents];
  uint8_t eventCount_ = 0;
  uint32_t launchLoop_ = 0;
  uint32_t launchStart_ = 0;  // a launch at a position (slice C): its first timeline tick
  bool launchJoin_ = false;   // ... inside its window: joined late there
  uint8_t handoffSlot_ = 0;
  ManifestId handoffFinal_ = kNoManifest;  // selected once the prepared window it follows is adopted
  uint32_t transTarget_ = 0, steadyTarget_ = 0, selectTick_ = 0, transGrains_ = 0, transBoundaries_ = 0;
  Stats stats_{};
};

}  // namespace ContentEngine
