// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCompanionPasses.h"
#include "RuntimeCanonicalPassInputs.h"
#include "RuntimeCaptureCompanions.h"
#include "Utils/NoteEditMem.h"
#include <cstdlib>
#include <new>

namespace {
}
NOTE_EDIT_MEM RuntimeCompanionPasses::~RuntimeCompanionPasses() {
  // Borrowed owners need not outlive destruction; do not dereference them here.
  for (size_t buffer = 0; buffer < 2; ++buffer) {
    if (events_[buffer] != nullptr)
      for (size_t i = 0; i < capacity_; ++i) events_[buffer][i].~MidiEvent();
    externalFirstFree(events_[buffer]); externalFirstFree(origins_[buffer]);
  }
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::prewarm(size_t capacity) {
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView) return false;
  if (capacity_ != 0) return capacity == capacity_;
  MidiEvent* events[2]; Origin* origins[2];
  for (size_t i = 0; i < 2; ++i) {
    events[i] = static_cast<MidiEvent*>(externalFirstAlloc(capacity * sizeof(MidiEvent)));
    origins[i] = static_cast<Origin*>(externalFirstAlloc(capacity * sizeof(Origin)));
  }
  if (events[0] == nullptr || events[1] == nullptr || origins[0] == nullptr || origins[1] == nullptr) {
    for (size_t i = 0; i < 2; ++i) { externalFirstFree(events[i]); externalFirstFree(origins[i]); }
    return false;
  }
  for (size_t buffer = 0; buffer < 2; ++buffer) {
    events_[buffer] = events[buffer]; origins_[buffer] = origins[buffer];
    for (size_t i = 0; i < capacity; ++i) {
      new (&events_[buffer][i]) MidiEvent(); new (&origins_[buffer][i]) Origin{};
    }
  }
  capacity_ = capacity; return true;
}
NOTE_EDIT_MEM uint64_t RuntimeCompanionPasses::requiredGrainsUpperBound(
    size_t capacity, size_t passCount, size_t historyCount, size_t rowCount, size_t quantum) {
  const auto apply = RuntimeCompanionApply::requiredContinuationGrainsUpperBound(capacity, historyCount, rowCount, quantum);
  if (apply == UINT64_MAX || passCount > RuntimePlaybackViewConfig::kEventsPerView) return UINT64_MAX;
  return 1 + uint64_t(passCount)*((rowCount ? apply : 0) + 1 + (capacity-1)/quantum + 4);
}
NOTE_EDIT_MEM void RuntimeCompanionPasses::releaseApply() {
  if (borrowed_ && apply_ != nullptr && apply_->generation() == applyGeneration_) apply_->discard();
  borrowed_ = false;
}
NOTE_EDIT_MEM void RuntimeCompanionPasses::discard() {
  ++generation_;
  releaseApply(); apply_ = nullptr; work_.reset(); passes_ = nullptr; source_ = nullptr;
  inputOwner_ = nullptr; inputGeneration_ = 0;
  rowOwner_ = nullptr; rowGeneration_ = 0;
  count_ = output_ = left_ = right_ = pass_ = 0; current_ = 0; phase_ = Phase::Idle;
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::begin(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const Pass* passes, size_t passCount, const EditPass* rows, size_t rowCount,
    const PlaybackViewSourceIdentity& source, uint32_t loopLength) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Denied) ||
      capacity_ == 0 || !apply.available() || (passes == nullptr && passCount != 0) ||
      passCount > RuntimePlaybackViewConfig::kEventsPerView || (rows == nullptr && rowCount != 0) ||
      rowCount > RuntimePlaybackViewConfig::kRecordsPerBridge || loopLength == 0) return false;
  discard(); apply_ = &apply; work_.emplace(work); passes_ = passes; passCount_ = passCount;
  rows_ = rows; rowCount_ = rowCount; source_ = &source; expected_ = source; loopLength_ = loopLength;
  phase_ = passCount == 0 ? Phase::Complete : Phase::Start; return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::valid() const {
  return source_ != nullptr && *source_ == expected_ &&
         (!inputOwner_ || (inputOwner_->generation() == inputGeneration_ && inputOwner_->ready() &&
                          inputOwner_->normalizedScratchMatches(*this, generation_))) &&
         (!rowOwner_ || (rowOwner_->preparationGeneration() == rowGeneration_ &&
                        inputOwner_ && inputOwner_->frozenCapture() &&
                        rowOwner_->ready(*inputOwner_->frozenCapture()))) &&
         (!borrowed_ || (apply_ != nullptr && apply_->generation() == applyGeneration_));
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::begin(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const RuntimeCanonicalPassInputs& inputs, const EditPass* rows, size_t rowCount,
    const PlaybackViewSourceIdentity& source, uint32_t loopLength) {
  if (!inputs.ready() || inputs.loopLength() != loopLength) return false;
  if (!begin(apply, work, inputs.data(), inputs.passCount(), rows, rowCount, source, loopLength)) return false;
  inputOwner_ = &inputs; inputGeneration_ = inputs.generation();
  inputs.claimNormalizedScratch(*this, generation_); return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::ready(const PlaybackViewSourceIdentity& source) const {
  return phase_ == Phase::Complete && valid() && source == expected_;
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::matchesOwners(
    const RuntimeCanonicalPassInputs& inputs, const RuntimeCaptureCompanions& rows) const {
  return inputOwner_ == &inputs && rowOwner_ == &rows && ready(expected_);
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::begin(
    RuntimeCompanionApply& apply, const RuntimeCompanionApply::Work& work,
    const RuntimeCanonicalPassInputs& inputs, const RuntimeCaptureCompanions& rows,
    const PlaybackViewSourceIdentity& source, uint32_t loopLength) {
  const auto* capture = inputs.frozenCapture();
  if (!capture || !rows.ready(*capture)) return false;
  if (!begin(apply, work, inputs, rows.data(capture->frozenGeneration), rows.size(), source, loopLength)) return false;
  rowOwner_ = &rows; rowGeneration_ = rows.preparationGeneration(); return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionPasses::normalizationMatches(
    const RuntimeCanonicalPassInputs& inputs, const PlaybackViewSourceIdentity& committed) const {
  return phase_ == Phase::Complete && inputOwner_ == &inputs &&
      inputGeneration_ == inputs.generation() && inputs.normalizedScratchMatches(*this, generation_) &&
      expected_.playbackRevision == committed.playbackRevision &&
      expected_.sourceGeneration == committed.sourceGeneration;
}
NOTE_EDIT_MEM const MidiEvent* RuntimeCompanionPasses::data() const { return ready(expected_) ? events_[current_] : nullptr; }
NOTE_EDIT_MEM const RuntimeCompanionPasses::Origin* RuntimeCompanionPasses::origins() const {
  return ready(expected_) ? origins_[current_] : nullptr;
}
NOTE_EDIT_MEM const MidiEvent* RuntimeCompanionPasses::inputEvent(Origin origin) const {
  if (!ready(expected_) || origin.pass >= passCount_ || origin.event >= passes_[origin.pass].count) return nullptr;
  return &passes_[origin.pass].events[origin.event];
}
NOTE_EDIT_MEM size_t RuntimeCompanionPasses::size() const { return ready(expected_) ? count_ : 0; }
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionPasses::deny() {
  releaseApply(); phase_ = Phase::Denied; return RuntimePlaybackOrderBuildResult::Denied;
}
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionPasses::step(size_t quantum) {
  if (quantum == 0 || phase_ == Phase::Idle) return RuntimePlaybackOrderBuildResult::Denied;
  if (phase_ == Phase::Denied || !valid()) return deny();
  if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  if (phase_ == Phase::Start) {
    const auto& input = passes_[pass_];
    if (input.count > capacity_ || (input.events == nullptr && input.count != 0)) return deny();
    // Input descriptors may not borrow either aggregate buffer: the next merge
    // writes one while the other remains its immutable input.
    for (size_t i = 0; i < 2; ++i) {
      const uintptr_t first = reinterpret_cast<uintptr_t>(input.events);
      const uintptr_t own = reinterpret_cast<uintptr_t>(events_[i]);
      if (input.count != 0 && first < own + capacity_ * sizeof(MidiEvent) &&
          own < first + input.count * sizeof(MidiEvent)) return deny();
    }
    if (rowCount_ == 0) {
      if (input.count > capacity_ - count_) return deny();
      if (inputOwner_) inputOwner_->normalizedDescriptors()[pass_] =
          {inputOwner_->normalizedScratch() + count_, input.count};
      left_ = right_ = output_ = 0; phase_ = Phase::Merge;
      return RuntimePlaybackOrderBuildResult::Pending;
    }
    const bool started = inputOwner_
        ? apply_->beginContinuationBatch(*work_, input.events, input.count, *source_, rows_, rowCount_,
                                         loopLength_, inputOwner_->historyPrefix())
        : apply_->beginBatch(*work_, input.events, input.count, *source_, rows_, rowCount_, loopLength_, true);
    if (!started) return deny();
    borrowed_ = true; applyGeneration_ = apply_->generation(); phase_ = Phase::Apply;
  } else if (phase_ == Phase::Apply) {
    const auto result = apply_->step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      if (apply_->size() > capacity_ - count_) return deny();
      if (inputOwner_) inputOwner_->normalizedDescriptors()[pass_] =
          {inputOwner_->normalizedScratch() + count_, apply_->size()};
      left_ = right_ = output_ = 0; phase_ = Phase::Merge;
    }
  } else if (phase_ == Phase::Merge) {
    if (rowCount_ && !apply_->ready(expected_)) return deny();
    const auto* addition = rowCount_ ? apply_->data() : passes_[pass_].events;
    const size_t added = rowCount_ ? apply_->size() : passes_[pass_].count;
    const auto* provenance = rowCount_ ? apply_->provenance() : nullptr;
    while (quantum != 0 && (left_ < count_ || right_ < added)) {
      --quantum;
      // Match std::merge: the earlier aggregate wins equal raw ticks. Do not
      // sort/normalize a pass again after the reference's shortening branch.
      const bool takeLeft = right_ == added || (left_ < count_ && !(addition[right_].tick < events_[current_][left_].tick));
      if (takeLeft) {
        events_[1-current_][output_] = events_[current_][left_];
        origins_[1-current_][output_] = origins_[current_][left_++];
      } else {
        events_[1-current_][output_] = addition[right_];
        if (inputOwner_) inputOwner_->normalizedScratch()[count_ + right_] = addition[right_];
        const uint32_t origin = provenance ? provenance[right_] : static_cast<uint32_t>(right_);
        ++right_;
        origins_[1-current_][output_] = {static_cast<uint16_t>(pass_),
          static_cast<uint16_t>(origin == UINT32_MAX ? UINT16_MAX : origin)};
      }
      ++output_;
    }
    if (left_ == count_ && right_ == added) {
      count_ = output_; current_ = 1-current_; releaseApply(); ++pass_;
      phase_ = pass_ == passCount_ ? Phase::Complete : Phase::Start;
    }
  }
  return phase_ == Phase::Complete ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}
