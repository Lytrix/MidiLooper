//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackOrderBuilder.h"
#include "Utils/PlaybackEventOrder.h"

#include <algorithm>
#include <limits>

#include "Utils/IntervalProjection.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define PLAYBACK_ORDER_MAINTENANCE_MEM FLASHMEM
#else
#define PLAYBACK_ORDER_MAINTENANCE_MEM
#endif

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::begin(RuntimePlaybackView& view, uint32_t loopLengthTicks, RuntimePlaybackOrderMode mode) {
  if (!begin(view.storage, loopLengthTicks, nullptr, mode)) {
    return false;
  }
  completionFlag_ = &view.complete;
  view.complete = complete();
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::begin(FixedPlaybackEventStorage& storage,
                                        uint32_t loopLengthTicks, const uint32_t* provenance, RuntimePlaybackOrderMode mode) {
  discard();
  storage_ = &storage;
  orderEntries_ = storage.order; scratchEntries_ = storage.mergeScratch; phaseOutput_ = storage.phases;
  provenance_ = provenance;
  completionFlag_ = nullptr;
  loopLengthTicks_ = loopLengthTicks;
  orderMode_ = mode;
  cursor_ = 0;
  runWidth_ = 1;
  runStart_ = 0;
  orderedEventCount_ = 0;
  if (!storage.isPrewarmed() || (mode == RuntimePlaybackOrderMode::PlaybackPhase && loopLengthTicks == 0) || storage.eventCount > storage.capacity) {
    phase_ = Phase::Denied;
    return false;
  }
  phase_ = storage.eventCount == 0 ? Phase::Complete : Phase::Fill;
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::beginIndexes(
    const FixedPlaybackEventStorage& source, uint16_t* order, uint16_t* scratch,
    uint32_t loopLengthTicks, const uint32_t* ranks) {
  discard();
  if (!source.isPrewarmed() || source.eventCount > source.capacity || loopLengthTicks == 0 ||
      order == nullptr || scratch == nullptr || order == scratch ||
      order == source.order || scratch == source.order ||
      order == source.mergeScratch || scratch == source.mergeScratch) return false;
  storage_ = &source; orderEntries_ = order; scratchEntries_ = scratch;
  provenance_ = ranks; loopLengthTicks_ = loopLengthTicks;
  phase_ = source.eventCount == 0 ? Phase::Complete : Phase::Fill;
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::beginCanonicalCapture(FixedPlaybackEventStorage& storage) {
  discard();
  storage_ = &storage;
  orderEntries_ = storage.order; scratchEntries_ = storage.mergeScratch; phaseOutput_ = storage.phases;
  provenance_ = nullptr;
  completionFlag_ = nullptr;
  loopLengthTicks_ = 0;
  orderMode_ = RuntimePlaybackOrderMode::CanonicalCaptureTick;
  cursor_ = 0;
  runWidth_ = 1;
  runStart_ = 0;
  orderedEventCount_ = 0;
  if (!storage.isPrewarmed() || storage.eventCount > storage.capacity) {
    phase_ = Phase::Denied;
    return false;
  }
  phase_ = storage.eventCount == 0 ? Phase::Complete : Phase::Fill;
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::beginCanonicalIndexes(
    const FixedPlaybackEventStorage& source, uint16_t* order, uint16_t* scratch, size_t indexCapacity) {
  if (source.eventCount > indexCapacity) { discard(); return false; }
  if (!beginIndexes(source, order, scratch, 1, nullptr)) return false;
  orderMode_ = RuntimePlaybackOrderMode::CanonicalCaptureTick;
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM size_t RuntimePlaybackOrderBuilder::workEntriesUpperBound(size_t eventCount) {
  if (eventCount == 0) {
    return 0;
  }
  size_t mergePasses = 0;
  for (size_t width = 1; width < eventCount && mergePasses < sizeof(size_t) * 8; width <<= 1) {
    ++mergePasses;
  }
  const size_t multiplier = 1 + 2 * mergePasses;
  return eventCount > std::numeric_limits<size_t>::max() / multiplier
             ? std::numeric_limits<size_t>::max()
             : eventCount * multiplier;
}

PLAYBACK_ORDER_MAINTENANCE_MEM size_t RuntimePlaybackOrderBuilder::requiredGrains(size_t eventCount, size_t entryQuantum) {
  if (entryQuantum == 0) {
    return std::numeric_limits<size_t>::max();
  }
  const size_t work = workEntriesUpperBound(eventCount);
  return work == 0 ? 0 : 1 + (work - 1) / entryQuantum;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::isBefore(uint16_t left, uint16_t right) const {
  const FixedPlaybackEventStorage& storage = *storage_;
  if (orderMode_ == RuntimePlaybackOrderMode::CanonicalCaptureTick) {
    if (storage.events[left].tick != storage.events[right].tick) {
      return storage.events[left].tick < storage.events[right].tick;
    }
    // Match the existing stable_sort capture oracle: equal ticks preserve the
    // frozen append order rather than applying audible Off-before-On ordering.
    return left < right;
  }
  if (storage.phases[left] != storage.phases[right]) {
    return storage.phases[left] < storage.phases[right];
  }
  const MidiEvent& leftEvent = storage.events[left];
  const MidiEvent& rightEvent = storage.events[right];
  const uint8_t leftKind = PlaybackEventOrder::eventKind(leftEvent);
  const uint8_t rightKind = PlaybackEventOrder::eventKind(rightEvent);
  if (leftKind != rightKind) {
    return leftKind < rightKind;
  }
  // `tick == loopLength` and `tick == 0` project to the same audible phase.
  // Apply the canonical Off-before-On rule before the raw-tick tie-break so a
  // wrap boundary cannot leave a NoteOn ahead of its same-phase NoteOff.
  if (leftEvent.tick != rightEvent.tick) {
    return leftEvent.tick < rightEvent.tick;
  }
  if (provenance_ != nullptr && provenance_[left] != provenance_[right])
    return provenance_[left] < provenance_[right];
  return left < right;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::startNextMergeRun() {
  const size_t count = orderedEventCount_;
  if (runStart_ >= count) {
    runStart_ = 0;
    cursor_ = 0;
    phase_ = Phase::CopyBack;
    return false;
  }
  leftCursor_ = runStart_;
  leftEnd_ = std::min(runStart_ + runWidth_, count);
  rightCursor_ = leftEnd_;
  rightEnd_ = std::min(leftEnd_ + runWidth_, count);
  outputCursor_ = runStart_;
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM RuntimePlaybackOrderBuildResult RuntimePlaybackOrderBuilder::step(size_t entryQuantum) {
  if (phase_ == Phase::Complete) {
    return RuntimePlaybackOrderBuildResult::Complete;
  }
  if (phase_ == Phase::Denied || storage_ == nullptr || entryQuantum == 0) {
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  const FixedPlaybackEventStorage& storage = *storage_;
  size_t remaining = entryQuantum;

  // Finish zero-entry phase transitions even when the last entry consumed
  // this turn's quantum. Completion itself must not require an unreserved turn.
  while (true) {
    if (phase_ == Phase::Fill) {
      if (cursor_ == storage.eventCount) {
        runWidth_ = orderedDuringFill_ ? orderedEventCount_ : 1;
        runStart_ = 0;
        phase_ = Phase::Merge;
        continue;
      }
      if (remaining == 0) return RuntimePlaybackOrderBuildResult::Pending;
      const size_t sourceIndex = cursor_++;
      --remaining;  // Filtered rows consume input quantum too.
      if (!storage.eventRetained(sourceIndex)) {
        continue;
      }
      orderEntries_[orderedEventCount_] = static_cast<uint16_t>(sourceIndex);
      if (phaseOutput_ != nullptr) phaseOutput_[sourceIndex] = orderMode_ == RuntimePlaybackOrderMode::PlaybackPhase
                                    ? IntervalProjection::playbackEventPhase(
                                          storage.events[sourceIndex].tick, loopLengthTicks_)
                                    : storage.events[sourceIndex].tick;
      if (orderedDuringFill_ && orderedEventCount_ != 0 &&
          isBefore(static_cast<uint16_t>(sourceIndex), orderEntries_[orderedEventCount_ - 1]))
        orderedDuringFill_ = false;
      ++orderedEventCount_;
      continue;
    }

    if (phase_ == Phase::Merge) {
      if (runWidth_ >= orderedEventCount_) {
        phase_ = Phase::Complete;
        if (completionFlag_ != nullptr) {
          *completionFlag_ = true;
        }
        return RuntimePlaybackOrderBuildResult::Complete;
      }
      if (outputCursor_ == 0 && leftEnd_ == 0 && rightEnd_ == 0 && !startNextMergeRun()) {
        continue;
      }
      if (rightCursor_ >= rightEnd_ && leftCursor_ >= leftEnd_) {
        runStart_ += runWidth_ * 2;
        leftEnd_ = rightEnd_ = outputCursor_ = 0;
        continue;
      }
      if (remaining == 0) return RuntimePlaybackOrderBuildResult::Pending;
      if (rightCursor_ >= rightEnd_ ||
          (leftCursor_ < leftEnd_ && isBefore(orderEntries_[leftCursor_], orderEntries_[rightCursor_]))) {
        scratchEntries_[outputCursor_++] = orderEntries_[leftCursor_++];
      } else {
        scratchEntries_[outputCursor_++] = orderEntries_[rightCursor_++];
      }
      --remaining;
      continue;
    }

    if (phase_ == Phase::CopyBack) {
      if (cursor_ == orderedEventCount_) {
        runWidth_ *= 2;
        runStart_ = 0;
        leftEnd_ = rightEnd_ = outputCursor_ = 0;
        cursor_ = 0;
        phase_ = Phase::Merge;
        continue;
      }
      if (remaining == 0) return RuntimePlaybackOrderBuildResult::Pending;
      orderEntries_[cursor_] = scratchEntries_[cursor_];
      ++cursor_;
      --remaining;
      continue;
    }
  }
}

PLAYBACK_ORDER_MAINTENANCE_MEM void RuntimePlaybackOrderBuilder::discard() {
  storage_ = nullptr;
  orderEntries_ = scratchEntries_ = nullptr; phaseOutput_ = nullptr;
  provenance_ = nullptr;
  completionFlag_ = nullptr;
  loopLengthTicks_ = 0;
  orderMode_ = RuntimePlaybackOrderMode::PlaybackPhase;
  cursor_ = 0;
  runWidth_ = 1;
  runStart_ = 0;
  leftCursor_ = rightCursor_ = leftEnd_ = rightEnd_ = outputCursor_ = 0;
  orderedEventCount_ = 0;
  orderedDuringFill_ = true;
  phase_ = Phase::Idle;
}
