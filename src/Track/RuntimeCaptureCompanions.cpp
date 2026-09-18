// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCaptureCompanions.h"
#include "Utils/NoteEditMem.h"

#include <cstdlib>
#include <new>

// Placement alias: the subsystem's cold code already shares `NOTE_EDIT_MEM` (see the sibling
// companion TUs), so this name documents the local rationale without redefining placement.
#define CAPTURE_COMPANION_MEM NOTE_EDIT_MEM

CAPTURE_COMPANION_MEM RuntimeCaptureCompanions::RuntimeCaptureCompanions() = default;

CAPTURE_COMPANION_MEM RuntimeCaptureCompanions::~RuntimeCaptureCompanions() {
  if (rows_ == nullptr) return;
  for (size_t i = 0; i < capacity_; ++i) rows_[i].~EditPass();
  externalFirstFree(rows_);
}

CAPTURE_COMPANION_MEM bool RuntimeCaptureCompanions::prewarm(size_t capacity) {
  if (rows_ != nullptr) return capacity_ == capacity;
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kRecordsPerBridge) return false;
  void* memory = externalFirstAlloc(capacity * sizeof(EditPass));
  if (memory == nullptr) return false;
  rows_ = static_cast<EditPass*>(memory);
  capacity_ = capacity;
  for (size_t i = 0; i < capacity_; ++i) new (&rows_[i]) EditPass();
  return true;
}

CAPTURE_COMPANION_MEM size_t RuntimeCaptureCompanions::eligibleRowCount(
    const PendingNoteChangeVec& frozenChanges) {
  size_t eligible = 0;
  for (const PendingNoteChange& change : frozenChanges) {
    if (change.kind == PendingNoteChangeKind::Add) continue;
    if (change.noteId == kInvalidNoteId) continue;
    ++eligible;
  }
  return eligible;
}

CAPTURE_COMPANION_MEM bool RuntimeCaptureCompanions::ensureCapacity(size_t needed) {
  if (needed <= capacity_) return true;
  if (needed > RuntimePlaybackViewConfig::kRecordsPerBridge) return false;
  if (rows_ != nullptr) {
    for (size_t i = 0; i < capacity_; ++i) rows_[i].~EditPass();
    externalFirstFree(rows_);
    rows_ = nullptr;
    capacity_ = 0;
  }
  return prewarm(needed);
}

CAPTURE_COMPANION_MEM bool RuntimeCaptureCompanions::begin(
    const CaptureAdmissionStaging& capture, uint32_t generation, const PendingNoteChangeVec& frozenChanges) {
  if (capture_ != nullptr || rows_ == nullptr || !capture.frozen || capture.captureGeneration != generation ||
      capture.frozenGeneration != generation || capture.openReservationCount() != 0 ||
      // The reserve is a row reserve: bound the rows this set will produce, not the whole
      // vector (its Add entries become the capture pass, never companion rows).
      eligibleRowCount(frozenChanges) > capacity_) return false;
  ++preparationGeneration_;
  capture_ = &capture;
  changes_ = &frozenChanges;
  changeData_ = frozenChanges.data();
  changeCount_ = frozenChanges.size();
  captureCount_ = capture.storage.eventCount;
  generation_ = generation;
  cursor_ = count_ = 0;
  complete_ = denied_ = false;
  return true;
}

CAPTURE_COMPANION_MEM bool RuntimeCaptureCompanions::sourceMatches() const {
  return capture_ != nullptr && changes_ != nullptr && capture_->frozen &&
         capture_->captureGeneration == generation_ && capture_->frozenGeneration == generation_ &&
         capture_->storage.eventCount == captureCount_ && changes_->size() == changeCount_ &&
         changes_->data() == changeData_;
}

CAPTURE_COMPANION_MEM RuntimePlaybackOrderBuildResult RuntimeCaptureCompanions::step(size_t quantum) {
  if (quantum == 0) return RuntimePlaybackOrderBuildResult::Denied;
  if (denied_ || !sourceMatches()) {
    denied_ = true;
    complete_ = false;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  while (quantum != 0 && cursor_ < changeCount_) {
    const auto& change = (*changes_)[cursor_++];
    --quantum; // Add rows consume input quantum even though they produce no companion.
    if (change.kind == PendingNoteChangeKind::Add) continue;
    if (change.noteId == kInvalidNoteId || !preparePendingNoteCompanion(change, rows_[count_])) {
      denied_ = true;
      return RuntimePlaybackOrderBuildResult::Denied;
    }
    ++count_;
  }
  complete_ = cursor_ == changeCount_;
  return complete_ ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}

CAPTURE_COMPANION_MEM bool RuntimeCaptureCompanions::ready(uint32_t generation) const {
  return complete_ && !denied_ && generation == generation_ && sourceMatches();
}

CAPTURE_COMPANION_MEM void RuntimeCaptureCompanions::discard() {
  ++preparationGeneration_;
  capture_ = nullptr;
  changes_ = nullptr;
  changeData_ = nullptr;
  changeCount_ = captureCount_ = cursor_ = count_ = 0;
  generation_ = 0;
  complete_ = denied_ = false;
}

CAPTURE_COMPANION_MEM bool RuntimeCaptureCompanions::ready(const CaptureAdmissionStaging& capture) const {
  return capture_ == &capture && ready(capture.frozenGeneration);
}

CAPTURE_COMPANION_MEM const EditPass* RuntimeCaptureCompanions::data(uint32_t generation) const {
  return ready(generation) ? rows_ : nullptr;
}
