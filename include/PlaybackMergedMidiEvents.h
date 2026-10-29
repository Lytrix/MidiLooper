#pragma once

#include <cstdint>

#include "MidiEvent.h"

/// Scratch merge cache for one loop playback path (not the musical PlaybackWindow domain object).
struct PlaybackMergedMidiEvents {
  uint32_t builtFromRevision = 0;
  /// Published `LoopSnapshot` identity used as the gather source, or nullptr
  /// when the window came from NOTE_EDIT preview or compatibility gather.
  const void* builtFromSnapshotIdentity = nullptr;
  /// For long loops: tick range covered by `mergedEvents` (windowed gather).
  uint32_t windowStartTick = 0;
  uint32_t windowLengthTicks = 0;
  SessionMidiEventVec mergedEvents;

  void clear() {
    builtFromRevision = 0;
    builtFromSnapshotIdentity = nullptr;
    windowStartTick = 0;
    windowLengthTicks = 0;
    mergedEvents.clear();
  }

  bool empty() const { return mergedEvents.empty(); }
};

/// **DEC-203 — the one merged-window policy, callable by builder and readiness alike.**
///
/// This exists because the two *did* drift: the builder wrote a whole-loop window for a short loop (DEC-197) while the
/// readiness predicate expected the two-bar clamp for every loop (DEC-200), so on device every 3..16-bar slot reported
/// "not ready" forever, re-preparing its window on every turn -- 34 ms `track_update` stalls and patchy content
/// (`captures/session_20261029_133409.log`, a 5-bar slot, 34095 us). One function, two callers, so the next change to
/// the policy cannot disagree with itself.
///
/// `longLoop` is the caller's `Loop::shouldAvoidFullVisualRebuild(loopLength)`. A short loop is the whole loop (the
/// DEC-197 rule, in force until the owner's "one clamp for every length" slice lands); a long loop is two bars at the
/// playhead, clamped by the loop.
inline uint32_t mergedPlaybackWindowLengthTicks(uint32_t loopLengthTicks, bool longLoop,
                                               uint32_t windowTicks) {
  if (!longLoop) return loopLengthTicks;
  return loopLengthTicks < windowTicks ? loopLengthTicks : windowTicks;
}

/// Complete committed playback window only. A partial gather must not invalidate
/// ledger identities.
inline bool isFullLoopMergedPlaybackWindow(const PlaybackMergedMidiEvents& merged,
                                           uint32_t loopLengthTicks) {
  return loopLengthTicks > 0 && merged.windowStartTick == 0 &&
         merged.windowLengthTicks == loopLengthTicks;
}
