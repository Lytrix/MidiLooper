// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Loop.h"
#include "Utils/LoopMem.h"

namespace {
template <typename CapturePass>
bool snapshotInventoryCaptureEligible(const CapturePass& pass) {
  return pass.id != kInvalidPassId && pass.state == CapturePassState::Active &&
      !pass.committedChunkIds.empty();
}
bool snapshotInventoryEditEligible(const EditPass& pass) {
  return pass.id != kInvalidEditPassId && pass.state == EditPassState::Active;
}
}  // namespace

LOOP_COLD_MEM void Loop::beginCommittedSnapshotInventoryPreparation(
    CommittedSnapshotInventoryPreparation& cursor,
    std::vector<PassId>& captures, EditPassIdList& companions) const {
  cursor.discard();
  if (!captures.empty() || !companions.empty()) {
    cursor.result_ = CommittedSnapshotInventoryPreparationResult::Fault;
    return;
  }
  cursor.loop_ = this;
  cursor.loopId_ = loopId;
  cursor.canonicalGeneration_ = canonicalStorageGeneration;
  cursor.revision_ = committedPassRevision();
  cursor.loopLength_ = loopLengthTicks;
  cursor.recordId_ = passes.recordPass.id;
  cursor.overdubCount_ = passes.overdubPasses.size();
  cursor.editCount_ = passes.editPasses.size();
  cursor.captures_ = &captures;
  cursor.companions_ = &companions;
  cursor.captureCapacity_ = captures.capacity();
  cursor.companionCapacity_ = companions.capacity();
  cursor.captureData_ = captures.data();
  cursor.companionData_ = companions.data();
  cursor.result_ = CommittedSnapshotInventoryPreparationResult::Progress;
}

LOOP_COLD_MEM bool Loop::committedSnapshotInventoryPreparationMatches(
    const CommittedSnapshotInventoryPreparation& cursor,
    const std::vector<PassId>& captures, const EditPassIdList& companions) const {
  return cursor.loop_ == this && cursor.loopId_ == loopId &&
      cursor.canonicalGeneration_ == canonicalStorageGeneration &&
      cursor.revision_ == committedPassRevision() && cursor.loopLength_ == loopLengthTicks &&
      cursor.recordId_ == passes.recordPass.id &&
      cursor.overdubCount_ == passes.overdubPasses.size() &&
      cursor.editCount_ == passes.editPasses.size() &&
      cursor.captures_ == &captures && cursor.companions_ == &companions &&
      cursor.captureSize_ == captures.size() && cursor.companionSize_ == companions.size() &&
      cursor.captureCapacity_ == captures.capacity() &&
      cursor.companionCapacity_ == companions.capacity() &&
      cursor.captureData_ == captures.data() && cursor.companionData_ == companions.data();
}

LOOP_COLD_MEM Loop::CommittedSnapshotInventoryPreparationResult
Loop::committedSnapshotInventoryPreparationResult(
    const CommittedSnapshotInventoryPreparation& cursor,
    const std::vector<PassId>& captures, const EditPassIdList& companions) const {
  using Result = CommittedSnapshotInventoryPreparationResult;
  if (cursor.result_ == Result::Idle || cursor.result_ == Result::Fault ||
      cursor.result_ == Result::Superseded) return cursor.result_;
  return committedSnapshotInventoryPreparationMatches(cursor, captures, companions)
      ? cursor.result_ : Result::Superseded;
}

LOOP_COLD_MEM Loop::CommittedSnapshotInventoryPreparationResult
Loop::stepCommittedSnapshotInventoryPreparation(
    CommittedSnapshotInventoryPreparation& cursor,
    std::vector<PassId>& captures, EditPassIdList& companions) const {
  using Result = CommittedSnapshotInventoryPreparationResult;
  using Phase = CommittedSnapshotInventoryPreparation::Phase;
  cursor.result_ = committedSnapshotInventoryPreparationResult(cursor, captures, companions);
  if (cursor.result_ != Result::Progress) return cursor.result_;

  // Count before reserving: reserves visit/copy zero existing output elements.
  // Each phase end consumes its own grant; no scan to find the next eligible ID.
  switch (cursor.phase_) {
    case Phase::CountRecord:
      if (snapshotInventoryCaptureEligible(passes.recordPass)) ++cursor.captureCount_;
      ++cursor.inspectedRows_;
      cursor.phase_ = Phase::CountOverdub;
      break;
    case Phase::CountOverdub:
      if (cursor.row_ == cursor.overdubCount_) {
        cursor.row_ = 0;
        cursor.phase_ = Phase::CountEdit;
        break;
      }
      if (snapshotInventoryCaptureEligible(passes.overdubPasses[cursor.row_]))
        ++cursor.captureCount_;
      ++cursor.row_;
      ++cursor.inspectedRows_;
      break;
    case Phase::CountEdit:
      if (cursor.row_ == cursor.editCount_) {
        cursor.row_ = 0;
        cursor.phase_ = Phase::ReserveCapture;
        break;
      }
      if (snapshotInventoryEditEligible(passes.editPasses[cursor.row_]))
        ++cursor.companionCount_;
      ++cursor.row_;
      ++cursor.inspectedRows_;
      break;
    case Phase::ReserveCapture:
      captures.reserve(cursor.captureCount_);
      cursor.captureCapacity_ = captures.capacity();
      cursor.captureData_ = captures.data();
      cursor.phase_ = Phase::ReserveCompanion;
      break;
    case Phase::ReserveCompanion:
      companions.reserve(cursor.companionCount_);
      cursor.companionCapacity_ = companions.capacity();
      cursor.companionData_ = companions.data();
      cursor.phase_ = Phase::EmitRecord;
      break;
    case Phase::EmitRecord:
    case Phase::EmitOverdub: {
      if (cursor.phase_ == Phase::EmitOverdub && cursor.row_ == cursor.overdubCount_) {
        cursor.row_ = 0;
        cursor.phase_ = Phase::EmitEdit;
        break;
      }
      const bool record = cursor.phase_ == Phase::EmitRecord;
      const bool eligible = record ? snapshotInventoryCaptureEligible(passes.recordPass)
          : snapshotInventoryCaptureEligible(passes.overdubPasses[cursor.row_]);
      if (eligible) {
        if (captures.size() >= cursor.captureCount_ || captures.size() >= captures.capacity()) {
          cursor.result_ = Result::Fault;
          break;
        }
        captures.push_back(record ? passes.recordPass.id : passes.overdubPasses[cursor.row_].id);
        ++cursor.captureSize_;
      }
      ++cursor.inspectedRows_;
      if (record) cursor.phase_ = Phase::EmitOverdub;
      else ++cursor.row_;
      break;
    }
    case Phase::EmitEdit:
      if (cursor.row_ == cursor.editCount_) {
        cursor.result_ = captures.size() == cursor.captureCount_ &&
            companions.size() == cursor.companionCount_ ? Result::Complete : Result::Fault;
        break;
      }
      if (snapshotInventoryEditEligible(passes.editPasses[cursor.row_])) {
        if (companions.size() >= cursor.companionCount_ || companions.size() >= companions.capacity()) {
          cursor.result_ = Result::Fault;
          break;
        }
        companions.push_back(passes.editPasses[cursor.row_].id);
        ++cursor.companionSize_;
      }
      ++cursor.row_;
      ++cursor.inspectedRows_;
      break;
  }
  return cursor.result_;
}
