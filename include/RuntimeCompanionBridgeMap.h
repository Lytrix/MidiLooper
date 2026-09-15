// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "RuntimeCompanionApply.h"
#include "RuntimeCompanionPasses.h"

/// Unpublished overlay mapping of a completed companion candidate. Single-stream
/// mode requires the exact immutable apply input; per-pass mode validates an
/// explicit published-base origin table. Neither mode accepts projected windows.
/// Original provenance and final canonical order are separate: consumers MUST
/// use the rank arrays for equal-key order, not infer it from provenance.
/// This is preparation data, not a compiled/selectable playback bridge.
class RuntimeCompanionBridgeMap {
 public:
  RuntimeCompanionBridgeMap() = default;
  ~RuntimeCompanionBridgeMap();
  RuntimeCompanionBridgeMap(const RuntimeCompanionBridgeMap&) = delete;
  RuntimeCompanionBridgeMap& operator=(const RuntimeCompanionBridgeMap&) = delete;
  bool prewarm(size_t capacity);
  static bool inputEventsMatch(const MidiEvent& a, const MidiEvent& b);
  static uint64_t requiredPerPassGrainsUpperBound(size_t baseCount, size_t candidateCount, size_t quantum);
  bool begin(const RuntimeCompanionApply& applied, const MidiEvent* base, size_t baseCount,
             const PlaybackViewSourceIdentity& source);
  /// baseOrigins is immutable published-base provenance into the frozen pass
  /// descriptors. Both it and the descriptors must outlive mapping/compilation.
  /// Missing output origins are capture inserts; no NoteId-based inference.
  bool begin(const RuntimeCompanionPasses& prepared, const MidiEvent* base, size_t baseCount,
             const RuntimeCompanionPasses::Origin* baseOrigins,
             const PlaybackViewSourceIdentity& baseSource,
             const PlaybackViewSourceIdentity& preparedSource);
  /// Preferred production boundary: provenance must have been built with the
  /// published view, rather than reconstructed from event equality.
  bool begin(const RuntimeCompanionPasses& prepared, const RuntimePlaybackView& base,
             const PlaybackViewSourceIdentity& preparedSource);
  RuntimePlaybackOrderBuildResult step(size_t quantum);
  bool ready(const PlaybackViewSourceIdentity& source) const;
  const PlaybackBridgeRecord* records() const;
  const uint16_t* recordRanks() const; // UINT16_MAX for Suppress.
  const uint16_t* baseRanks() const;   // UINT16_MAX for removed base rows.
  size_t size() const;
  size_t lastStepInputCount() const { return lastStepInputs_; }
  void discard();
  uint64_t generation() const { return mapGeneration_; }
  bool matchesBase(const MidiEvent* events, size_t count,
                   const PlaybackViewSourceIdentity& source) const;
 private:
  enum class Phase : uint8_t { Idle, Clear, ValidateBase, SortOrigins, CopyOrigins, BaseDuplicates, Candidate, FindOrigin, Suppress, Complete, Denied };
  PlaybackBridgeRecord* records_ = nullptr;
  uint16_t* recordRanks_ = nullptr;
  uint16_t* baseRanks_ = nullptr;
  uint16_t* baseIndex_ = nullptr;
  size_t sortWidth_ = 1, sortStart_ = 0, sortLeft_ = 0, sortRight_ = 0;
  size_t sortLeftEnd_ = 0, sortRightEnd_ = 0, sortOutput_ = 0, searchEnd_ = 0;
  size_t capacity_ = 0, baseCount_ = 0, candidateCount_ = 0, count_ = 0, cursor_ = 0;
  size_t lastStepInputs_ = 0;
  const RuntimeCompanionApply* applied_ = nullptr;
  const RuntimeCompanionPasses* prepared_ = nullptr;
  const RuntimeCompanionPasses::Origin* baseOrigins_ = nullptr;
  const PlaybackViewSourceIdentity* baseIdentity_ = nullptr;
  PlaybackViewSourceIdentity preparedSource_;
  size_t scan_ = 0;
  const MidiEvent* base_ = nullptr;
  PlaybackViewSourceIdentity source_;
  uint64_t generation_ = 0, mapGeneration_ = 0;
  Phase phase_ = Phase::Idle;
  bool valid() const;
  bool append(PlaybackBridgeOperation op, uint32_t origin, const MidiEvent& event, uint16_t rank);
  RuntimePlaybackOrderBuildResult deny();
};
