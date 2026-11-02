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
  /// C5.3b (DEC-246 amendment 24): the device's ticks per owner turn, in the unit the successor-lead measurement counts.
  /// The most ticks (and microseconds) any `kShortSpan` / `kLongSpan` **consecutive** running dispatches of one epoch
  /// spanned in this block: the host measured a successor window at 14-16 owner turns idle and 112-128 under
  /// eight-track arbitration, so these two spans turn those counts into ticks. A discontinuity or a stopped dispatch
  /// restarts the run, so no span crosses one.
  static constexpr uint32_t kShortSpan = 16;
  static constexpr uint32_t kLongSpan = 128;
  uint32_t span16MaxTicks = 0;
  uint32_t span128MaxTicks = 0;
  uint32_t span128MaxUs = 0;

  /// `permittedAdvanceTicksIn` defaults to **0 = no comparison**: a caller that does not know the declaration (the
  /// clock-plumbing fixtures, which observe the epoch/tick rule on its own) records no overrun rather than inventing one.
  void observe(PlaybackDeadlineClock clock, bool running, bool eligible,
               bool progressed, bool denied, uint32_t workerUs, uint32_t permittedAdvanceTicksIn = 0,
               uint32_t nowUs = 0) {
    permittedAdvanceTicks = permittedAdvanceTicksIn;
    // The consecutive-dispatch spans: a running dispatch of the same epoch with a non-decreasing tick extends the run;
    // anything else starts a new one at this dispatch (a stopped one leaves it empty).
    if (!running || clock.epoch == 0 || (runLength_ != 0 && (clock.epoch != runEpoch_ ||
        static_cast<uint32_t>(clock.tick) < spanTick_[(runLength_ - 1) % kLongSpan])))
      runLength_ = 0;
    if (running && clock.epoch != 0) {
      const uint32_t tick = static_cast<uint32_t>(clock.tick);
      runEpoch_ = clock.epoch;
      const uint32_t slot = runLength_ % kLongSpan;
      ++runLength_;
      if (runLength_ > kShortSpan) {
        const uint32_t back = spanTick_[(runLength_ - 1 - kShortSpan) % kLongSpan];
        if (tick - back > span16MaxTicks) span16MaxTicks = tick - back;
      }
      if (runLength_ > kLongSpan) {
        // The oldest entry still in the ring is exactly `kLongSpan` dispatches back: read it before overwriting it.
        const uint32_t back = spanTick_[slot];
        const uint32_t backUs = spanUs_[slot];
        if (tick - back > span128MaxTicks) span128MaxTicks = tick - back;
        if (nowUs != 0 && nowUs - backUs > span128MaxUs) span128MaxUs = nowUs - backUs;
      }
      spanTick_[slot] = tick;
      spanUs_[slot] = nowUs;
    }
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
    span16MaxTicks = span128MaxTicks = span128MaxUs = 0;  // the run itself continues across blocks
  }
 private:
  uint32_t spanTick_[kLongSpan] = {};
  uint32_t spanUs_[kLongSpan] = {};
  uint32_t runLength_ = 0;
  uint32_t runEpoch_ = 0;
  PlaybackDeadlineClock last_{};
  bool seen_ = false;
  bool wasRunning_ = false;
  bool wasEligible_ = false;
};
