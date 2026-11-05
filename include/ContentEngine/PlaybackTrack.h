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
    uint32_t lastTransitionGrains;  // compose grains the latest adopted selection took (replay, window, catch-up)
    uint32_t underruns;      // boundaries with no window to play
    uint32_t staleWindows;   // windows composed after their boundary had passed, discarded to catch up
    uint32_t refusedStarts;  // cold starts refused (e.g. index capacity): the old source keeps playing
  };

  void init(Composer& a, Composer& b, Window* buffers[3], const Table& table, Manifests& manifests, Runs& runs,
            Player& player, uint32_t windowTicks) {
    comp_[0] = &a;
    comp_[1] = &b;
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
    transComp_ = 0;
    transTarget_ = steadyTarget_ = 0;
    stats_ = Stats{};
  }

  /// The slot's current source changed to `manifest` (or it starts playing) at timeline tick `now`. The transition
  /// targets the next boundary after `now`; meanwhile the playing source continues.
  void select(uint8_t slot, ManifestId manifest, uint32_t now) {
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
    transGrains_ = 0;
    loop_ = m.loopLengthTicks;
    const uint32_t b = nextBoundary(now);
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
    }
  }

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
  bool transitionWorkPending() const { return transActive_ && buf_[trans_].state != State::Complete; }
  bool workTransition(uint32_t maxRows) {
    if (!transitionWorkPending()) return false;
    ++transGrains_;
    if (!transComposing_) {
      const typename Cold::Status st = cold_[transComp_].step(*buf_[trans_].w, maxRows);
      if (st == Cold::Status::Done) {
        transComposing_ = true;
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
      }
    }
    return true;
  }

  /// The first boundary after timeline tick `now` on this track's grid (0 before any source).
  uint32_t boundaryAfter(uint32_t now) const { return loop_ == 0 ? 0 : nextBoundary(now); }

  /// One Clock tick at timeline tick `t`: succession at a boundary, then the window's emits.
  uint8_t tick(uint32_t t, Emit* out, uint8_t max) {
    uint8_t n = 0;
    if (loop_ != 0 && (t % loop_) % window_ == 0) n = succession(t, out, max);
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
  }

  uint8_t succession(uint32_t t, Emit* out, uint8_t max) {
    // No complete window is ever stale here: a composer that missed a boundary discards every window before its target.
    uint8_t take = kNone;
    bool transition = false;
    if (transActive_ && buf_[trans_].state == State::Complete && buf_[trans_].start == t) {
      take = trans_;
      transition = true;
    } else if (next_ != kNone && buf_[next_].state == State::Complete && buf_[next_].start == t) {
      take = next_;
      if (transActive_) ++stats_.lateSourceSwitches;
    }
    if (playing_ != kNone) buf_[playing_].state = State::Free;
    playing_ = kNone;
    if (transActive_ && !transition) transTarget_ = nextBoundary(t);  // missed: aim at the next boundary
    if (steady_ != kNone && !transition && take == kNone) steadyTarget_ = nextBoundary(t);
    if (take == kNone) {
      if (steady_ != kNone || transActive_) ++stats_.underruns;
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
      if (stats_.lastLatencyTicks > stats_.maxLatencyTicks) stats_.maxLatencyTicks = stats_.lastLatencyTicks;
    } else {
      next_ = kNone;
    }
    playing_ = take;
    buf_[take].state = State::Playing;
    playingIdentity_ = buf_[take].identity;
    return player_->beginWindow(*buf_[take].w, out, max);
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
  bool transActive_ = false, transComposing_ = false;
  uint32_t transTarget_ = 0, steadyTarget_ = 0, selectTick_ = 0, transGrains_ = 0;
  Stats stats_{};
};

}  // namespace ContentEngine
