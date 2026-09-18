//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "RuntimePlaybackView.h"
#include "RuntimeCompanionPasses.h"

enum class RuntimePlaybackViewFillCoverage : uint8_t { AllSource, PlaybackCoverage, RawCoverage };

enum class RuntimePlaybackViewFillResult : uint8_t { Pending, Complete, Denied };

struct RuntimePlaybackViewSource {
  using SizeReader = size_t (*)(const void*);
  using EventReader = const MidiEvent& (*)(const void*, size_t);
  using GenerationReader = uint64_t (*)(const void*);
  const void* context = nullptr;
  SizeReader size = nullptr;
  EventReader eventAt = nullptr;
  GenerationReader generation = nullptr;
  size_t count = 0;
  uint64_t generationToken = 0;
};

/// Copies an already immutable source into a prewarmed view in fixed entry
/// quanta.  The caller owns source lifetime until completion.
/// Coverage-scoped fills charge every examined source row to the quantum,
/// even when outside the window. Source extent need not fit the destination;
/// matching rows must fit or the unpublished build is denied.
/// This has no Loop dependency: task 2.3 supplies immutable capture/source staging.
class RuntimePlaybackViewFill {
 public:
  bool begin(RuntimePlaybackView& destination, const MidiEvent* source, size_t sourceCount,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillCoverage scope = RuntimePlaybackViewFillCoverage::AllSource);
  bool begin(RuntimePlaybackView& destination, const RuntimePlaybackViewSource& source,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillCoverage scope = RuntimePlaybackViewFillCoverage::AllSource);
  /// Construct events and provenance together from already-effective immutable
  /// pass inputs merged without new edits. Source descriptors remain stable until
  /// copy completes; the resulting view owns the origin table independently.
  /// PlaybackCoverage filters with the same charged source visits as other
  /// window fills. It does not require the whole source to fit the window.
  bool begin(RuntimePlaybackView& destination, const RuntimeCompanionPasses& baseline,
             const PlaybackViewSourceIdentity& sourceIdentity,
             const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
             const LedgerBoundarySummary& ledgerBoundary,
             RuntimePlaybackViewFillCoverage scope = RuntimePlaybackViewFillCoverage::AllSource);
  RuntimePlaybackViewFillResult step(size_t entryQuantum);
  void discard();

 private:
  RuntimePlaybackView* destination_ = nullptr;
  const RuntimeCompanionPasses* baseline_ = nullptr;
  uint64_t baselineGeneration_ = 0;
  PlaybackViewSourceIdentity expected_;
  const MidiEvent* source_ = nullptr;
  RuntimePlaybackViewSource::SizeReader sourceSize_ = nullptr;
  RuntimePlaybackViewSource::EventReader sourceEventAt_ = nullptr;
  RuntimePlaybackViewSource::GenerationReader sourceGeneration_ = nullptr;
  const void* sourceContext_ = nullptr;
  uint64_t sourceGenerationToken_ = 0;
  size_t sourceCount_ = 0;
  size_t cursor_ = 0;
  size_t copied_ = 0;
  RuntimePlaybackViewFillCoverage scope_ = RuntimePlaybackViewFillCoverage::AllSource;
};
