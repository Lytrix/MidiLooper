// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "RuntimeCompanionSpanWalk.h"

/// Complete unpublished pitch reconstruction for Length overlap analysis.
/// Borrows source identity, source events/retention, span walk, and wrap lookup
/// exclusively until completion/discard. The owner keeps input contents immutable.
class RuntimeCompanionReconstruction {
 public:
  RuntimeCompanionReconstruction();
  ~RuntimeCompanionReconstruction();
  RuntimeCompanionReconstruction(const RuntimeCompanionReconstruction&) = delete;
  RuntimeCompanionReconstruction& operator=(const RuntimeCompanionReconstruction&) = delete;
  bool available() const { return phase_ == Phase::Idle || phase_ == Phase::Complete || phase_ == Phase::Denied; }
  bool prewarm(size_t noteCapacity);
  bool begin(RuntimeCompanionSpanWalk& spans, RuntimeNotePairLookup& wrap,
             const MidiEvent* events, size_t count, uint8_t pitch, uint32_t loopLength,
             const PlaybackViewSourceIdentity& source, const uint8_t* retained = nullptr);
  RuntimePlaybackOrderBuildResult step(size_t inputQuantum);
  bool ready(const PlaybackViewSourceIdentity& source) const;
  const NoteUtils::DisplayNote* data() const;
  size_t size() const;
  size_t lastStepInputCount() const { return visited_; }
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Spans, Project, Dedupe, Complete, Denied };
  NoteUtils::DisplayNote* notes_ = nullptr;
  size_t capacity_ = 0, count_ = 0, compare_ = 0, visited_ = 0;
  RuntimeCompanionSpanWalk* spans_ = nullptr;
  const PlaybackViewSourceIdentity* source_ = nullptr;
  PlaybackViewSourceIdentity expected_;
  uint32_t loopLength_ = 0;
  NoteUtils::DisplayNote pending_[2]{};
  uint8_t pendingCount_ = 0, pendingIndex_ = 0;
  Phase phase_ = Phase::Idle;
  bool sourceMatches() const;
  void projectSpan();
};
