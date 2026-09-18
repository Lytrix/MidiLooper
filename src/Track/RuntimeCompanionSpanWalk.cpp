// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCompanionSpanWalk.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/NoteEditMem.h"
#include <cstdlib>

NOTE_EDIT_MEM RuntimeCompanionSpanWalk::RuntimeCompanionSpanWalk() = default;
NOTE_EDIT_MEM RuntimeCompanionSpanWalk::~RuntimeCompanionSpanWalk() {
  if (flags_ == nullptr) return;
  externalFirstFree(flags_);
}
NOTE_EDIT_MEM bool RuntimeCompanionSpanWalk::prewarm(size_t capacity) {
  if (flags_ != nullptr) return capacity == capacity_;
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView) return false;
  flags_ = static_cast<uint8_t*>(externalFirstAlloc(capacity));
  if (flags_ == nullptr) return false;
  capacity_ = capacity;
  return true;
}
NOTE_EDIT_MEM void RuntimeCompanionSpanWalk::discard() {
  if (wrap_ != nullptr) wrap_->discard();
  wrap_ = nullptr;
  events_ = nullptr;
  retained_ = nullptr;
  count_ = cursor_ = scan_ = visited_ = activeCount_ = off_ = 0;
  pair_ = back_ = SIZE_MAX;
  loopLength_ = tailStart_ = head_ = wrappedTick_ = 0;
  pitch_ = offZeroTotal_ = offZeroPaired_ = remainingZero_ = 0;
  beyond_ = preferred_ = removeWrappedActive_ = spanReady_ = false;
  span_ = CanonicalNoteSpan{};
  phase_ = Phase::Idle;
}
NOTE_EDIT_MEM bool RuntimeCompanionSpanWalk::begin(
    RuntimeNotePairLookup& wrapLookup, const MidiEvent* events, size_t count,
    uint8_t pitch, uint32_t loopLength, const uint8_t* retainedRows) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete) || flags_ == nullptr ||
      !wrapLookup.available() || count > capacity_ || (events == nullptr && count != 0) ||
      loopLength == 0 || loopLength > INT32_MAX || pitch >= 128) return false;
  discard();
  wrap_ = &wrapLookup;
  events_ = events;
  retained_ = retainedRows;
  count_ = count;
  pitch_ = pitch;
  loopLength_ = loopLength;
  tailStart_ = NoteUtils::wrapTailStartTick(loopLength);
  phase_ = Phase::Initialize;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionSpanWalk::retained(size_t index) const {
  return (retained_ == nullptr || retained_[index]) &&
         (events_[index].isNoteOn() || events_[index].isNoteOff()) &&
         events_[index].data.noteData.note == pitch_;
}
NOTE_EDIT_MEM bool RuntimeCompanionSpanWalk::startQuery(Query query, size_t target) {
  if (!wrap_->beginReconstructionWrap(events_, count_, off_, loopLength_, target, retained_)) return false;
  query_ = query;
  phase_ = Phase::Query;
  return true;
}
NOTE_EDIT_MEM void RuntimeCompanionSpanWalk::emit(
    size_t on, uint32_t endExclusive, bool open, bool split, Phase next) {
  const auto& event = events_[on];
  span_ = {event.noteId, {static_cast<int32_t>(event.tick), static_cast<int32_t>(endExclusive)},
           pitch_, event.data.noteData.velocity, open, split};
  spanReady_ = true;
  phase_ = next;
}
NOTE_EDIT_MEM void RuntimeCompanionSpanWalk::emitPair(bool wrapped) {
  flags_[pair_] &= ~Active;
  --activeCount_;
  if (!wrapped) {
    emit(pair_, events_[off_].tick + 1u, false, false, Phase::Events);
    return;
  }
  if (head_ == 0) ++offZeroPaired_;
  const uint32_t adjusted = wrap_->reconstructedHeadTick();
  emit(pair_, adjusted == 0 ? loopLength_ : loopLength_ + adjusted + 1u,
       false, adjusted != 0, Phase::Events);
}
NOTE_EDIT_MEM void RuntimeCompanionSpanWalk::finishAutoQuery() {
  const int result = wrap_->result();
  if (result >= 0 && (query_ == Query::Empty || !(flags_[result] & Wrapped))) {
    wrappedTick_ = events_[result].tick;
    removeWrappedActive_ = query_ == Query::Stack;
    scan_ = 0;
    if (head_ == 0) ++offZeroPaired_;
    const uint32_t adjusted = wrap_->reconstructedHeadTick();
    emit(result, adjusted == 0 ? loopLength_ : loopLength_ + adjusted + 1u,
         false, adjusted != 0, Phase::MarkWrapped);
  } else if (query_ == Query::Empty) {
    scan_ = cursor_;
    phase_ = Phase::EqualTick;
  } else {
    if (back_ != SIZE_MAX && events_[back_].tick == events_[off_].tick) {
      flags_[back_] &= ~Active;
      --activeCount_;
    }
    phase_ = Phase::Events;
  }
}
NOTE_EDIT_MEM bool RuntimeCompanionSpanWalk::consumeSpan() {
  if (!spanReady_) return false;
  spanReady_ = false;
  return true;
}
NOTE_EDIT_MEM RuntimeCompanionSpanResult RuntimeCompanionSpanWalk::step(size_t quantum) {
  visited_ = 0;
  if (phase_ == Phase::Idle || quantum == 0) return RuntimeCompanionSpanResult::Denied;
  if (spanReady_) return RuntimeCompanionSpanResult::SpanReady;
  while (true) {
    if (spanReady_) return RuntimeCompanionSpanResult::SpanReady;
    if (phase_ == Phase::Complete) { wrap_ = nullptr; return RuntimeCompanionSpanResult::Complete; }
    if (phase_ == Phase::Query) {
      if (quantum == 0) return RuntimeCompanionSpanResult::Pending;
      const auto result = wrap_->step(quantum);
      const size_t spent = wrap_->lastStepInputCount();
      visited_ += spent;
      quantum -= spent;
      if (result == RuntimePlaybackOrderBuildResult::Denied) return RuntimeCompanionSpanResult::Denied;
      if (result == RuntimePlaybackOrderBuildResult::Pending) return RuntimeCompanionSpanResult::Pending;
      if (query_ == Query::Preferred) {
        if (wrap_->result() >= 0) { pair_ = back_; preferred_ = true; phase_ = Phase::SelectPair; }
        else if (!startQuery(Query::Stack)) return RuntimeCompanionSpanResult::Denied;
      } else if (query_ == Query::Pair) emitPair(wrap_->result() >= 0);
      else finishAutoQuery();
      continue;
    }
    if (phase_ == Phase::SelectPair) {
      const uint32_t tick = events_[pair_].tick;
      if (!beyond_ && head_ == loopLength_ - 1 && tick >= tailStart_) {
        scan_ = 0;
        phase_ = Phase::DeferEnd;
      } else if (head_ < tick && tick >= tailStart_ && (!beyond_ || tailStart_ > 0)) {
        if (preferred_) emitPair(true);
        else if (!startQuery(Query::Pair, pair_)) return RuntimeCompanionSpanResult::Denied;
      } else emitPair(false);
      continue;
    }
    if (phase_ == Phase::Initialize && scan_ == count_) { scan_ = 0; phase_ = Phase::Events; continue; }
    if (phase_ == Phase::Events && cursor_ == count_) {
      remainingZero_ = offZeroTotal_ > offZeroPaired_ ? offZeroTotal_ - offZeroPaired_ : 0;
      scan_ = count_;
      phase_ = Phase::Leftovers;
      continue;
    }
    if (phase_ == Phase::FindPair && scan_ == 0) {
      if (back_ != SIZE_MAX && head_ < events_[back_].tick && events_[back_].tick >= tailStart_) {
        if (!startQuery(Query::Preferred, back_)) return RuntimeCompanionSpanResult::Denied;
      } else if (!startQuery(Query::Stack)) return RuntimeCompanionSpanResult::Denied;
      continue;
    }
    if (phase_ == Phase::DeferEnd && scan_ == count_) { phase_ = Phase::Events; continue; }
    if ((phase_ == Phase::MarkWrapped || phase_ == Phase::EqualTick) && scan_ == count_) {
      phase_ = Phase::Events;
      continue;
    }
    if (phase_ == Phase::Leftovers && (scan_ == 0 || remainingZero_ == 0)) {
      scan_ = 0; phase_ = Phase::Finish; continue;
    }
    if (phase_ == Phase::Finish && scan_ == count_) { phase_ = Phase::Complete; continue; }
    if (quantum == 0) return RuntimeCompanionSpanResult::Pending;
    --quantum;
    ++visited_;
    if (phase_ == Phase::Initialize) {
      flags_[scan_] = 0;
      if (retained(scan_) && events_[scan_].isNoteOff() && events_[scan_].tick == 0) ++offZeroTotal_;
      ++scan_;
    } else if (phase_ == Phase::Events) {
      const size_t index = cursor_++;
      if (!retained(index)) continue;
      const auto& event = events_[index];
      if (event.isNoteOn()) {
        if (!(flags_[index] & (Consumed | Wrapped)) && event.tick < loopLength_) {
          flags_[index] |= Active;
          ++activeCount_;
        }
      } else {
        off_ = index; head_ = event.tick % loopLength_; beyond_ = event.tick >= loopLength_;
        pair_ = back_ = SIZE_MAX; preferred_ = false;
        if (activeCount_ == 0) {
          if (!startQuery(Query::Empty)) return RuntimeCompanionSpanResult::Denied;
        } else { scan_ = index; phase_ = Phase::FindPair; }
      }
    } else if (phase_ == Phase::FindPair) {
      const size_t index = --scan_;
      if (!(flags_[index] & Active)) continue;
      if (back_ == SIZE_MAX) back_ = index;
      if (events_[index].tick < events_[off_].tick) { pair_ = index; phase_ = Phase::SelectPair; }
    } else if (phase_ == Phase::DeferEnd) {
      const size_t index = scan_++;
      // At an actual Off@L-1, the reference's second branch includes that
      // very Off. Deferral therefore reduces to its latest-tail predicate.
      if (retained(index) && events_[index].isNoteOn() && events_[index].channel == events_[off_].channel &&
          events_[index].tick < loopLength_ && events_[index].tick > events_[pair_].tick)
        emitPair(false);
    } else if (phase_ == Phase::EqualTick) {
      const size_t index = scan_++;
      if (retained(index) && !(flags_[index] & Consumed) && events_[index].isNoteOn() &&
          events_[index].channel == events_[off_].channel && events_[index].tick == head_) {
        flags_[index] |= Consumed;
        phase_ = Phase::Events;
      }
    } else if (phase_ == Phase::MarkWrapped) {
      const size_t index = scan_++;
      if (retained(index) && events_[index].isNoteOn() && events_[index].tick == wrappedTick_) {
        flags_[index] |= Wrapped;
        if (removeWrappedActive_ && (flags_[index] & Active)) {
          flags_[index] &= ~Active;
          --activeCount_;
          removeWrappedActive_ = false;
        }
      }
    } else if (phase_ == Phase::Leftovers) {
      const size_t index = --scan_;
      if ((flags_[index] & Active) && events_[index].tick >= tailStart_) {
        flags_[index] &= ~Active; --activeCount_; --remainingZero_;
        emit(index, loopLength_, false, false, Phase::Leftovers);
      }
    } else if (phase_ == Phase::Finish) {
      const size_t index = scan_++;
      if (flags_[index] & Active) {
        flags_[index] &= ~Active; --activeCount_;
        emit(index, loopLength_, true, false, Phase::Finish);
      }
    }
  }
}
