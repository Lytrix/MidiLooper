//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "RuntimePlaybackView.h"
#include "LoopSnapshot.h"
#include "LoopEventStore.h"
#include "RuntimeCompanionPasses.h"

struct Loop;

enum class RuntimePlaybackViewFillScope : uint8_t { AllSource, PlaybackCoverage, RawCoverage, RecordCoverage, SnapshotWindow };

enum class RuntimePlaybackViewFillResult : uint8_t { Pending, Complete, Denied };

struct RuntimePlaybackViewSource {
  using SizeFn = size_t (*)(const void*);
  using EventAtFn = const MidiEvent& (*)(const void*, size_t);
  using ReadEventAtFn = bool (*)(const void*, size_t, MidiEvent&);
  using GenerationFn = uint64_t (*)(const void*);
  using BlockMayIntersectFn = bool (*)(const void*, uint64_t, size_t,
                                        uint32_t, uint32_t, uint32_t, bool&);
  const void* context = nullptr;
  SizeFn size = nullptr;
  union {
    EventAtFn eventAt = nullptr;
    ReadEventAtFn readEventAt;
  };
  GenerationFn generation = nullptr;
  BlockMayIntersectFn blockMayIntersect = nullptr;
  size_t count = 0;
  uint64_t generationToken = 0;
  bool readsByCopy = false;
  PlaybackWindowOrigin origin = PlaybackWindowOrigin::Committed;
  /// Capture-origin frozen Record source: content token is frozen by the job,
  /// while generationToken continues to protect the sealed range's lifetime.
  uint64_t captureContentToken = 0;
  bool mutableCapture = false;
  /// Borrows the sealed range until fill completion. Never flattens its chunks.
  static RuntimePlaybackViewSource fromCaptureRange(const LoopEventStore::CaptureRange& range);
  /// Borrow the live Loop Capture producer, never a committed root or raw shell.
  static RuntimePlaybackViewSource fromLiveCapture(const Loop& loop);
  static RuntimePlaybackViewSource fromFrozenRecordCapture(
      const LoopEventStore::CaptureRange& range, uint64_t frozenContentToken);
};
/// Copies an immutable source into a prewarmed view in fixed entry quanta.
/// Coverage-scoped fills charge examined rows; a sealed Capture source may
/// additionally charge one unit per 32-row summary and skip only proven
/// irrelevant groups. Invalid summaries always fall back to charged rows.
/// Matching rows must fit the destination or the unpublished build is denied.
/// This has no Loop dependency: task 2.3 supplies immutable capture/source staging.
class RuntimePlaybackViewFill {
 public:
  bool begin(RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillScope scope = RuntimePlaybackViewFillScope::AllSource);
  bool begin(RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillScope scope = RuntimePlaybackViewFillScope::AllSource);
  /// Construct events and provenance together from already-effective immutable
  /// pass inputs merged without new edits. Source descriptors remain stable until
  /// copy completes; the resulting view owns the origin table independently.
  /// PlaybackCoverage filters with the same charged source visits as other
  /// window fills. It does not require the whole source to fit the window.
  bool begin(RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillScope scope = RuntimePlaybackViewFillScope::AllSource);
  /// Borrows a pinned window cursor; each cursor inspection/transition costs one
  /// unit. Caller owns its retirement, including denial/cancellation paths.
  bool begin(RuntimePlaybackView& destination, LoopSnapshot::EventWindowCursor& source,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary);
  RuntimePlaybackViewFillResult step(size_t entryQuantum);
  void discard();
  /// An immutable frozen source may release after copying. A *live* Capture
  /// borrow remains mutation-sensitive through private ordering/publication.
  bool liveCaptureCurrent() const;
  size_t copiedEventCount() const { return copied_; }
  bool active() const { return destination_ != nullptr && cursor_ < sourceCount_; }

 private:
  RuntimePlaybackView* destination_ = nullptr;
  union {
    const RuntimeCompanionPasses* baseline_ = nullptr;
    LoopSnapshot::EventWindowCursor* snapshotSource_;
  };
  uint64_t baselineGeneration_ = 0;
  PlaybackViewSourceIdentity expected_;
  const MidiEvent* source_ = nullptr;
  RuntimePlaybackViewSource::SizeFn sourceSize_ = nullptr;
  union {
    RuntimePlaybackViewSource::EventAtFn sourceEventAt_ = nullptr;
    RuntimePlaybackViewSource::ReadEventAtFn sourceReadEventAt_;
  };
  RuntimePlaybackViewSource::GenerationFn sourceGeneration_ = nullptr;
  const void* sourceContext_ = nullptr;
  uint64_t sourceGenerationToken_ = 0;
  RuntimePlaybackViewSource::BlockMayIntersectFn blockMayIntersect_ = nullptr;
  bool inspectBlockRows_ = false;
  size_t sourceCount_ = 0;
  size_t cursor_ = 0;
  size_t copied_ = 0;
  RuntimePlaybackViewFillScope scope_ = RuntimePlaybackViewFillScope::AllSource;
  bool sourceReadsByCopy_ = false;
  bool guardLiveCaptureAfterFill_ = false;
};
