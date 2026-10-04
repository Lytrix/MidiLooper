// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "Utils/PlaybackDeadlineClock.h"

/// Capture-only aggregation at EVERY lane dispatch, not only selected worker calls.
/// Eligible-predecessor gaps are conservative observations, not proof of continuous
/// pending work: another owner can complete/cancel the shared builder between calls.
struct PlaybackLaneMeasurement {
  uint32_t opportunities = 0;
  uint32_t maxDeltaTicks = 0;
  uint32_t eligiblePredecessorMax = 0;
  uint32_t eligibleTurns = 0;
  uint32_t progressTurns = 0;
  uint32_t deniedTurns = 0;
  uint32_t maxWorkerUs = 0;
  uint32_t discontinuities = 0;
  uint32_t saturatedGaps = 0;

  void observe(PlaybackDeadlineClock clock, bool running, bool eligible,
               bool progressed, bool denied, uint32_t workerUs) {
    if (seen_) {
      if (clock.epoch == 0 || clock.epoch != last_.epoch || clock.tick < last_.tick) {
        ++discontinuities;
      } else if (running && wasRunning_) {
        const uint64_t delta = clock.tick - last_.tick;
        const uint32_t bounded = delta > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(delta);
        if (delta > UINT32_MAX) ++saturatedGaps;
        if (bounded > maxDeltaTicks) maxDeltaTicks = bounded;
        if (wasEligible_ && bounded > eligiblePredecessorMax) eligiblePredecessorMax = bounded;
      }
    }
    ++opportunities;
    eligibleTurns += eligible;
    progressTurns += progressed;
    deniedTurns += denied;
    if (workerUs > maxWorkerUs) maxWorkerUs = workerUs;
    last_ = clock;
    wasRunning_ = running;
    wasEligible_ = eligible;
    seen_ = true;
  }
  // Reset only this reporting block; retain the preceding dispatch across blocks.
  void clearBlock() {
    maxDeltaTicks = eligiblePredecessorMax = eligibleTurns = progressTurns = 0;
    deniedTurns = maxWorkerUs = discontinuities = saturatedGaps = 0;
  }
 private:
  PlaybackDeadlineClock last_{};
  bool seen_ = false;
  bool wasRunning_ = false;
  bool wasEligible_ = false;
};
