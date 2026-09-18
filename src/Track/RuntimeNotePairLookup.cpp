// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeNotePairLookup.h"
#include "LoopEventStore.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include <cstdlib>
#include "Utils/NoteUtils.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define NOTE_PAIR_MEM FLASHMEM
#else
#define NOTE_PAIR_MEM
#endif

NOTE_PAIR_MEM RuntimeNotePairLookup::RuntimeNotePairLookup() = default;

NOTE_PAIR_MEM RuntimeNotePairLookup::~RuntimeNotePairLookup() {
  if (lane_ == nullptr) return;
  if (isInExternalMemoryPool(lane_)) extmem_free(lane_);
  else std::free(lane_);
}

NOTE_PAIR_MEM bool RuntimeNotePairLookup::prewarm(size_t capacity) {
  if (lane_ != nullptr) return capacity_ == capacity;
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView) return false;
  const size_t bytes = capacity * (2 * sizeof(uint16_t) + sizeof(uint8_t));
  void* memory = extmem_malloc(bytes);
  if (memory == nullptr) memory = std::malloc(bytes);
  if (memory == nullptr) return false;
  lane_ = static_cast<uint16_t*>(memory);
  stack_ = lane_ + capacity;
  paired_ = reinterpret_cast<uint8_t*>(stack_ + capacity);
  capacity_ = capacity;
  return true;
}

NOTE_PAIR_MEM void RuntimeNotePairLookup::discard() {
  events_ = nullptr;
  retained_ = nullptr;
  count_ = target_ = cursor_ = laneCount_ = stackCount_ = runStart_ = runEnd_ = untaggedCount_ = 0;
  runTick_ = loopLength_ = latestTailTick_ = 0;
  lastStepInputCount_ = 0;
  reconstructionTarget_ = reconstructionBestOn_ = SIZE_MAX;
  reconstructedHeadTick_ = occupiedHeadTick_ = 0;
  reconstructionMidOn_ = reconstructionLinearCloser_ = false;
  untagged_ = result_ = -1;
  phase_ = Phase::Idle;
}

NOTE_PAIR_MEM bool RuntimeNotePairLookup::begin(const MidiEvent* events, size_t count,
                                               size_t targetOn, const uint8_t* retained) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete) || lane_ == nullptr || events == nullptr ||
      count > capacity_ || targetOn >= count || !events[targetOn].isNoteOn() ||
      (retained != nullptr && retained[targetOn] == 0)) return false;
  discard();
  events_ = events;
  retained_ = retained;
  count_ = count;
  target_ = targetOn;
  phase_ = Phase::Collect;
  return true;
}

NOTE_PAIR_MEM bool RuntimeNotePairLookup::beginWrapHead(
    const MidiEvent* events, size_t count, size_t targetOn, uint32_t loopLength,
    const uint8_t* retained) {
  if (loopLength == 0 || !begin(events, count, targetOn, retained)) return false;
  loopLength_ = loopLength;
  latestTailTick_ = events[targetOn].tick;
  phase_ = Phase::WrapTail;
  return true;
}

NOTE_PAIR_MEM bool RuntimeNotePairLookup::beginReconstructionWrap(
    const MidiEvent* events, size_t count, size_t offIndex, uint32_t loopLength,
    size_t targetOn, const uint8_t* retained) {
  if (!available() || events == nullptr || count > capacity_ || offIndex >= count ||
      !events[offIndex].isNoteOff() || loopLength == 0 ||
      (retained != nullptr && !retained[offIndex]) ||
      (targetOn != SIZE_MAX && (targetOn >= count || !events[targetOn].isNoteOn() ||
       events[targetOn].data.noteData.note != events[offIndex].data.noteData.note ||
       (retained != nullptr && !retained[targetOn])))) return false;
  discard();
  events_ = events;
  retained_ = retained;
  count_ = count;
  target_ = offIndex;
  loopLength_ = loopLength;
  reconstructionTarget_ = targetOn;
  reconstructedHeadTick_ = occupiedHeadTick_ = events[offIndex].tick % loopLength;
  phase_ = Phase::ReconstructionOns;
  return true;
}

