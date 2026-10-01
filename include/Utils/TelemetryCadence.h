//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

/// Telemetry cadence for the persistence slice rows.
///
/// **Why this exists (measured, 2026-10-01).** A workspace save of one track's metadata (TrackMeta plus one
/// SlotMeta per slot) emitted a `PERS,work` row **per slice**: 241 rows per slot, **5 951 rows in one boot**
/// (`captures/s4b9-cross-track-11`), and the same save's per-step rows added 1 890 more. That volume made the
/// capture ring shed rows the acceptance surface needs (`RING,shed`) — which then set `SERIAL_INCOMPLETE` and
/// aborted an HITL leg whose subject was unrelated (`captures/s4b9-cross-track-10`). The rows are diagnostic; the
/// ring is evidence. **A diagnostic must not evict the evidence it is meant to explain.**
///
/// The cadence keeps every *phase* — a work item's first slice of a phase always emits, and the phase-completion
/// rows (`payload_done`, `done`) keep their own call sites — while cutting the per-slice volume. The duration the
/// emitted row carries is the **running maximum** of the slices seen so far, not the last slice's, so a bound
/// check over these rows stays a faithful upper bound instead of a sample of it
/// (`scripts/hitl/verify/deferred_persist_large_loop_bounds.py` takes `max` over the rows it sees).

#include <cstdint>

namespace TelemetryCadence {

/// One in every `kSliceLogInterval` persistence slices is logged, plus the first slice of each phase.
inline constexpr uint32_t kSliceLogInterval = 16;

/// One in every `kTailWriteStepLogInterval` loop-snapshot **tail-write** steps is logged, for the hand-rolled
/// per-step row in `WorkspaceSave.cpp` (2 705 rows in one boot before this, `captures/s4b9-cross-track-15`).
inline constexpr uint32_t kTailWriteStepLogInterval = 128;

/// The loop-snapshot tail emitter logs every `kTailStepLogInterval` routine step. Phase changes, progress-signature
/// changes, stalls (`unchangedSigStreak`) and completion always log regardless of this interval, so hang
/// attribution is unchanged — only the routine cadence is coarser.
inline constexpr uint32_t kTailStepLogInterval = 512;

struct SliceLogState {
  uint32_t slices = 0;
  uint32_t maxSliceUs = 0;
};

inline void reset(SliceLogState& state) {
  state = SliceLogState{};
}

/// Advance `state` for one slice and report whether this slice's row should be emitted. `carriedUs` always
/// receives the **running maximum**, so a slice that is not logged cannot hide a spike. `interval == 0` logs
/// every slice (the escape hatch a caller uses when it wants the raw cadence).
inline bool shouldLogSlice(SliceLogState& state, uint32_t sliceUs, uint32_t interval,
                           uint32_t& carriedUs) {
  ++state.slices;
  if (sliceUs > state.maxSliceUs) {
    state.maxSliceUs = sliceUs;
  }
  carriedUs = state.maxSliceUs;
  if (interval == 0) {
    return true;
  }
  return state.slices == 1 || (state.slices % interval) == 0;
}

/// A plain "every `interval`-th occurrence" gate for rows that carry no duration of their own. The first
/// occurrence always logs, so a caller that resets on a new item still shows that item's first step.
inline bool shouldLogOccurrence(uint32_t& count, uint32_t interval) {
  ++count;
  if (interval == 0) {
    return true;
  }
  return count == 1 || (count % interval) == 0;
}

}  // namespace TelemetryCadence
