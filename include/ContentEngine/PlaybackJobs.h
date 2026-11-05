//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Playback compose work as scheduler jobs (fixed-memory-engine E6.2, plan revision 3 § 7.3 / § 7.5). Per track two
// jobs, each submitted only while its work is pending and never twice:
//   * steady: the window for the track's next boundary. Hard, deadline = that boundary; declared grains = the measured
//     worst window (100 grains at 128 row units, E5.8). Missing it is an underrun.
//   * transition: cold-start replay plus the new source's window. Transition class (owner ruling 2026-11-05): bounded
//     progress — at least every second grain while the hard load allows — and priority over soft work, ordered by
//     target boundary. If it is not ready at its boundary the previous source keeps playing (pins held), a late source
//     switch is counted, and it is adopted at the first boundary after it is ready.
// `service` is called from the main loop with the Clock position; the jobs run one grain per scheduler turn.

#pragma once

#include <cstdint>

#include "ContentEngine/WorkerScheduler.h"

namespace ContentEngine {

template <typename Track, uint8_t Tracks, typename Scheduler>
class PlaybackJobs {
 public:
  static constexpr uint32_t kRowsPerGrain = 128;
  static constexpr uint32_t kSteadyGrains = 100;       // measured worst window (clustered, E5.8)
  static constexpr uint32_t kTransitionGrains = 3200;  // warm-up cycle + replay + window, plan § 7.2 (spread)
  static constexpr uint32_t kMaxLeadTicks = 1u << 16;  // a deadline at most ~68 s ahead at 300 BPM

  struct Stats {
    uint32_t submitted;
    uint32_t tableFull;  // a job could not be submitted this turn (retried on the next `service`)
  };

  void init(Track* const* tracks, Scheduler& scheduler) {
    scheduler_ = &scheduler;
    for (uint8_t i = 0; i < Tracks; ++i) {
      steady_[i] = Job{tracks[i], true, false, 0};
      transition_[i] = Job{tracks[i], false, false, 0};
    }
    stats_ = Stats{};
  }

  /// Submits the jobs the tracks need at Clock tick `nowTick` (timeline) = `nowUs`, at `usPerTick`.
  void service(uint32_t nowTick, uint32_t nowUs, uint32_t usPerTick) {
    uint32_t now[Tracks];
    for (auto& n : now) n = nowTick;
    service(now, nowUs, usPerTick);
  }
  /// The same with each track's own timeline tick (tracks keep their own loop origin, E9.1).
  void service(const uint32_t* trackTicks, uint32_t nowUs, uint32_t usPerTick) {
    for (uint8_t i = 0; i < Tracks; ++i) {
      const uint32_t nowTick = trackTicks[i];
      Job& s = steady_[i];
      if (!s.live && s.track->steadyPending()) {
        const uint32_t boundary = s.track->boundaryAfter(nowTick);
        s.target = boundary;
        submit(s, JobSpec{JobClass::Hard, nowUs + (boundary - nowTick) * usPerTick, kSteadyGrains, &step, &s});
      }
      Job& t = transition_[i];
      if (!t.live && t.track->transitionWorkPending()) {
        // Bound the lead so the deadline stays representable.
        // Late (the target passed: a late join being prepared) is due now.
        const uint32_t target = t.track->transitionTarget();
        const uint32_t lead = target > nowTick ? target - nowTick : 0;
        submit(t, JobSpec{JobClass::Transition, nowUs + (lead < kMaxLeadTicks ? lead : kMaxLeadTicks) * usPerTick,
                          kTransitionGrains, &step, &t});
      }
    }
  }

  const Stats& stats() const { return stats_; }
  /// The track-timeline boundary track `t`'s live steady job must finish by (0: none live).
  uint32_t steadyTarget(uint8_t t) const { return steady_[t].live ? steady_[t].target : 0; }

 private:
  struct Job {
    Track* track;
    bool steady;
    bool live;
    uint32_t target;  // steady: its boundary on the track's timeline
  };

  void submit(Job& j, const JobSpec& spec) {
    uint8_t id;
    if (scheduler_->submit(spec, id)) {
      j.live = true;
      ++stats_.submitted;
    } else {
      ++stats_.tableFull;
    }
  }

  static JobStep step(void* ctx) {
    Job& j = *static_cast<Job*>(ctx);
    const bool more = j.steady ? (j.track->workSteady(kRowsPerGrain), j.track->steadyPending())
                               : (j.track->workTransition(kRowsPerGrain), j.track->transitionWorkPending());
    if (!more) j.live = false;
    return more ? JobStep::Continue : JobStep::Done;
  }

  Scheduler* scheduler_ = nullptr;
  Job steady_[Tracks];
  Job transition_[Tracks];
  Stats stats_{};
};

}  // namespace ContentEngine
