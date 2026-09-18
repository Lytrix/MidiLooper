// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCanonicalPassInputs.h"
#include "LoopPasses.h"
#include "Utils/NoteEditMem.h"
#include <cstdlib>
#include <new>

namespace {
}
NOTE_EDIT_MEM RuntimeCanonicalPassInputs::RuntimeCanonicalPassInputs() = default;
NOTE_EDIT_MEM RuntimeCanonicalPassInputs::~RuntimeCanonicalPassInputs() {
  // Explicit runtime discard handles live borrowed workers; destruction must
  // not dereference a worker that may already have been destroyed.
  for (size_t i = 0; i < capacity_; ++i) { raw_[i].~MidiEvent(); effective_[i].~MidiEvent(); }
  externalFirstFree(raw_); externalFirstFree(effective_); externalFirstFree(outputs_); externalFirstFree(nextOutputs_);
  externalFirstFree(selectedPasses_); externalFirstFree(selectedHistory_);
  externalFirstFree(captureOrder_); externalFirstFree(captureScratch_);
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::prewarm(size_t capacity) {
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView) return false;
  if (capacity_) return capacity_ == capacity;
  auto* raw = static_cast<MidiEvent*>(externalFirstAlloc(capacity * sizeof(MidiEvent)));
  auto* effective = static_cast<MidiEvent*>(externalFirstAlloc(capacity * sizeof(MidiEvent)));
  auto* outputs = static_cast<RuntimeCompanionPasses::Pass*>(externalFirstAlloc(capacity * sizeof(RuntimeCompanionPasses::Pass)));
  auto* nextOutputs = static_cast<RuntimeCompanionPasses::Pass*>(externalFirstAlloc(capacity * sizeof(RuntimeCompanionPasses::Pass)));
  auto* selected = static_cast<SealedPass*>(externalFirstAlloc(capacity * sizeof(SealedPass)));
  auto* history = static_cast<const EditPass**>(externalFirstAlloc(RuntimePlaybackViewConfig::kRecordsPerBridge * sizeof(const EditPass*)));
  auto* order = static_cast<uint16_t*>(externalFirstAlloc(capacity * sizeof(uint16_t)));
  auto* scratch = static_cast<uint16_t*>(externalFirstAlloc(capacity * sizeof(uint16_t)));
  if (!raw || !effective || !outputs || !nextOutputs || !selected || !history || !order || !scratch) {
    externalFirstFree(raw); externalFirstFree(effective); externalFirstFree(outputs); externalFirstFree(nextOutputs);
    externalFirstFree(selected); externalFirstFree(history);
    externalFirstFree(order); externalFirstFree(scratch); return false;
  }
  for (size_t i = 0; i < capacity; ++i) {
    new (&raw[i]) MidiEvent(); new (&effective[i]) MidiEvent();
    new (&outputs[i]) RuntimeCompanionPasses::Pass{};
    new (&nextOutputs[i]) RuntimeCompanionPasses::Pass{};
    new (&selected[i]) SealedPass{};
  }
  for (size_t i = 0; i < RuntimePlaybackViewConfig::kRecordsPerBridge; ++i) new (&history[i]) const EditPass*(nullptr);
  raw_ = raw; effective_ = effective; outputs_ = outputs; nextOutputs_ = nextOutputs; capacity_ = capacity;
  selectedPasses_ = selected; selectedHistory_ = history;
  captureOrder_ = order; captureScratch_ = scratch;
  return true;
}
NOTE_EDIT_MEM void RuntimeCanonicalPassInputs::releaseApply() {
  if (borrowed_ && apply_ && apply_->generation() == applyGeneration_) apply_->discard();
  borrowed_ = false;
}
NOTE_EDIT_MEM uint64_t RuntimeCanonicalPassInputs::requiredGrainsUpperBound(
    size_t capacity, size_t passes, size_t rows, size_t quantum) {
  const auto history = RuntimeCompanionApply::requiredHistoryGrainsUpperBound(capacity, rows, quantum);
  if (history == UINT64_MAX || passes > capacity) return UINT64_MAX;
  const auto ceilGrains = [quantum](size_t visits) -> uint64_t {
    return visits == 0 ? 0 : 1 + (visits - 1) / quantum;
  };
  return 1 + uint64_t(passes) * ((rows ? history : 0) +
      ceilGrains(capacity + LoopEventStoreConfig::POOL_CHUNK_COUNT) + ceilGrains(capacity) + 3);
}
NOTE_EDIT_MEM void RuntimeCanonicalPassInputs::discard() {
  ++generation_; normalizedOwner_ = nullptr; releaseApply(); read_.discard(); work_.reset(); apply_ = nullptr;
  passes_ = nullptr; history_ = nullptr; source_ = nullptr;
  live_.reset(); current_.reset(); preparedCaptureOrder_.reset(); captureOrderedCount_ = 0; captureOrderBuild_.discard(); includeCapture_ = captureStarted_ = false;
  count_ = passCount_ = pass_ = copy_ = rowCount_ = 0; phase_ = Phase::Idle;
  loopPasses_ = nullptr; selectionCursor_ = 0; bestOverdub_ = SIZE_MAX; haveSequence_ = false;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::begin(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const SealedPass* passes, size_t passCount, const EditPass* history, size_t rowCount,
    const PlaybackViewSourceIdentity& source, uint32_t loopLength) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Denied) ||
      !capacity_ || passCount > capacity_ || (!passes && passCount) ||
      (!history && rowCount) || rowCount > RuntimePlaybackViewConfig::kRecordsPerBridge ||
      !apply.available() || !loopLength) return false;
  discard(); apply_ = &apply; work_.emplace(work); passes_ = passes; passCount_ = passCount;
  history_ = history; rowCount_ = rowCount; source_ = &source; expected_ = source; loopLength_ = loopLength;
  phase_ = passCount ? Phase::Start : Phase::Complete;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::valid(bool checkCommandEpoch) const {
  return source_ && *source_ == expected_ &&
      (!preparedCaptureOrder_ || preparedCaptureOrder_->storageGeneration == preparedCaptureOrderGeneration_) &&
      (!current_ || (current_->playbackRevision == expected_.playbackRevision &&
                     current_->sourceGeneration == expected_.sourceGeneration &&
                     current_->loopLengthTicks == loopLength_ &&
                     current_->storageGeneration == storageGeneration_)) &&
      (!live_ || (live_->playbackRevision == expected_.playbackRevision &&
                  live_->sourceGeneration == expected_.sourceGeneration &&
                  (!checkCommandEpoch || live_->transportCommandEpoch == commandEpoch_) &&
                  live_->loopLengthTicks == loopLength_ &&
                  live_->capture.frozen &&
                  live_->capture.captureGeneration == captureGeneration_ &&
                  live_->capture.frozenGeneration == captureGeneration_ &&
                  live_->capture.storage.eventCount == captureCount_)) &&
      (!borrowed_ || (apply_ && apply_->generation() == applyGeneration_));
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::retargetFrozenCommandEpoch(
    const uint32_t& epoch, const CaptureAdmissionStaging& capture) {
  if (phase_ == Phase::Idle || phase_ == Phase::Denied || !includeCapture_ || !live_ ||
      &live_->transportCommandEpoch != &epoch || &live_->capture != &capture || !valid(false)) return false;
  commandEpoch_ = epoch;
  return true;
}

NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::beginFrozen(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const FrozenSource& live) {
  if (live.playbackRevision != source.playbackRevision ||
      live.sourceGeneration != source.sourceGeneration || !live.capture.frozen ||
      live.capture.captureGeneration != live.capture.frozenGeneration ||
      live.capture.openReservationCount() != 0) return false;
  if (!begin(apply, work, passes, source, live.loopLengthTicks)) return false;
  live_.emplace(live); commandEpoch_ = live.transportCommandEpoch;
  captureGeneration_ = live.capture.captureGeneration;
  captureCount_ = live.capture.storage.eventCount;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::beginCurrent(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source, const CurrentSource& live) {
  if (live.playbackRevision != source.playbackRevision || live.sourceGeneration != source.sourceGeneration) return false;
  if (!begin(apply, work, passes, source, live.loopLengthTicks)) return false;
  current_.emplace(live);
  storageGeneration_ = live.storageGeneration;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::resumeCurrentAfterAppend(
    const LoopPasses& passes, uint32_t priorRevision,
    const PlaybackViewSourceIdentity& source, const CurrentSource& live, bool committedFrozenTail) {
  // A source revision alone never licenses reuse: the caller certifies the
  // append, and unchanged history storage/owners protect normalized output.
  if (phase_ == Phase::Idle || phase_ == Phase::Denied || !current_ ||
      loopPasses_ != &passes || expected_.playbackRevision != priorRevision ||
      !((priorRevision != UINT32_MAX && source.playbackRevision == priorRevision + 1) ||
        (!committedFrozenTail && source.playbackRevision == priorRevision &&
         source.snapshotIdentity != expected_.snapshotIdentity)) ||
      source.sourceGeneration != expected_.sourceGeneration ||
      live.playbackRevision != source.playbackRevision || live.sourceGeneration != source.sourceGeneration ||
      live.loopLengthTicks != loopLength_ || live.storageGeneration != storageGeneration_ ||
      &current_->storageGeneration != &live.storageGeneration ||
      &current_->playbackRevision != &live.playbackRevision ||
      &current_->sourceGeneration != &live.sourceGeneration ||
      &current_->loopLengthTicks != &live.loopLengthTicks ||
      includeCapture_ != committedFrozenTail ||
      (committedFrozenTail && phase_ != Phase::Complete)) return false;
  ++generation_;
  releaseApply(); read_.discard(); captureOrderBuild_.discard();
  live_.reset(); preparedCaptureOrder_.reset();
  includeCapture_ = captureStarted_ = false; captureOrderedCount_ = 0;
  // count_ and outputs_[0..pass_) remain owned and immutable. Partially copied
  // output lies beyond count_ and will be overwritten by the existing worker.
  expected_ = source; source_ = &source; current_.emplace(live);
  passCount_ = rowCount_ = copy_ = selectionCursor_ = 0;
  bestOverdub_ = SIZE_MAX; haveSequence_ = false;
  phase_ = Phase::SelectRecord;
  return true;
}

NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::adoptCommittedNormalization(
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const CurrentSource& live, const RuntimeCompanionPasses& resolved) {
  if (!current_ || loopPasses_ != &passes || !includeCapture_ ||
      !resolved.normalizationMatches(*this, source) ||
      live.playbackRevision != source.playbackRevision ||
      live.sourceGeneration != source.sourceGeneration || live.loopLengthTicks != loopLength_ ||
      &current_->playbackRevision != &live.playbackRevision ||
      &current_->sourceGeneration != &live.sourceGeneration ||
      &current_->loopLengthTicks != &live.loopLengthTicks ||
      &current_->storageGeneration != &live.storageGeneration) return false;
  const size_t normalizedCount = resolved.normalizedEventCount();
  const size_t normalizedPasses = resolved.normalizedPassCount();
  ++generation_; releaseApply(); read_.discard(); captureOrderBuild_.discard();
  std::swap(raw_, effective_); std::swap(outputs_, nextOutputs_);
  count_ = normalizedCount; pass_ = normalizedPasses;
  expected_ = source; source_ = &source; current_.emplace(live);
  storageGeneration_ = live.storageGeneration;
  live_.reset(); preparedCaptureOrder_.reset(); includeCapture_ = captureStarted_ = false;
  captureOrderedCount_ = passCount_ = rowCount_ = copy_ = selectionCursor_ = 0;
  bestOverdub_ = SIZE_MAX; haveSequence_ = false;
  phase_ = Phase::SelectRecord;
  return true;
}

NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::beginFrozenOverdub(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
    const FrozenSource& live) {
  if (!live.capture.storage.isPrewarmed() || live.capture.storage.eventCount > capacity_) return false;
  if (!beginFrozen(apply, work, passes, source, live)) return false;
  includeCapture_ = true; return true;
}
NOTE_EDIT_MEM uint64_t RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(
    size_t capacity, size_t rows, size_t quantum) {
  const auto selected = requiredSelectedGrainsUpperBound(capacity, rows, quantum);
  if (selected == UINT64_MAX) return UINT64_MAX;
  return selected + requiredFrozenTailGrainsUpperBound(capacity, rows, quantum);
}
NOTE_EDIT_MEM uint64_t RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(
    size_t capacity, size_t rows, size_t quantum) {
  const auto history = RuntimeCompanionApply::requiredHistoryGrainsUpperBound(capacity, rows, quantum);
  if (history == UINT64_MAX) return UINT64_MAX;
  return RuntimePlaybackOrderBuilder::requiredGrains(capacity, quantum) +
      2 * (1 + (capacity - 1) / quantum) +
      (rows ? history : 0) + 4;
}
NOTE_EDIT_MEM uint64_t RuntimeCanonicalPassInputs::requiredPreparedFrozenTailGrainsUpperBound(
    size_t capacity, size_t rows, size_t quantum) {
  const auto history = RuntimeCompanionApply::requiredHistoryGrainsUpperBound(capacity, rows, quantum);
  if (history == UINT64_MAX) return UINT64_MAX;
  return 3 * (1 + (capacity - 1) / quantum) + (rows ? history : 0) + 4;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::appendFrozenOverdub(
    const FrozenSource& live, const PreparedCaptureOrder* preparedOrder) {
  if (!ready() || includeCapture_ || !apply_ || !apply_->available() ||
      live.playbackRevision != expected_.playbackRevision ||
      live.sourceGeneration != expected_.sourceGeneration || live.loopLengthTicks != loopLength_ ||
      !live.capture.frozen || live.capture.captureGeneration != live.capture.frozenGeneration ||
      live.capture.openReservationCount() != 0 || !live.capture.storage.isPrewarmed() ||
      live.capture.storage.eventCount > capacity_ ||
      (preparedOrder && (preparedOrder->count > live.capture.storage.eventCount ||
                         (preparedOrder->count && !preparedOrder->indexes)))) return false;
  // No consumer may continue to use descriptors while the tail is appended.
  ++generation_;
  live_.emplace(live); commandEpoch_ = live.transportCommandEpoch;
  captureGeneration_ = live.capture.captureGeneration; captureCount_ = live.capture.storage.eventCount;
  includeCapture_ = true; captureStarted_ = false; captureOrderBuild_.discard();
  if (preparedOrder) {
    preparedCaptureOrder_.emplace(*preparedOrder);
    preparedCaptureOrderGeneration_ = preparedOrder->storageGeneration;
    captureOrderedCount_ = preparedOrder->count;
    captureStarted_ = true; copy_ = 0; phase_ = Phase::CopyCaptureOrder;
  } else phase_ = Phase::CaptureOrder;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::beginApply(size_t count) {
  if (rowCount_ == 0) {
    if (count > capacity_ - count_) return false;
    copy_ = 0; outputs_[pass_] = {effective_ + count_, count}; phase_ = Phase::Copy;
    return true;
  }
  const bool started = loopPasses_
      ? apply_->beginSelectedHistoryBatch(*work_, raw_, count, *source_, selectedHistory_, rowCount_, loopLength_)
      : apply_->beginHistoryBatch(*work_, raw_, count, *source_, history_, rowCount_, loopLength_);
  if (!started) return false;
  borrowed_ = true; applyGeneration_ = apply_->generation(); phase_ = Phase::Apply;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::begin(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const LoopPasses& passes, const PlaybackViewSourceIdentity& source, uint32_t loopLength) {
  if (passes.overdubPasses.size() > capacity_ ||
      passes.editPasses.size() > RuntimePlaybackViewConfig::kRecordsPerBridge) return false;
  if (!begin(apply, work, nullptr, 0, nullptr, 0, source, loopLength)) return false;
  loopPasses_ = &passes; passes_ = selectedPasses_; phase_ = Phase::SelectRecord; return true;
}
NOTE_EDIT_MEM uint64_t RuntimeCanonicalPassInputs::requiredSelectedGrainsUpperBound(
    size_t capacity, size_t rows, size_t quantum) {
  const auto preparation = requiredGrainsUpperBound(capacity, capacity, rows, quantum);
  if (preparation == UINT64_MAX) return UINT64_MAX;
  const uint64_t visits = uint64_t(capacity) * (capacity + 1) + rows;
  return preparation + (visits ? 1 + (visits - 1) / quantum : 0) + capacity + 4;
}
NOTE_EDIT_MEM bool RuntimeCanonicalPassInputs::ready() const { return phase_ == Phase::Complete && valid(); }
NOTE_EDIT_MEM const CaptureAdmissionStaging* RuntimeCanonicalPassInputs::frozenCapture() const {
  return ready() && live_ ? &live_->capture : nullptr;
}
NOTE_EDIT_MEM RuntimeCompanionApply::HistoryPrefix RuntimeCanonicalPassInputs::historyPrefix() const {
  if (!ready()) return {};
  return {history_, loopPasses_ ? selectedHistory_ : nullptr, rowCount_};
}
NOTE_EDIT_MEM const RuntimeCompanionPasses::Pass* RuntimeCanonicalPassInputs::data() const { return ready() ? outputs_ : nullptr; }
NOTE_EDIT_MEM size_t RuntimeCanonicalPassInputs::passCount() const { return ready() ? passCount_ : 0; }
NOTE_EDIT_MEM size_t RuntimeCanonicalPassInputs::eventCount() const { return ready() ? count_ : 0; }
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCanonicalPassInputs::deny() {
  releaseApply(); read_.discard(); phase_ = Phase::Denied;
  return RuntimePlaybackOrderBuildResult::Denied;
}
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCanonicalPassInputs::step(size_t quantum) {
  if (!quantum || phase_ == Phase::Idle) return RuntimePlaybackOrderBuildResult::Denied;
  if (phase_ == Phase::Denied || !valid()) return deny();
  if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  if (phase_ == Phase::SelectRecord) {
    const auto& record = loopPasses_->recordPass;
    if (loopPasses_->hasRecordPass() && record.state == CapturePassState::Active && !record.committedChunkIds.empty())
      selectedPasses_[passCount_++] = {record.committedChunkIds.data(), record.committedChunkIds.size()};
    phase_ = Phase::SelectOverdub;
  } else if (phase_ == Phase::SelectOverdub) {
    const auto& overdubs = loopPasses_->overdubPasses;
    while (quantum-- && selectionCursor_ < overdubs.size()) {
      const size_t index = selectionCursor_++;
      const auto& pass = overdubs[index];
      if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty() ||
          (haveSequence_ && pass.mergeSequence <= lastSequence_)) continue;
      if (bestOverdub_ != SIZE_MAX && pass.mergeSequence == overdubs[bestOverdub_].mergeSequence) return deny();
      if (bestOverdub_ == SIZE_MAX || pass.mergeSequence < overdubs[bestOverdub_].mergeSequence) bestOverdub_ = index;
    }
    if (selectionCursor_ == overdubs.size()) {
      if (bestOverdub_ == SIZE_MAX) { selectionCursor_ = 0; phase_ = Phase::FilterHistory; }
      else {
        if (passCount_ == capacity_) return deny();
        const auto& pass = overdubs[bestOverdub_];
        selectedPasses_[passCount_++] = {pass.committedChunkIds.data(), pass.committedChunkIds.size()};
        lastSequence_ = pass.mergeSequence; haveSequence_ = true;
        bestOverdub_ = SIZE_MAX; selectionCursor_ = 0;
      }
    }
  } else if (phase_ == Phase::FilterHistory) {
    const auto& rows = loopPasses_->editPasses;
    while (quantum-- && selectionCursor_ < rows.size()) {
      const auto& row = rows[selectionCursor_++];
      if (row.state == EditPassState::Active && row.passType == EditPassType::Note) selectedHistory_[rowCount_++] = &row;
    }
    if (selectionCursor_ == rows.size()) {
      if (pass_ > passCount_) return deny();
      phase_ = pass_ < passCount_ ? Phase::Start :
          (includeCapture_ ? Phase::CaptureOrder : Phase::Complete);
    }
  } else if (phase_ == Phase::Start) {
    const auto& pass = passes_[pass_];
    if (pass.count > LoopEventStoreConfig::POOL_CHUNK_COUNT ||
        !LoopEventStore::beginReadSealedChunks(read_, pass.chunks, pass.count, raw_, capacity_, generation_)) return deny();
    phase_ = Phase::Read;
  } else if (phase_ == Phase::Read) {
    const auto result = LoopEventStore::stepReadSealedChunks(read_, quantum);
    if (result == LoopEventStoreReadResult::Denied) return deny();
    if (result == LoopEventStoreReadResult::Complete) {
      if (!beginApply(read_.size())) return deny();
    }
  } else if (phase_ == Phase::CaptureOrder) {
    if (!captureStarted_) {
      if (!captureOrderBuild_.beginCanonicalIndexes(live_->capture.storage, captureOrder_, captureScratch_, capacity_)) return deny();
      captureStarted_ = true;
      return RuntimePlaybackOrderBuildResult::Pending;
    }
    const auto result = captureOrderBuild_.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      captureOrderedCount_ = captureOrderBuild_.orderedEventCount();
      if (captureOrderedCount_ == 0) phase_ = Phase::Complete;
      else {
        if (passCount_ == capacity_) return deny();
        ++passCount_; copy_ = 0; phase_ = Phase::CaptureCopy;
      }
    }
  } else if (phase_ == Phase::CopyCaptureOrder) {
    while (quantum-- && copy_ < captureOrderedCount_) {
      const uint16_t index = preparedCaptureOrder_->indexes[copy_];
      const auto& storage = live_->capture.storage;
      if (index >= captureCount_ || !storage.eventRetained(index)) return deny();
      if (copy_ != 0) {
        const uint16_t previous = captureOrder_[copy_ - 1];
        if (storage.events[index].tick < storage.events[previous].tick ||
            (storage.events[index].tick == storage.events[previous].tick && index <= previous)) return deny();
      }
      captureOrder_[copy_++] = index;
    }
    if (copy_ == captureOrderedCount_) {
      preparedCaptureOrder_.reset(); // Shared order may now be reused independently.
      if (!captureOrderedCount_) phase_ = Phase::Complete;
      else {
        if (passCount_ == capacity_) return deny();
        ++passCount_; copy_ = 0; phase_ = Phase::CaptureCopy;
      }
    }
  } else if (phase_ == Phase::CaptureCopy) {
    const size_t count = captureOrderedCount_;
    while (quantum-- && copy_ < count) {
      raw_[copy_] = live_->capture.storage.events[captureOrder_[copy_]]; ++copy_;
    }
    if (copy_ == count && !beginApply(count)) return deny();
  } else if (phase_ == Phase::Apply) {
    const auto result = apply_->step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      if (!apply_->ready(expected_) || apply_->size() > capacity_ - count_) return deny();
      copy_ = 0; outputs_[pass_] = {effective_ + count_, apply_->size()}; phase_ = Phase::Copy;
    }
  } else if (phase_ == Phase::Copy) {
    while (quantum-- && copy_ < outputs_[pass_].count) {
      effective_[count_ + copy_] = (rowCount_ ? apply_->data() : raw_)[copy_]; ++copy_;
    }
    if (copy_ == outputs_[pass_].count) {
      count_ += copy_; ++pass_; releaseApply(); read_.discard();
      phase_ = pass_ != passCount_ ? Phase::Start :
          (includeCapture_ && !captureStarted_ ? Phase::CaptureOrder : Phase::Complete);
    }
  }
  return phase_ == Phase::Complete ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}

NOTE_EDIT_MEM size_t RuntimeCanonicalPassInputs::frozenCaptureEventCount() const {
  return ready() && includeCapture_ ? captureOrderedCount_ : 0;
}
NOTE_EDIT_MEM const MidiEvent* RuntimeCanonicalPassInputs::frozenCaptureEvent(size_t index) const {
  return ready() && includeCapture_ && live_ && index < captureOrderedCount_
      ? &live_->capture.storage.events[captureOrder_[index]] : nullptr;
}