NOTE_PAIR_MEM RuntimePlaybackOrderBuildResult RuntimeNotePairLookup::step(size_t quantum) {
  lastStepInputCount_ = 0;
  if (quantum == 0 || phase_ == Phase::Idle) return RuntimePlaybackOrderBuildResult::Denied;
  const MidiEvent& target = events_[target_];
  while (true) {
    if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
    if (phase_ == Phase::ReconstructionOns || phase_ == Phase::ReconstructionOffs) {
      if (cursor_ == count_) {
        if (phase_ == Phase::ReconstructionOns) {
          const size_t chosen = reconstructionTarget_ == SIZE_MAX
                                  ? reconstructionBestOn_ : reconstructionTarget_;
          if (chosen == SIZE_MAX ||
              !NoteUtils::isHeadTailWrappedPair(events_[chosen].tick, reconstructedHeadTick_, loopLength_) ||
              (reconstructionBestOn_ != SIZE_MAX && events_[reconstructionBestOn_].tick > events_[chosen].tick) ||
              (reconstructionTarget_ == SIZE_MAX && reconstructionMidOn_)) {
            phase_ = Phase::Complete;
            continue;
          }
          reconstructionBestOn_ = chosen;
          cursor_ = 0;
          phase_ = Phase::ReconstructionOffs;
        } else {
          if (!reconstructionLinearCloser_) {
            result_ = static_cast<int>(reconstructionBestOn_);
            if (untaggedCount_ >= 2) reconstructedHeadTick_ = occupiedHeadTick_;
          }
          phase_ = Phase::Complete;
        }
        continue;
      }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      const size_t index = cursor_++;
      const auto& event = events_[index];
      if ((retained_ != nullptr && !retained_[index]) ||
          (!event.isNoteOn() && !event.isNoteOff()) || event.channel != target.channel ||
          event.data.noteData.note != target.data.noteData.note) continue;
      if (phase_ == Phase::ReconstructionOns) {
        if (!event.isNoteOn() || event.tick >= loopLength_) continue;
        if (reconstructionBestOn_ == SIZE_MAX || event.tick > events_[reconstructionBestOn_].tick)
          reconstructionBestOn_ = index; // First On wins equal-tick ties, as in the reference.
        if (event.tick > reconstructedHeadTick_ && event.tick < NoteUtils::wrapTailStartTick(loopLength_))
          reconstructionMidOn_ = true;
        if (event.tick > 0 && event.tick <= reconstructedHeadTick_ && event.tick - 1 < occupiedHeadTick_)
          occupiedHeadTick_ = event.tick - 1;
      } else if (event.isNoteOff()) {
        const uint32_t tick = event.tick % loopLength_;
        if (tick == reconstructedHeadTick_) ++untaggedCount_;
        // The On pass proved no later in-loop On in this lane. Thus a
        // normalized Off after the tail has no intervening On; the nested
        // linear-closer scan reduces to this one visit per Off.
        if (tick > events_[reconstructionBestOn_].tick && tick != loopLength_ - 1)
          reconstructionLinearCloser_ = true;
      }
    } else if (phase_ == Phase::Collect) {
      if (cursor_ == count_) { phase_ = Phase::RunStart; continue; }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      const size_t index = cursor_++;
      --quantum;
      ++lastStepInputCount_;
      const auto& event = events_[index];
      if ((retained_ == nullptr || retained_[index]) && (event.isNoteOn() || event.isNoteOff()) &&
          event.channel == target.channel && event.data.noteData.note == target.data.noteData.note) {
        lane_[laneCount_] = static_cast<uint16_t>(index);
        paired_[laneCount_++] = 0;
      }
    } else if (phase_ == Phase::RunStart) {
      if (runStart_ == laneCount_) { cursor_ = 0; phase_ = Phase::Wrap; continue; }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      runTick_ = events_[lane_[runStart_]].tick;
      runEnd_ = runStart_ + 1;
      phase_ = Phase::RunEnd;
    } else if (phase_ == Phase::RunEnd) {
      if (runEnd_ == laneCount_) { cursor_ = runStart_; phase_ = Phase::Offs; continue; }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      if (events_[lane_[runEnd_]].tick == runTick_) ++runEnd_;
      else { cursor_ = runStart_; phase_ = Phase::Offs; }
    } else if (phase_ == Phase::Offs) {
      if (cursor_ == runEnd_) { cursor_ = runStart_; phase_ = Phase::Ons; continue; }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      const size_t laneIndex = cursor_++;
      const uint16_t index = lane_[laneIndex];
      if (events_[index].isNoteOff() && stackCount_ != 0) {
        const uint16_t on = stack_[--stackCount_];
        paired_[laneIndex] = 1;
        if (on == target_) { result_ = index; phase_ = Phase::Complete; }
      }
    } else if (phase_ == Phase::Ons) {
      if (cursor_ == runEnd_) { runStart_ = runEnd_; phase_ = Phase::RunStart; continue; }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      const uint16_t index = lane_[cursor_++];
      if (events_[index].isNoteOn()) stack_[stackCount_++] = index;
    } else if (phase_ == Phase::WrapTail || phase_ == Phase::WrapHead) {
      if (cursor_ == count_) {
        if (phase_ == Phase::WrapTail && latestTailTick_ == target.tick) {
          cursor_ = 0;
          phase_ = Phase::WrapHead;
        } else {
          result_ = phase_ == Phase::WrapHead && untaggedCount_ == 1 ? untagged_ : -1;
          phase_ = Phase::Complete;
        }
        continue;
      }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      const size_t index = cursor_++;
      const auto& event = events_[index];
      if ((retained_ != nullptr && !retained_[index]) ||
          (!event.isNoteOn() && !event.isNoteOff()) || event.channel != target.channel ||
          event.data.noteData.note != target.data.noteData.note) continue;
      if (phase_ == Phase::WrapTail) {
        // For any head qualifying for target, every later in-loop On also
        // qualifies: increasing the On preserves tail-window and half-loop
        // distance predicates. One maximum replaces the legacy nested scan.
        if (event.isNoteOn() && event.tick < loopLength_ && event.tick > latestTailTick_)
          latestTailTick_ = event.tick;
      } else if (event.isNoteOff() && NoteUtils::isHeadTailWrappedPair(
                     target.tick, event.tick % loopLength_, loopLength_)) {
        if (target.noteId != kInvalidNoteId && event.noteId == target.noteId) {
          result_ = static_cast<int>(index);
          phase_ = Phase::Complete;
        } else {
          untagged_ = static_cast<int>(index);
          ++untaggedCount_; // Legacy preferred count includes other tagged IDs.
        }
      }
    } else if (phase_ == Phase::Wrap) {
      if (cursor_ == laneCount_) {
        result_ = untaggedCount_ == 1 ? untagged_ : -1;
        phase_ = Phase::Complete;
        continue;
      }
      if (quantum == 0) return RuntimePlaybackOrderBuildResult::Pending;
      --quantum;
      ++lastStepInputCount_;
      const size_t laneIndex = cursor_++;
      const uint16_t index = lane_[laneIndex];
      const auto& event = events_[index];
      if (paired_[laneIndex] || !event.isNoteOff() || event.tick >= target.tick) continue;
      if (target.noteId != kInvalidNoteId && event.noteId == target.noteId) {
        result_ = index;
        phase_ = Phase::Complete;
      } else if (event.noteId == kInvalidNoteId) {
        untagged_ = index;
        ++untaggedCount_;
      }
    }
  }
}

