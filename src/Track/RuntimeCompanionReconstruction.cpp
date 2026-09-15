// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCompanionReconstruction.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/NoteEditMem.h"
#include <cstdlib>
#include <new>

NOTE_EDIT_MEM RuntimeCompanionReconstruction::RuntimeCompanionReconstruction() = default;
NOTE_EDIT_MEM RuntimeCompanionReconstruction::~RuntimeCompanionReconstruction() {
  if (notes_ == nullptr) return;
  if (isInExternalMemoryPool(notes_)) extmem_free(notes_);
  else std::free(notes_);
}
NOTE_EDIT_MEM bool RuntimeCompanionReconstruction::prewarm(size_t capacity) {
  if (notes_ != nullptr) return capacity == capacity_;
  if (capacity == 0 || capacity > 2 * RuntimePlaybackViewConfig::kEventsPerView) return false;
  void* memory = extmem_malloc(capacity * sizeof(NoteUtils::DisplayNote));
  if (memory == nullptr) memory = std::malloc(capacity * sizeof(NoteUtils::DisplayNote));
  if (memory == nullptr) return false;
  notes_ = static_cast<NoteUtils::DisplayNote*>(memory);
  for (size_t i = 0; i < capacity; ++i) new (notes_ + i) NoteUtils::DisplayNote{};
  capacity_ = capacity;
  return true;
}
NOTE_EDIT_MEM void RuntimeCompanionReconstruction::discard() {
  if (spans_ != nullptr) spans_->discard();
  spans_ = nullptr;
  source_ = nullptr;
  expected_ = PlaybackViewSourceIdentity{};
  count_ = compare_ = visited_ = 0;
  loopLength_ = 0;
  pendingCount_ = pendingIndex_ = 0;
  phase_ = Phase::Idle;
}
NOTE_EDIT_MEM bool RuntimeCompanionReconstruction::begin(
    RuntimeCompanionSpanWalk& spans, RuntimeNotePairLookup& wrap,
    const MidiEvent* events, size_t count, uint8_t pitch, uint32_t loopLength,
    const PlaybackViewSourceIdentity& source, const uint8_t* retained) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Denied) ||
      notes_ == nullptr) return false;
  // Do not discard a different in-flight borrowed span walk on admission failure.
  if (!spans.begin(wrap, events, count, pitch, loopLength, retained)) return false;
  spans_ = &spans;
  source_ = &source;
  expected_ = source;
  count_ = compare_ = visited_ = 0;
  pendingCount_ = pendingIndex_ = 0;
  loopLength_ = loopLength;
  phase_ = Phase::Spans;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionReconstruction::sourceMatches() const {
  return source_ != nullptr && *source_ == expected_;
}
NOTE_EDIT_MEM bool RuntimeCompanionReconstruction::ready(const PlaybackViewSourceIdentity& source) const {
  return phase_ == Phase::Complete && sourceMatches() && source == expected_;
}
NOTE_EDIT_MEM const NoteUtils::DisplayNote* RuntimeCompanionReconstruction::data() const {
  return sourceMatches() && phase_ == Phase::Complete ? notes_ : nullptr;
}
NOTE_EDIT_MEM size_t RuntimeCompanionReconstruction::size() const {
  return data() == nullptr ? 0 : count_;
}
NOTE_EDIT_MEM void RuntimeCompanionReconstruction::projectSpan() {
  const auto& span = spans_->span();
  pendingCount_ = pendingIndex_ = 0;
  // The pitch span walk emits only in-loop On starts. For a valid positive
  // interval the full-loop display reference always selects storage alignment;
  // equivalent shifted intervals cannot replace it. Invalid intervals emit none.
  if (span.interval.start >= 0 && static_cast<uint32_t>(span.interval.start) < loopLength_ &&
      span.interval.end > span.interval.start) {
    uint32_t end = static_cast<uint32_t>(span.interval.end);
    if (span.isOpen) end = loopLength_ - 1;
    else if (end > loopLength_) { end %= loopLength_; end = end == 0 ? loopLength_ - 1 : end - 1; }
    else --end;
    pending_[0] = {span.noteId, span.pitch, span.velocity,
                   static_cast<uint32_t>(span.interval.start), span.splitHeadTail ? loopLength_ - 1 : end};
    pendingCount_ = 1;
    if (span.splitHeadTail) {
      const auto head = NoteUtils::resolveWrapHeadSegment(loopLength_, end,
                                             NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
      if (head.visible) {
        pending_[1] = {span.noteId, span.pitch, span.velocity, head.startTick, head.endTickInclusive};
        pendingCount_ = 2;
      }
    }
  }
  spans_->consumeSpan();
  compare_ = 0;
  phase_ = Phase::Dedupe;
}
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionReconstruction::step(size_t quantum) {
  visited_ = 0;
  if (phase_ == Phase::Idle || quantum == 0) return RuntimePlaybackOrderBuildResult::Denied;
  if (phase_ == Phase::Denied || !sourceMatches()) {
    if (spans_ != nullptr) spans_->discard();
    spans_ = nullptr;
    phase_ = Phase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  while (true) {
    if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
    if (phase_ == Phase::Dedupe && pendingIndex_ == pendingCount_) { phase_ = Phase::Spans; continue; }
    if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
    if (phase_ == Phase::Spans) {
      const auto result = spans_->step(quantum);
      const size_t spent = spans_->lastStepInputCount();
      visited_ += spent; quantum -= spent;
      if (result == RuntimeCompanionSpanResult::Denied) {
        spans_->discard(); spans_ = nullptr; phase_ = Phase::Denied;
        return RuntimePlaybackOrderBuildResult::Denied;
      }
      if (result == RuntimeCompanionSpanResult::Complete) {
        spans_ = nullptr; phase_ = Phase::Complete; continue;
      }
      if (result == RuntimeCompanionSpanResult::Pending) return RuntimePlaybackOrderBuildResult::Pending;
      phase_ = Phase::Project;
      continue;
    }
    --quantum; ++visited_;
    if (phase_ == Phase::Project) { projectSpan(); continue; }
    auto& pending = pending_[pendingIndex_];
    if (compare_ < count_) {
      const auto& prior = notes_[compare_++];
      // Geometry-only dedupe keeps the first projected row, including its ID
      // and velocity. This is the reference's ranked-key/original-index rule.
      if (prior.note == pending.note && prior.startTick == pending.startTick && prior.endTick == pending.endTick) {
        ++pendingIndex_; compare_ = 0;
      }
    } else {
      if (count_ == capacity_) {
        spans_->discard(); spans_ = nullptr; phase_ = Phase::Denied;
        return RuntimePlaybackOrderBuildResult::Denied;
      }
      notes_[count_++] = pending;
      ++pendingIndex_; compare_ = 0;
    }
  }
}
