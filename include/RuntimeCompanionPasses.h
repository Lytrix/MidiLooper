// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <optional>
#include "RuntimeCompanionApply.h"

class RuntimeCanonicalPassInputs;
class RuntimeCaptureCompanions;

/// DEC-060: caller freezes active passes in canonical merge order, including
/// capture, and keeps their descriptors/events and rows immutable until done.
/// This owner applies the same rows independently per pass, then performs the
/// reference pairwise stable raw-tick merge without repairing within-pass order.
class RuntimeCompanionPasses {
 public:
  struct Pass { const MidiEvent* events; size_t count; };
  using Origin = PlaybackPassEventOrigin; // UINT16_MAX event = synthesized Off.
  RuntimeCompanionPasses() = default;
  ~RuntimeCompanionPasses();
  RuntimeCompanionPasses(const RuntimeCompanionPasses&) = delete;
  RuntimeCompanionPasses& operator=(const RuntimeCompanionPasses&) = delete;
  bool prewarm(size_t capacity);
  static uint64_t requiredGrainsUpperBound(size_t capacity, size_t passCount, size_t historyCount,
                                          size_t rowCount, size_t quantum);
  bool begin(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
             const Pass* passes, size_t passCount, const EditPass* rows, size_t rowCount,
             const PlaybackViewSourceIdentity& source, uint32_t loopLength);
  /// Typed normalized inputs stay generation-bound throughout preparation and
  /// subsequent origin lookup. Their owner must outlive this preparation.
  bool begin(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
             const RuntimeCanonicalPassInputs& inputs, const EditPass* rows, size_t rowCount,
             const PlaybackViewSourceIdentity& source, uint32_t loopLength);
  /// Both owners must outlive discard. Requires the same frozen capture;
  /// row preparation reuse invalidates even if its capture generation repeats.
  bool begin(RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
             const RuntimeCanonicalPassInputs& inputs, const RuntimeCaptureCompanions& rows,
             const PlaybackViewSourceIdentity& source, uint32_t loopLength);
  RuntimePlaybackOrderBuildResult step(size_t quantum);
  bool idle() const { return phase_ == Phase::Idle; }
  bool ready(const PlaybackViewSourceIdentity& source) const;
  bool matchesOwners(const RuntimeCanonicalPassInputs& inputs, const RuntimeCaptureCompanions& rows) const;
  const MidiEvent* data() const;
  const Origin* origins() const;
  size_t size() const;
  // A completed result may be adopted only by its original canonical owner
  // after the matching source transaction committed. No stale data() access.
  bool normalizationMatches(const RuntimeCanonicalPassInputs& inputs,
                            const PlaybackViewSourceIdentity& committed) const;
  size_t normalizedEventCount() const { return count_; }
  size_t normalizedPassCount() const { return passCount_; }
  void discard();
  uint64_t generation() const { return generation_; }
  const MidiEvent* inputEvent(Origin origin) const;
  bool uneditedBaseline(const PlaybackViewSourceIdentity& source) const { return ready(source) && rowCount_ == 0; }
 private:
  enum class Phase : uint8_t { Idle, Start, Apply, Merge, Complete, Denied };
  MidiEvent* events_[2] = {nullptr, nullptr};
  Origin* origins_[2] = {nullptr, nullptr};
  size_t capacity_ = 0, count_ = 0, output_ = 0, left_ = 0, right_ = 0;
  uint8_t current_ = 0;
  const Pass* passes_ = nullptr;
  const RuntimeCanonicalPassInputs* inputOwner_ = nullptr;
  uint64_t inputGeneration_ = 0;
  const RuntimeCaptureCompanions* rowOwner_ = nullptr;
  uint64_t rowGeneration_ = 0;
  size_t passCount_ = 0, pass_ = 0;
  const EditPass* rows_ = nullptr;
  size_t rowCount_ = 0;
  const PlaybackViewSourceIdentity* source_ = nullptr;
  PlaybackViewSourceIdentity expected_;
  uint32_t loopLength_ = 0;
  RuntimeCompanionApply* apply_ = nullptr;
  std::optional<RuntimeCompanionApply::Work> work_;
  uint64_t applyGeneration_ = 0;
  bool borrowed_ = false;
  Phase phase_ = Phase::Idle;
  uint64_t generation_ = 0;
  bool valid() const;
  void releaseApply();
  RuntimePlaybackOrderBuildResult deny();
};
