// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

#include "RuntimePlaybackOrderBuilder.h"
#include "RuntimeCompanionBridgeMap.h"
#include "Utils/PlaybackCursorAdvance.h"

/// Copies a frozen, semantically prepared capture and its prepared companion
/// overlay into an unpublished bridge. It does not publish canonical content.
/// The caller freezes the companion rows for the duration of this fill; each
/// step checks capture identity and consumes at most its input entry quantum.
class RuntimeCaptureBridgeFill {
 public:
  bool begin(const CaptureAdmissionStaging& capture, uint32_t frozenGeneration,
             const PlaybackBridgeRecord* companionRecords, size_t companionCount,
             FixedPlaybackBridgeStorage& candidate);
  RuntimePlaybackOrderBuildResult step(size_t entryQuantum);
  void discard();

 private:
  const CaptureAdmissionStaging* capture_ = nullptr;
  const PlaybackBridgeRecord* companions_ = nullptr;
  FixedPlaybackBridgeStorage* candidate_ = nullptr;
  PlaybackViewSourceIdentity target_;
  uint32_t generation_ = 0;
  size_t captureCount_ = 0;
  size_t companionCount_ = 0;
  size_t cursor_ = 0;
  size_t outputCount_ = 0;
  bool complete_ = false;
};

/// Compiles an unpublished overlay into two ordered streams. Each step visits
/// at most entryQuantum input/output entries. The base view stays immutable.
/// A change to an already-open note retains its original Off across wrap,
/// then admits each changed pair at its first safe On occurrence.
class RuntimePlaybackBridgeBuild {
 public:
  /// Ranked compilation: at most recordCount insertions, two bounded sorts,
  /// and linear clear/record/closure/output walks. Fixed ledger scans remain
  /// part of each entry's timing cost, not additional scheduling grains.
  static uint64_t requiredRankedGrainsUpperBound(size_t baseCount, size_t recordCount, size_t quantum);
  /// Optional mapping is copied in bounded grains and must stay immutable until
  /// completion. Ranked mapping requires matching canonical coverage and an
  /// empty bridge; partial coverage currently requires an empty ledger.
  bool begin(const RuntimePlaybackView& base, FixedPlaybackBridgeStorage& bridge,
             const RuntimeCompanionBridgeMap* mapping = nullptr);
  // Continue the same canonical source at a contiguous coverage boundary.
  // Caller has drained the predecessor window. No second source edit is admitted
  // through this entry point. The predecessor stays immutable until completion.
  bool beginSuccessor(const RuntimePlaybackView& base, FixedPlaybackBridgeStorage& bridge,
                      const FixedPlaybackBridgeStorage& predecessor);
  static uint64_t requiredSuccessorGrainsUpperBound(size_t baseCount, size_t ledgerCount, size_t quantum);
  RuntimePlaybackOrderBuildResult step(size_t entryQuantum);
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Inherit, Clear, Records, CarryOffs, Closures, BaseOrder, Order, RankBaseOrder, Complete, Denied };
  const RuntimePlaybackView* base_ = nullptr;
  FixedPlaybackBridgeStorage* bridge_ = nullptr;
  RuntimePlaybackOrderBuilder order_;
  const RuntimeCompanionBridgeMap* mapping_ = nullptr;
  uint64_t mappingGeneration_ = 0;
  const FixedPlaybackBridgeStorage* predecessor_ = nullptr;
  uint64_t predecessorGeneration_ = 0;
  uint64_t bridgeStorageGeneration_ = 0;
  size_t mappedRecordsCopied_ = 0;
  bool ranked_ = false;
  Phase phase_ = Phase::Idle;
  size_t cursor_ = 0;
  size_t recordCount_ = 0;
  size_t insertionCursor_ = 0;
  PlaybackViewSourceIdentity source_;
  std::array<uint16_t, ActiveNoteLedger::kMaxOpenNotes> closureIndex_{};
  bool insertionMatches(size_t index, size_t openIndex) const;
  uint8_t effectiveMask(const MidiEvent& event, size_t openIndex) const;
  bool touchesOpenNote(const MidiEvent& event, bool mark = false);
};

/// Read-only two-way merge over a compiled immutable base/bridge pair. A peek
/// or advance does constant work, including runs of suppressed base events:
/// those runs were removed from the index by the bounded preparation owner.
/// selectAfterPhase is for completed tick boundaries, never mid-phase handoff.
class RuntimePlaybackBridgeCursor {
 public:
  bool selectAfterPhase(const RuntimePlaybackView& base, const FixedPlaybackBridgeStorage& bridge,
                        uint32_t cycle, uint32_t completedPhase = UINT32_MAX);
  const MidiEvent* peek() const;
  bool advance();
  const EmissionCursor& emissionCursor() const { return emitted_; }
  bool matchesBaseStorage(const RuntimePlaybackView& base) const {
    return base_ == &base && baseStorageGeneration_ == base.storageGeneration;
  }
  bool matchesBridgeStorage(const FixedPlaybackBridgeStorage& bridge) const {
    return bridge_ == &bridge && bridgeStorageGeneration_ == bridge.storageGeneration;
  }
  size_t remaining() const {
    return bridge_ == nullptr ? 0 : baseCount_ - baseCursor_ + insertionCount_ - insertionCursor_;
  }

 private:
  const RuntimePlaybackView* base_ = nullptr;
  const FixedPlaybackBridgeStorage* bridge_ = nullptr;
  uint64_t baseStorageGeneration_ = 0;
  uint64_t bridgeStorageGeneration_ = 0;
  size_t baseCursor_ = 0;
  size_t insertionCursor_ = 0;
  const uint16_t* baseOrder_ = nullptr;
  const uint16_t* insertionOrder_ = nullptr;
  size_t baseCount_ = 0;
  size_t insertionCount_ = 0;
  EmissionCursor emitted_;
  bool hasEmitted_ = false;
  bool insertionFirst() const;
};

/// Adapter to the existing MIDI emit/dedup/ledger policy. It retains the two
/// physical cursor positions as the policy consumes one logical stream index.
struct RuntimePlaybackBridgeStream {
  RuntimePlaybackBridgeCursor cursor;
  uint16_t position = 0;
  uint16_t playbackIndex = 0;
  size_t count = 0;

  bool selectAfterPhase(const RuntimePlaybackView& base, const FixedPlaybackBridgeStorage& bridge,
                        uint32_t cycle, uint32_t completedPhase = UINT32_MAX);
  PlaybackEventStream stream();
  bool matches(const RuntimePlaybackView& base, const FixedPlaybackBridgeStorage& bridge,
                uint32_t cycle) const;
  void reset();

 private:
  const RuntimePlaybackView* base_ = nullptr;
  const FixedPlaybackBridgeStorage* bridge_ = nullptr;
  PlaybackViewSourceIdentity baseSource_;
  PlaybackViewSourceIdentity bridgeSource_;
  uint32_t cycle_ = 0;
};
