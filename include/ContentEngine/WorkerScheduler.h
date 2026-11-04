//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Engine worker scheduler (fixed-memory-engine E5.2, plan revision 3 § 7.5). The main loop calls `runGrain` once per
// turn; it runs exactly one grain of one job and returns, so MIDI service, NoteOff service and display drawing run
// between any two grains (a grain is the only non-preemptible engine unit; the Clock ISR still preempts it).
//
// Policy, per grain:
//   1. Hard jobs (deadline: a window boundary) run earliest-deadline-first.
//   2. Before a soft grain is taken, every hard job's remaining declared grains x g must fit in its slack minus B_max
//      (cumulative, in deadline order). If not, the hard job with the earliest deadline runs.
//   3. While display work is pending, at least every fourth grain goes to display; persistence at least 1 in 16.
//   4. Otherwise hard work runs ahead; with no hard work, background (reclaim, compaction, sweeps) takes the rest,
//      and display / persistence take what background leaves.
// Progress: every job declares its worst-case grain count. A job that runs past it is reported (`overruns`), never
// silently extended; a hard job that completes after its deadline is reported (`missedDeadlines`). The job table is
// fixed (`MaxJobs`); a full table refuses `submit` by name. No allocation.

#pragma once

#include <cstdint>

namespace ContentEngine {

enum class JobClass : uint8_t { Hard, Display, Persistence, Background };
enum class JobStep : uint8_t { Continue, Done };
using JobFn = JobStep (*)(void* ctx);

struct JobSpec {
  JobClass cls;
  uint32_t deadlineUs;      // Hard only: absolute time the job must be complete by
  uint32_t declaredGrains;  // worst-case grain count
  JobFn fn;
  void* ctx;
};

template <uint8_t MaxJobs>
class WorkerScheduler {
 public:
  static constexpr uint8_t kNoJob = 0xFF;

  struct Report {
    uint32_t grains;
    uint32_t hardGrains, displayGrains, persistenceGrains, backgroundGrains;
    uint32_t overruns;         // jobs that ran past their declared grain count
    uint32_t missedDeadlines;  // hard jobs still live, or completing, after their deadline (counted once each)
    uint32_t slackForcedHard;  // soft share deferred because a hard job's slack was short
  };

  /// `grainUs`: measured worst grain g; `blockUs`: B_max, the longest non-engine block in the main loop.
  void init(uint32_t grainUs, uint32_t blockUs) {
    g_ = grainUs;
    bMax_ = blockUs;
    for (auto& j : jobs_) j.live = false;
    sinceDisplay_ = 0;
    sincePersist_ = 0;
    report_ = Report{};
  }

  /// False (CapacityDenied) when the table is full.
  bool submit(const JobSpec& spec, uint8_t& id) {
    for (uint8_t i = 0; i < MaxJobs; ++i)
      if (!jobs_[i].live) {
        jobs_[i] = Job{spec, 0, true, false, false};
        id = i;
        return true;
      }
    id = kNoJob;
    return false;
  }
  void cancel(uint8_t id) {
    if (id < MaxJobs) jobs_[id].live = false;
  }
  bool live(uint8_t id) const { return id < MaxJobs && jobs_[id].live; }

  /// Runs one grain at time `nowUs`. Returns the job id run, or kNoJob when idle.
  uint8_t runGrain(uint32_t nowUs) {
    for (auto& j : jobs_)
      if (j.live && j.spec.cls == JobClass::Hard && !j.missed && static_cast<int32_t>(nowUs - j.spec.deadlineUs) > 0) {
        j.missed = true;
        ++report_.missedDeadlines;
      }
    const uint8_t id = pick(nowUs);
    if (id == kNoJob) return kNoJob;
    Job& j = jobs_[id];
    ++report_.grains;
    switch (j.spec.cls) {
      case JobClass::Hard: ++report_.hardGrains; break;
      case JobClass::Display: ++report_.displayGrains; break;
      case JobClass::Persistence: ++report_.persistenceGrains; break;
      case JobClass::Background: ++report_.backgroundGrains; break;
    }
    sinceDisplay_ = j.spec.cls == JobClass::Display ? 0 : sinceDisplay_ + 1;
    sincePersist_ = j.spec.cls == JobClass::Persistence ? 0 : sincePersist_ + 1;
    ++j.used;
    if (j.used > j.spec.declaredGrains && !j.overran) {
      j.overran = true;
      ++report_.overruns;
    }
    if (j.spec.fn(j.spec.ctx) == JobStep::Done) {
      if (j.spec.cls == JobClass::Hard && !j.missed && static_cast<int32_t>(nowUs + g_ - j.spec.deadlineUs) > 0)
        ++report_.missedDeadlines;
      j.live = false;
    }
    return id;
  }

