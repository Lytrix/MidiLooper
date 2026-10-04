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
  /// The **instrument's own** limit: gaps that overflowed the 32-bit counter. Never a statement about the service
  /// contract -- a jump over a million ticks reports here, and a permitted-bound overrun does not.
  uint32_t saturatedGaps = 0;
  /// DEC-108 §13b / task 3.10 (**the timing qualification's blocker 3**): the permitted per-turn advance this block was
  /// compared against, the number of **eligible-predecessor** gaps that exceeded it, and the worst of them. The limit is
  /// *wired* here -- not merely declared -- so an overrun is an observation the live path makes rather than a figure an
  /// operator has to derive from a maximum. An **idle** gap above the limit is not an overrun: the bound is a
  /// *scheduling* bound, and the sampler's eligibility split is exactly what tells the two apart.
  uint32_t permittedAdvanceTicks = 0;
  uint32_t overruns = 0;
  uint32_t overrunMaxTicks = 0;

  /// `permittedAdvanceTicksIn` defaults to **0 = no comparison**: a caller that does not know the declaration (the
  /// clock-plumbing fixtures, which observe the epoch/tick rule on its own) records no overrun rather than inventing one.
  void observe(PlaybackDeadlineClock clock, bool running, bool eligible,
               bool progressed, bool denied, uint32_t workerUs, uint32_t permittedAdvanceTicksIn = 0) {
    permittedAdvanceTicks = permittedAdvanceTicksIn;
    if (seen_) {
      if (clock.epoch == 0 || clock.epoch != last_.epoch || clock.tick < last_.tick) {
        ++discontinuities;
      } else if (running && wasRunning_) {
        const uint64_t delta = clock.tick - last_.tick;
        const uint32_t bounded = delta > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(delta);
        if (delta > UINT32_MAX) ++saturatedGaps;
        if (bounded > maxDeltaTicks) maxDeltaTicks = bounded;
        if (wasEligible_) {
          if (bounded > eligiblePredecessorMax) eligiblePredecessorMax = bounded;
          // The permitted limit, applied to the quantity it is about: a gap across a turn whose predecessor was
          // *eligible*. Over the limit is an **overrun**, counted and sized here so the row can name it.
          if (permittedAdvanceTicks != 0 && bounded > permittedAdvanceTicks) {
            ++overruns;
            if (bounded > overrunMaxTicks) overrunMaxTicks = bounded;
          }
        }
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
    overruns = overrunMaxTicks = 0;
  }
 private:
  PlaybackDeadlineClock last_{};
  bool seen_ = false;
  bool wasRunning_ = false;
  bool wasEligible_ = false;
};