NOTE_PAIR_MEM bool RuntimeNoteDeleteLookup::begin(
    RuntimeNotePairLookup& pairLookup, const MidiEvent* events, size_t count,
    NoteId noteId, uint32_t loopLength, const uint8_t* retained) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete) ||
      !pairLookup.available() || (events == nullptr && count != 0) ||
      count > RuntimePlaybackViewConfig::kEventsPerView)
    return false;
  discard();
  pair_ = &pairLookup;
  events_ = events;
  retained_ = retained;
  count_ = count;
  noteId_ = noteId;
  loopLength_ = loopLength;
  phase_ = Phase::FindOn;
  return true;
}

NOTE_PAIR_MEM void RuntimeNoteDeleteLookup::discard() {
  if (pair_ != nullptr) pair_->discard();
  pair_ = nullptr;
  events_ = nullptr;
  retained_ = nullptr;
  count_ = cursor_ = 0;
  loopLength_ = maxTick_ = 0;
  noteId_ = kInvalidNoteId;
  on_ = off_ = wrapOff_ = -1;
  phase_ = Phase::Idle;
}

NOTE_PAIR_MEM RuntimePlaybackOrderBuildResult RuntimeNoteDeleteLookup::step(size_t quantum) {
  if (quantum == 0 || phase_ == Phase::Idle) return RuntimePlaybackOrderBuildResult::Denied;
  if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  if (phase_ == Phase::FindOn) {
    while (cursor_ < count_ && quantum != 0) {
      --quantum;
      const size_t index = cursor_++;
      if (retained_ != nullptr && !retained_[index]) continue;
      const auto& event = events_[index];
      if (event.tick > maxTick_) maxTick_ = event.tick;
      if (on_ < 0 && noteId_ != kInvalidNoteId && event.isNoteOn() && event.noteId == noteId_)
        on_ = static_cast<int>(index);
    }
    if (cursor_ != count_) return RuntimePlaybackOrderBuildResult::Pending;
    if (on_ < 0) {
      pair_ = nullptr;
      phase_ = Phase::Complete;
      return RuntimePlaybackOrderBuildResult::Complete;
    }
    if (loopLength_ == 0) {
      // Derive the length from the last tick, clamped to one bar minimum.
      loopLength_ = maxTick_ + 1u;
      if (loopLength_ < LoopEventStoreConfig::BAR_TICKS) loopLength_ = LoopEventStoreConfig::BAR_TICKS;
    }
    if (!pair_->begin(events_, count_, static_cast<size_t>(on_), retained_))
      return RuntimePlaybackOrderBuildResult::Denied;
    phase_ = Phase::Pair;
    return RuntimePlaybackOrderBuildResult::Pending;
  }
  const auto result = pair_->step(quantum);
  if (result != RuntimePlaybackOrderBuildResult::Complete) return result;
  if (phase_ == Phase::Pair) {
    off_ = pair_->result();
    if (!pair_->beginWrapHead(events_, count_, static_cast<size_t>(on_), loopLength_, retained_))
      return RuntimePlaybackOrderBuildResult::Denied;
    phase_ = Phase::Wrap;
    return RuntimePlaybackOrderBuildResult::Pending;
  }
  wrapOff_ = pair_->result();
  pair_ = nullptr;
  phase_ = Phase::Complete;
  return RuntimePlaybackOrderBuildResult::Complete;
}
