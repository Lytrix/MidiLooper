//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <cstddef>

#include "LoopEventStore.h"
#include "MidiEvent.h"

/// Thin committed-chunk traversal view for consumers (display, playback, diagnostics).
/// Full vs windowed strategy is selected at construction; appendTo fills the output vector.
///
/// Wrap-aware invariant: chunk/window intersection MUST use loop-wrap rules consistent with
/// DisplayWindowUtils (linear firstTick/lastTick vs window is insufficient when a span wraps).
class CommittedEventRange {
 public:
  struct WindowTraversalCursor {
    size_t listIndex = 0;
    size_t chunkIndex = 0;
    size_t chunkEventOffset = 0;
    bool complete = false;
  };

  struct WindowTraversalResult {
    size_t eventsAppended = 0;
    bool complete = false;
    bool resumed = false;
    /// Chunks copied out of the store and filtered this call (the gate's admitted chunks). The span read that decides
    /// admission is O(1) per chunk; the copy is the cost that must stay proportional to the window (DEC-113, 7.2).
    size_t chunksCopied = 0;
    /// Every chunk whose span was read this call, including chunks skipped as outside the window. A sparse
    /// window can inspect the whole pass while appending nothing; this is the count the display producer's
    /// per-step cap bounds (DEC-237 Slice 2).
    size_t chunksInspected = 0;
  };

  /// Full committed traversal — appends every event from the listed chunk id lists.
  static CommittedEventRange full(const CommittedChunkIdList* const* lists, size_t listCount,
                                  uint32_t loopLengthTicks);

  /// Windowed committed traversal — skips chunks that do not intersect the window, then
  /// filters events to the half-open loop window [windowStart, windowStart + windowLength).
  static CommittedEventRange inWindow(const CommittedChunkIdList* const* lists, size_t listCount,
                                      uint32_t loopLengthTicks, uint32_t windowStart,
                                      uint32_t windowLength);

  void appendTo(SessionMidiEventVec& out) const;
  void appendTo(MidiEventVec& out) const;

  /// Budgeted window traversal primitive. Appends up to `maxEvents` from the current cursor.
  /// The cursor can be reused to resume in the next maintenance slice. `maxChunks` bounds every chunk
  /// whose span is read (admitted or skipped); `0` keeps the historical unlimited behavior. At least one
  /// chunk is inspected per call so a capped caller always advances.
  static WindowTraversalResult appendWindowBudgeted(
      const CommittedChunkIdList* const* lists, size_t listCount, uint32_t loopLengthTicks,
      uint32_t windowStart, uint32_t windowLength, size_t maxEvents,
      WindowTraversalCursor& cursor, SessionMidiEventVec& out, size_t maxChunks = 0);

  static void resetWindowTraversalCursor(WindowTraversalCursor& cursor);

  /// True when chunk [firstTick, lastTick] intersects the half-open window (wrap-aware).
  static bool chunkIntersectsWindow(uint32_t firstTick, uint32_t lastTick, uint32_t windowStart,
                                    uint32_t windowLength, uint32_t loopLengthTicks);

 private:
  CommittedEventRange(const CommittedChunkIdList* const* lists, size_t listCount,
                      uint32_t loopLengthTicks, bool windowed, uint32_t windowStart,
                      uint32_t windowLength);

  const CommittedChunkIdList* const* lists_ = nullptr;
  size_t listCount_ = 0;
  uint32_t loopLengthTicks_ = 0;
  bool windowed_ = false;
  uint32_t windowStart_ = 0;
  uint32_t windowLength_ = 0;
};
