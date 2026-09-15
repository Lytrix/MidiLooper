//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "RuntimePlaybackView.h"

enum class RuntimePlaybackOrderBuildResult : uint8_t { Pending, Complete, Denied };

/// Playback order projects ticks into a loop phase. Canonical capture order
/// keeps the original stable tick order so the pass representation remains a
/// faithful event timeline across a wrap.
enum class RuntimePlaybackOrderMode : uint8_t { PlaybackPhase, CanonicalCaptureTick, RawPlaybackTick };

/// Persistent, allocation-free ordering state for an already filled playback
/// view.  One `step` consumes no more than its caller's entry quantum.  The
/// fixed phase and scratch arrays are prewarmed alongside the view.
class RuntimePlaybackOrderBuilder {
 public:
  bool begin(RuntimePlaybackView& view, uint32_t loopLengthTicks,
             RuntimePlaybackOrderMode mode = RuntimePlaybackOrderMode::PlaybackPhase);
  /// Order any prewarmed fixed event storage without giving the builder
  /// ownership of a published RuntimePlaybackView. Deferred capture admission
  /// uses this for its frozen candidate.
  bool begin(FixedPlaybackEventStorage& storage, uint32_t loopLengthTicks,
             const uint32_t* provenance = nullptr,
             RuntimePlaybackOrderMode mode = RuntimePlaybackOrderMode::PlaybackPhase);
  /// Sort borrowed indexes without writing the immutable source or its phases.
  /// Caller supplies two distinct arrays of source.capacity entries, disjoint
  /// from source storage and ranks; source phases must already match the loop.
  bool beginIndexes(const FixedPlaybackEventStorage& source, uint16_t* order, uint16_t* scratch,
                    uint32_t loopLengthTicks, const uint32_t* ranks);
  /// Stable raw-tick ordering for a frozen capture candidate. This shares the
  /// prewarmed arrays with the playback builder but is run before the audible
  /// phase order, never concurrently with it.
  bool beginCanonicalCapture(FixedPlaybackEventStorage& storage);
  /// Private canonical indexes; arrays contain at least indexCapacity entries
  /// and are disjoint from source storage. Does not write source phases/order.
  bool beginCanonicalIndexes(const FixedPlaybackEventStorage& source, uint16_t* order,
                             uint16_t* scratch, size_t indexCapacity);
  RuntimePlaybackOrderBuildResult step(size_t entryQuantum);
  /// Conservative entry-operation count for this bottom-up merge order. It
  /// includes the initial order fill and every merge/copy-back pass. Already
  /// ordered retained inputs finish after fill; this worst-case bound is unchanged.
  static size_t workEntriesUpperBound(size_t eventCount);
  static size_t requiredGrains(size_t eventCount, size_t entryQuantum);
  bool active() const { return phase_ != Phase::Idle && phase_ != Phase::Complete; }
  bool complete() const { return phase_ == Phase::Complete; }
  size_t orderedEventCount() const { return orderedEventCount_; }
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Fill, Merge, CopyBack, Complete, Denied };

  const FixedPlaybackEventStorage* storage_ = nullptr;
  uint16_t* orderEntries_ = nullptr;
  uint16_t* scratchEntries_ = nullptr;
  uint32_t* phaseOutput_ = nullptr;
  const uint32_t* provenance_ = nullptr;
  bool* completionFlag_ = nullptr;
  uint32_t loopLengthTicks_ = 0;
  RuntimePlaybackOrderMode orderMode_ = RuntimePlaybackOrderMode::PlaybackPhase;
  Phase phase_ = Phase::Idle;
  size_t cursor_ = 0;
  size_t runWidth_ = 1;
  size_t runStart_ = 0;
  size_t leftCursor_ = 0;
  size_t rightCursor_ = 0;
  size_t leftEnd_ = 0;
  size_t rightEnd_ = 0;
  size_t outputCursor_ = 0;
  size_t orderedEventCount_ = 0;
  bool orderedDuringFill_ = true;

  bool startNextMergeRun();
  bool isBefore(uint16_t left, uint16_t right) const;
};
