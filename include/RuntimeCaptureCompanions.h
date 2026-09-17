// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

#include "RuntimePlaybackOrderBuilder.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/PendingNoteCompanion.h"

/// Unpublished canonical companion payloads. Storage is allocated only by
/// prewarm, before active transport. Preparing rows does not assign canonical
/// pass IDs, modify Loop passes, or clear the pending source changes.
class RuntimeCaptureCompanions {
 public:
  RuntimeCaptureCompanions();
  RuntimeCaptureCompanions(const RuntimeCaptureCompanions&) = delete;
  RuntimeCaptureCompanions& operator=(const RuntimeCaptureCompanions&) = delete;
  ~RuntimeCaptureCompanions();
  bool prewarm(size_t capacity);
  /// Grow the row reserve to at least `needed` rows. Cold-point only (the record arm or
  /// the overdub admission): it releases and re-takes the reserve, so it must never run in
  /// a deferred grain. A request no larger than the current capacity is satisfied without
  /// touching the reserve, which is what lets a later, denser interval still fit after a
  /// smaller grant, and lets canonical growth re-grant upward.
  bool ensureCapacity(size_t needed);
  /// Rows a frozen change set can actually produce. A companion row exists only for a
  /// non-Add transform that carries a valid note id; the same vector also carries the
  /// interval's `Add` entries, which become the capture pass and never become rows, so the
  /// whole-vector size is not the row demand.
  static size_t eligibleRowCount(const PendingNoteChangeVec& frozenChanges);
  /// Keep change contents frozen and all borrowed owners alive until this
  /// preparation and its consumers are discarded. Vector identity/count and
  /// frozen capture generation are checked each turn and on readiness queries.
  bool begin(const CaptureAdmissionStaging& capture, uint32_t generation,
             const PendingNoteChangeVec& frozenChanges);
  RuntimePlaybackOrderBuildResult step(size_t inputQuantum);
  void discard();
  bool idle() const { return capture_ == nullptr; }
  bool sourceCurrent() const { return !denied_ && sourceMatches(); }
  bool ready(uint32_t generation) const;
  bool ready(const CaptureAdmissionStaging& capture) const;
  /// No incomplete prefix; row()/size() below are preparation diagnostics.
  const EditPass* data(uint32_t generation) const;
  uint64_t preparationGeneration() const { return preparationGeneration_; }
  size_t size() const { return count_; }
  size_t capacity() const { return capacity_; }
  const EditPass& row(size_t index) const { return rows_[index]; }

 private:
  EditPass* rows_ = nullptr;
  size_t capacity_ = 0;
  const CaptureAdmissionStaging* capture_ = nullptr;
  const PendingNoteChangeVec* changes_ = nullptr;
  const PendingNoteChange* changeData_ = nullptr;
  size_t changeCount_ = 0;
  size_t captureCount_ = 0;
  size_t cursor_ = 0;
  size_t count_ = 0;
  uint32_t generation_ = 0;
  uint64_t preparationGeneration_ = 0;
  bool complete_ = false;
  bool denied_ = false;
  bool sourceMatches() const;
};
