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
enum class RuntimePlaybackOrderMode : uint8_t { PlaybackPhase, CanonicalCaptureTick, RawPlaybackTick, SnapshotCanonical, SnapshotPlayback };

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
  /// Conservative suffix fill/sort plus full prefix-delta merge/copy-back.
  /// Counts filtered suffix rows too. Not a physical-time/service guarantee.
  static size_t extensionWorkEntriesUpperBound(size_t prefixEvents, size_t appendedRows);
  /// Extend a completed raw fixed-storage order after private append only.
  /// Caller certifies unchanged prefix/retention/projection/provenance and
  /// storage lifetime; this is not a source receipt or publication operation.
  /// View-owned and borrowed-index attempts refuse. Capacity <= UINT16_MAX.
  /// Freeze this suffix; appends during ordering wait for the next extension.
  bool extendPrivateOrder();
  /// **Task 3.15e slice 3 — extend without touching the readable prefix.** Identical setup to `extendPrivateOrder`, but
  /// the appended rows are ordered **within the tail only** and concatenated after the existing order; the
  /// `PrefixMerge`/`PrefixCopyBack` pair is skipped. Use this when a reader may be consuming `[0, orderedEventCount())`
  /// concurrently (the live overdub overlay): an already-readable row is never moved, and a tail row that would sort
  /// before the prefix stays in the tail. Caller certifies the prefix, retention, projection and provenance unchanged.
  bool extendPrivateOrderPreservingPrefix();
  /// Frozen source frontier for extendible fixed storage (capacity <= UINT16_MAX).
  size_t orderedSourceExtent() const { return frozenSourceExtent_; }
  RuntimePlaybackOrderBuildResult step(size_t entryQuantum);
  /// Conservative entry-operation count for this bottom-up merge order. It
  /// includes initial fill, one write per merge level, and at most one final
  /// normalization to the caller's order array. Sorted inputs finish after fill.
  static size_t workEntriesUpperBound(size_t eventCount);
  static size_t requiredGrains(size_t eventCount, size_t entryQuantum);
  bool active() const { return phase_ != Phase::Idle && phase_ != Phase::Complete; }
  bool complete() const { return phase_ == Phase::Complete; }
  size_t orderedEventCount() const { return orderedEventCount_; }
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Fill, Merge, CopyBack, Complete, Denied,
                               DeltaFill, DeltaMerge, DeltaCopyBack,
                               // Slice 3: the same delta lifecycle, but completion concatenates the sorted tail after
                               // the frozen prefix instead of running PrefixMerge/CopyBack. Encoded as phases (no new
                               // field) because this object lives inside the pinned `LoopPlaybackRuntime`.
                               FrozenDeltaFill, FrozenDeltaMerge, FrozenDeltaCopyBack,
                               PrefixMerge, PrefixCopyBack };

  const FixedPlaybackEventStorage* storage_ = nullptr;
  uint16_t* orderEntries_ = nullptr;
  uint16_t* scratchEntries_ = nullptr;
  uint32_t* phaseOutput_ = nullptr;
  union {
    const uint32_t* provenance_ = nullptr;
    const PlaybackPassEventOrigin* snapshotEventIds_;
    const uint16_t* snapshotRanks_;
  };
  bool* completionFlag_ = nullptr;
  uint32_t loopLengthTicks_ = 0;
  RuntimePlaybackOrderMode orderMode_ = RuntimePlaybackOrderMode::PlaybackPhase;
  Phase phase_ = Phase::Idle;
  uint16_t prefixEventCount_ = 0;
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
  bool mergeReadsScratch_ = false;
  uint16_t frozenSourceExtent_ = 0;

  bool startNextMergeRun();
  bool isBefore(uint16_t left, uint16_t right) const;
};
