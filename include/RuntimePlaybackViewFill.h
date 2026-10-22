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

/// **Named** refusal for a build the fill could not complete honestly. A refusal never leaves a partial window behind.
enum class RuntimePlaybackViewFillRefusal : uint8_t {
  None,
  /// Committed base rows plus the append-stable live overlay do not fit the one view capacity. The window is cleared; no
  /// overlay row is truncated and no committed row is evicted to make room.
  CombinedCapacityExceeded,
  /// The live capture generation/projection the overlay was built against no longer exists (a pass commit or a re-based
  /// capture). The next build must re-derive from the now-committed source instead of continuing this one.
  ContinuationStale,
  /// The continuation was asked for with an identity that does not share the base's revision/generation, or with a source
  /// that is not an append-stable live capture.
  ContinuationIdentityMismatch,
};

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
  bool appendStableCapture = false;
  CaptureProjectionIdentity captureProjection{};
  LoopEventStoreSourceCursor captureCursor;
  /// Borrows the sealed range until fill completion. Never flattens its chunks.
  static RuntimePlaybackViewSource fromCaptureRange(const LoopEventStore::CaptureRange& range);
  /// Borrow the live Loop Capture producer, never a committed root or raw shell.
  static RuntimePlaybackViewSource fromLiveCapture(const Loop& loop, bool appendStable = false);
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
  /// Preserve a certified private prefix and freeze the newest raw suffix.
  bool extendLiveCapture(const PlaybackViewSourceIdentity& identity);
  /// **Slice 3.15e — one window, two halves.** Continue a *completed* committed-base build from one append-stable live
  /// overlay source into the **same** destination, its **same** ordered index and its **same** source identity.
  ///
  /// The base keeps its per-row `passOrigins`; each overlay row appended here carries `kLiveOverlayPassOrigin` (with its own
  /// ordinal in `event`) and `kLiveOverlayCanonicalRank`, so one ordered window can be served while only committed-origin
  /// rows ever acquire occupy/closure ledger ownership. Capacity is the destination's own: base rows plus the whole overlay
  /// must fit, otherwise the fill refuses by name and clears the unpublished window — it never truncates overlay rows or
  /// evicts committed ones.
  bool beginLiveContinuation(RuntimePlaybackView& destination, const RuntimePlaybackViewSource& live,
                             const PlaybackViewSourceIdentity& identity);
  bool appendStableCapture() const { return testFlag(kAppendStableCaptureBit); }
  /// True while this fill is appending the live-overlay half of a window (never for the first-take Record lane).
  bool continuingOverlay() const { return testFlag(kOverlayActiveBit); }
  RuntimePlaybackViewFillRefusal refusal() const {
    return static_cast<RuntimePlaybackViewFillRefusal>((flags_ >> kOverlayRefusalShift) & 0x3u);
  }
  /// Content generation of the live overlay this fill is appending (0 when none): the identity slice-2 arbitration needs.
  /// It is the live source's own generation token, so it needs no field of its own.
  uint64_t continuationCaptureGeneration() const { return continuingOverlay() ? sourceGenerationToken_ : 0; }
  /// Local borrow state only: never dereferences its source owner.
  bool ownsLiveCaptureBorrow() const { return ownsLiveGuard() && sourceContext_ != nullptr; }
  size_t copiedEventCount() const { return copied_; }
  size_t frozenSourceExtent() const { return sourceCount_; }
  size_t sourceFrontier() const { return cursor_; }
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
  LoopEventStoreSourceCursor liveCaptureCursor_;
  CaptureProjectionIdentity liveCaptureProjection_{};
  RuntimePlaybackViewSource::BlockMayIntersectFn blockMayIntersect_ = nullptr;
  bool inspectBlockRows_ = false;
  size_t sourceCount_ = 0;
  size_t cursor_ = 0;
  size_t copied_ = 0;
  RuntimePlaybackViewFillScope scope_ = RuntimePlaybackViewFillScope::AllSource;
  /// **One byte for five facts**, so the per-slot runtime size pin (`sizeof(LoopPlaybackRuntime)` == 7 904 target) is
  /// unchanged by this slice: the three append/live-capture flags the fill already carried, plus the live-overlay bit and its
  /// named refusal. Read/write only through these accessors.
  enum FlagBits : uint8_t {
    kSourceReadsByCopyBit = 1u << 0,
    kGuardLiveCaptureBit = 1u << 1,
    kAppendStableCaptureBit = 1u << 2,
    kOverlayActiveBit = 1u << 3,
  };
  static constexpr uint8_t kOverlayRefusalShift = 4;
  uint8_t flags_ = 0;
  bool testFlag(uint8_t bit) const { return (flags_ & bit) != 0; }
  void setFlag(uint8_t bit, bool on) {
    flags_ = on ? uint8_t(flags_ | bit) : uint8_t(flags_ & ~bit);
  }
  bool sourceReadsByCopy() const { return testFlag(kSourceReadsByCopyBit); }
  bool ownsLiveGuard() const { return testFlag(kGuardLiveCaptureBit); }
  void setRefusal(RuntimePlaybackViewFillRefusal reason) {
    flags_ = uint8_t((flags_ & ~(0x3u << kOverlayRefusalShift)) |
                     ((static_cast<uint8_t>(reason) & 0x3u) << kOverlayRefusalShift));
  }
  void refuseOverlay(RuntimePlaybackViewFillRefusal reason);
};