  const Report& report() const { return report_; }

 private:
  struct Job {
    JobSpec spec;
    uint32_t used;
    bool live;
    bool overran;
    bool missed;
  };

  uint32_t remaining(const Job& j) const { return j.used < j.spec.declaredGrains ? j.spec.declaredGrains - j.used : 1; }

  /// Earliest-deadline hard job, or kNoJob.
  uint8_t earliestHard() const {
    uint8_t best = kNoJob;
    for (uint8_t i = 0; i < MaxJobs; ++i)
      if (jobs_[i].live && jobs_[i].spec.cls == JobClass::Hard &&
          (best == kNoJob || static_cast<int32_t>(jobs_[i].spec.deadlineUs - jobs_[best].spec.deadlineUs) < 0))
        best = i;
    return best;
  }

  /// Every hard job, in deadline order, can finish its remaining grains after one soft grain and one B_max block.
  bool slackAllowsSoftGrain(uint32_t nowUs) const {
    uint8_t order[MaxJobs];
    uint8_t n = 0;
    for (uint8_t i = 0; i < MaxJobs; ++i)
      if (jobs_[i].live && jobs_[i].spec.cls == JobClass::Hard) {
        uint8_t k = n++;
        while (k > 0 && static_cast<int32_t>(jobs_[order[k - 1]].spec.deadlineUs - jobs_[i].spec.deadlineUs) > 0) {
          order[k] = order[k - 1];
          --k;
        }
        order[k] = i;
      }
    uint64_t need = static_cast<uint64_t>(g_) + bMax_;  // the soft grain itself, then one blocking task
    for (uint8_t k = 0; k < n; ++k) {
      const Job& j = jobs_[order[k]];
      need += static_cast<uint64_t>(remaining(j)) * g_;
      const int64_t slack = static_cast<int64_t>(static_cast<int32_t>(j.spec.deadlineUs - nowUs));
      if (static_cast<int64_t>(need) > slack) return false;
    }
    return true;
  }

  uint8_t firstOf(JobClass c) const {
    for (uint8_t i = 0; i < MaxJobs; ++i)
      if (jobs_[i].live && jobs_[i].spec.cls == c) return i;
    return kNoJob;
  }

  uint8_t pick(uint32_t nowUs) {
    const uint8_t hard = earliestHard();
    const uint8_t display = firstOf(JobClass::Display);
    const uint8_t persist = firstOf(JobClass::Persistence);
    const bool softOwed = (display != kNoJob && sinceDisplay_ >= 3) || (persist != kNoJob && sincePersist_ >= 15);
    if (hard != kNoJob && !slackAllowsSoftGrain(nowUs)) {
      if (softOwed) ++report_.slackForcedHard;
      return hard;
    }
    if (display != kNoJob && sinceDisplay_ >= 3) return display;
    if (persist != kNoJob && sincePersist_ >= 15) return persist;
    if (hard != kNoJob) return hard;
    const uint8_t background = firstOf(JobClass::Background);
    if (background != kNoJob) return background;
    if (display != kNoJob) return display;
    return persist;
  }

  Job jobs_[MaxJobs];
  uint32_t g_ = 0, bMax_ = 0;
  uint32_t sinceDisplay_ = 0, sincePersist_ = 0;
  Report report_{};
};

}  // namespace ContentEngine
