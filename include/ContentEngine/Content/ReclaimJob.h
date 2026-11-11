//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Reclaim worker. Runs whose last reference was dropped wait
// in the run table's reclaim queue; this background job returns them to the pool in grains of `kUnitsPerGrain` units
// (a page, or an emptied descriptor). Nothing is freed on the Clock path or inside a command.
//
// A submission declares the grains the queue needs at that moment and ends after them (or when the queue empties), so
// the scheduler never sees an overrun; runs released meanwhile are taken by the next submission (`submitIfPending`).

#pragma once

#include <cstdint>

#include "ContentEngine/WorkerScheduler.h"

namespace ContentEngine {

template <typename Runs, typename Pool>
class ReclaimJob {
 public:
  static constexpr uint32_t kUnitsPerGrain = 64;

  void bind(Runs& runs, Pool& pool) {
    runs_ = &runs;
    pool_ = &pool;
    grainsLeft_ = 0;
    running_ = false;
  }

  /// Submits one background job for the queue as it stands, unless one is live or nothing is queued. False when the
  /// scheduler's table is full (the queue waits; nothing is lost).
  template <typename Scheduler>
  bool submitIfPending(Scheduler& s) {
    if (running_ || runs_->reclaimPendingRuns() == 0) return true;
    grainsLeft_ = (runs_->reclaimPendingUnits() + kUnitsPerGrain - 1) / kUnitsPerGrain;
    const JobSpec spec{JobClass::Background, 0, grainsLeft_, &ReclaimJob::step, this};
    uint8_t id;
    running_ = s.submit(spec, id);
    return running_;
  }

  bool running() const { return running_; }
  uint32_t grainsLeft() const { return grainsLeft_; }

 private:
  static JobStep step(void* ctx) {
    auto* self = static_cast<ReclaimJob*>(ctx);
    self->runs_->reclaim(*self->pool_, kUnitsPerGrain);
    if (self->grainsLeft_ > 0) --self->grainsLeft_;
    if (self->grainsLeft_ != 0 && self->runs_->reclaimPendingRuns() != 0) return JobStep::Continue;
    self->running_ = false;
    return JobStep::Done;
  }

  Runs* runs_ = nullptr;
  Pool* pool_ = nullptr;
  uint32_t grainsLeft_ = 0;
  bool running_ = false;
};

}  // namespace ContentEngine
