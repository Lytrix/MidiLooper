//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackOrderBuilder.h"

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
  if (mode == RuntimePlaybackOrderMode::SnapshotCanonical &&
      (!view.sourceFilled || view.passOrigins == nullptr || view.passOriginsReady)) return false;
  if (mode == RuntimePlaybackOrderMode::SnapshotPlayback &&
      (!view.canonicalRanksReady || view.canonicalRanks == nullptr)) return false;
  const auto storageMode = mode == RuntimePlaybackOrderMode::SnapshotCanonical
      ? RuntimePlaybackOrderMode::RawPlaybackTick : mode == RuntimePlaybackOrderMode::SnapshotPlayback
      ? RuntimePlaybackOrderMode::PlaybackPhase : mode;
  if (!begin(view.storage, loopLengthTicks, nullptr, storageMode)) {
    return false;
  }
  orderMode_ = mode;
  completionFlag_ = mode == RuntimePlaybackOrderMode::SnapshotCanonical ? nullptr : &view.complete;
  view.complete = completionFlag_ != nullptr && complete();
  if (mode == RuntimePlaybackOrderMode::SnapshotCanonical) {
    snapshotEventIds_ = view.passOrigins;
    phaseOutput_ = nullptr;  // Fill already wrote canonicalOrder, not playback phases.
  } else if (mode == RuntimePlaybackOrderMode::SnapshotPlayback) {
    snapshotRanks_ = view.canonicalRanks;
  }
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::begin(FixedPlaybackEventStorage& storage,
                                        uint32_t loopLengthTicks, const uint32_t* provenance, RuntimePlaybackOrderMode mode) {
  discard();
  // Snapshot modes require a typed view's metadata; raw storage cannot supply it.
  if (mode == RuntimePlaybackOrderMode::SnapshotCanonical || mode == RuntimePlaybackOrderMode::SnapshotPlayback)
    return false;
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
  if (!storage.isPrewarmed() || ((mode == RuntimePlaybackOrderMode::PlaybackPhase || mode == RuntimePlaybackOrderMode::SnapshotPlayback) && loopLengthTicks == 0) || storage.eventCount > storage.capacity) {
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

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::extendPrivateOrder() {
  if (!complete() || storage_ == nullptr || completionFlag_ != nullptr ||
      !storage_->isPrewarmed() || phaseOutput_ != storage_->phases ||
      orderEntries_ != storage_->order || scratchEntries_ != storage_->mergeScratch ||
      storage_->capacity > UINT16_MAX || storage_->eventCount > storage_->capacity ||
      storage_->eventCount < frozenSourceExtent_) return false;
  if (storage_->eventCount == frozenSourceExtent_) return true;
  prefixEventCount_ = static_cast<uint16_t>(orderedEventCount_);
  cursor_ = frozenSourceExtent_;
  frozenSourceExtent_ = static_cast<uint16_t>(storage_->eventCount);
  runWidth_ = 1; runStart_ = prefixEventCount_;
  leftCursor_ = rightCursor_ = leftEnd_ = rightEnd_ = outputCursor_ = 0;
  orderedDuringFill_ = true; mergeReadsScratch_ = false;
  phase_ = Phase::DeltaFill;
  return true;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::extendPrivateOrderPreservingPrefix() {
  if (!extendPrivateOrder()) return false;
  // The same delta merge sorts only `[prefix, count)`; the frozen phases make completion concatenate that sorted tail
  // after the published prefix instead of running the full prefix merge. A zero-append call returns before any phase.
  if (phase_ == Phase::DeltaFill) phase_ = Phase::FrozenDeltaFill;
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
  const size_t multiplier = 1 + mergePasses + (mergePasses & 1u);
  return eventCount > std::numeric_limits<size_t>::max() / multiplier
             ? std::numeric_limits<size_t>::max()
             : eventCount * multiplier;
}

PLAYBACK_ORDER_MAINTENANCE_MEM size_t RuntimePlaybackOrderBuilder::extensionWorkEntriesUpperBound(
    size_t prefixEvents, size_t appendedRows) {
  if (appendedRows == 0) return 0;
  const size_t suffixWork = workEntriesUpperBound(appendedRows);
  if (prefixEvents == 0) return suffixWork;
  const size_t max = std::numeric_limits<size_t>::max();
  if (appendedRows > max - prefixEvents) return max;
  const size_t total = prefixEvents + appendedRows;
  return total > (max - suffixWork) / 2 ? max : suffixWork + 2 * total;
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
  if (orderMode_ == RuntimePlaybackOrderMode::SnapshotCanonical) {
    if (storage.phases[left] != storage.phases[right]) return storage.phases[left] < storage.phases[right];
    const auto& l = snapshotEventIds_[left];
    const auto& r = snapshotEventIds_[right];
    if (l.pass != r.pass) return l.pass < r.pass;
    if (l.event != r.event) return l.event < r.event;
    return left < right;
  }
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
  const uint8_t leftKind = leftEvent.isNoteOff() ? 0 : (leftEvent.isNoteOn() ? 1 : 2);
  const uint8_t rightKind = rightEvent.isNoteOff() ? 0 : (rightEvent.isNoteOn() ? 1 : 2);
  if (leftKind != rightKind) {
    return leftKind < rightKind;
  }
  // `tick == loopLength` and `tick == 0` project to the same audible phase.
  // Apply the canonical Off-before-On rule before the raw-tick tie-break so a
  // wrap boundary cannot leave a NoteOn ahead of its same-phase NoteOff.
  if (leftEvent.tick != rightEvent.tick) {
    return leftEvent.tick < rightEvent.tick;
  }
  if (orderMode_ == RuntimePlaybackOrderMode::SnapshotPlayback) {
    if (snapshotRanks_[left] != snapshotRanks_[right]) return snapshotRanks_[left] < snapshotRanks_[right];
  } else if (provenance_ != nullptr && provenance_[left] != provenance_[right]) {
    return provenance_[left] < provenance_[right];
  }
  return left < right;
}

PLAYBACK_ORDER_MAINTENANCE_MEM bool RuntimePlaybackOrderBuilder::startNextMergeRun() {
  const size_t count = orderedEventCount_;
  if (runStart_ >= count) {
    runStart_ = phase_ == Phase::DeltaMerge ? prefixEventCount_ : 0;
    cursor_ = runStart_;
    // A completed level becomes the next input without an N-entry copy-back.
    mergeReadsScratch_ = !mergeReadsScratch_;
    runWidth_ *= 2;
    // Keep the frozen suffix range while moving to its next merge level.
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
  if ((phase_ == Phase::DeltaFill || phase_ == Phase::DeltaMerge || phase_ == Phase::DeltaCopyBack ||
       phase_ == Phase::FrozenDeltaFill || phase_ == Phase::FrozenDeltaMerge ||
       phase_ == Phase::FrozenDeltaCopyBack || phase_ == Phase::PrefixMerge ||
       phase_ == Phase::PrefixCopyBack) &&
      (storage.eventCount < frozenSourceExtent_ || storage.eventCount > storage.capacity)) {
    phase_ = Phase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  size_t remaining = entryQuantum;

  // Finish zero-entry phase transitions even when the last entry consumed
  // this turn's quantum. Completion itself must not require an unreserved turn.
  while (true) {
    if (phase_ == Phase::Fill || phase_ == Phase::DeltaFill || phase_ == Phase::FrozenDeltaFill) {
      const bool frozenFill = phase_ == Phase::FrozenDeltaFill;
      const bool delta = phase_ == Phase::DeltaFill || frozenFill;
      const size_t sourceEnd = delta ? frozenSourceExtent_ : storage.eventCount;
      if (cursor_ == sourceEnd) {
        if (!delta) frozenSourceExtent_ = static_cast<uint16_t>(std::min(sourceEnd, size_t(UINT16_MAX)));
        runWidth_ = orderedDuringFill_ ? orderedEventCount_ - (delta ? prefixEventCount_ : 0) : 1;
        runStart_ = delta ? prefixEventCount_ : 0;
        phase_ = delta ? (frozenFill ? Phase::FrozenDeltaMerge : Phase::DeltaMerge) : Phase::Merge;
        continue;
      }
      if (remaining == 0) return RuntimePlaybackOrderBuildResult::Pending;
      const size_t sourceIndex = cursor_++;
      --remaining;  // Filtered rows consume input quantum too.
      if (!storage.eventRetained(sourceIndex)) {
        continue;
      }
      orderEntries_[orderedEventCount_] = static_cast<uint16_t>(sourceIndex);
      if (phaseOutput_ != nullptr) phaseOutput_[sourceIndex] = (orderMode_ == RuntimePlaybackOrderMode::PlaybackPhase || orderMode_ == RuntimePlaybackOrderMode::SnapshotPlayback)
                                    ? IntervalProjection::playbackEventPhase(
                                          storage.events[sourceIndex].tick, loopLengthTicks_)
                                    : storage.events[sourceIndex].tick;
      if (orderedDuringFill_ && orderedEventCount_ != (delta ? prefixEventCount_ : 0) &&
          isBefore(static_cast<uint16_t>(sourceIndex), orderEntries_[orderedEventCount_ - 1]))
        orderedDuringFill_ = false;
      ++orderedEventCount_;
      continue;
    }

    if (phase_ == Phase::Merge || phase_ == Phase::DeltaMerge || phase_ == Phase::FrozenDeltaMerge) {
      const bool frozenDelta = phase_ == Phase::FrozenDeltaMerge;
      const bool delta = phase_ == Phase::DeltaMerge || frozenDelta;
      const size_t first = delta ? prefixEventCount_ : 0;
      if (runWidth_ >= orderedEventCount_ - first) {
        if (mergeReadsScratch_) {
          cursor_ = first;
          phase_ = delta ? (frozenDelta ? Phase::FrozenDeltaCopyBack : Phase::DeltaCopyBack) : Phase::CopyBack;
          continue;
        }
        if (delta && !frozenDelta && prefixEventCount_ != 0 && orderedEventCount_ != prefixEventCount_) {
          leftCursor_ = 0; leftEnd_ = prefixEventCount_;
          rightCursor_ = prefixEventCount_; rightEnd_ = orderedEventCount_; outputCursor_ = 0;
          phase_ = Phase::PrefixMerge;
          continue;
        }
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
      const uint16_t* input = mergeReadsScratch_ ? scratchEntries_ : orderEntries_;
      uint16_t* output = mergeReadsScratch_ ? orderEntries_ : scratchEntries_;
      if (rightCursor_ >= rightEnd_ ||
          (leftCursor_ < leftEnd_ && isBefore(input[leftCursor_], input[rightCursor_]))) {
        output[outputCursor_++] = input[leftCursor_++];
      } else {
        output[outputCursor_++] = input[rightCursor_++];
      }
      --remaining;
      continue;
    }

    if (phase_ == Phase::PrefixMerge) {
      if (outputCursor_ == orderedEventCount_) { cursor_ = 0; phase_ = Phase::PrefixCopyBack; continue; }
      if (remaining == 0) return RuntimePlaybackOrderBuildResult::Pending;
      if (rightCursor_ >= rightEnd_ ||
          (leftCursor_ < leftEnd_ && isBefore(orderEntries_[leftCursor_], orderEntries_[rightCursor_])))
        scratchEntries_[outputCursor_++] = orderEntries_[leftCursor_++];
      else scratchEntries_[outputCursor_++] = orderEntries_[rightCursor_++];
      --remaining;
      continue;
    }
    if (phase_ == Phase::PrefixCopyBack) {
      if (cursor_ == orderedEventCount_) { phase_ = Phase::Complete; return RuntimePlaybackOrderBuildResult::Complete; }
      if (remaining == 0) return RuntimePlaybackOrderBuildResult::Pending;
      orderEntries_[cursor_] = scratchEntries_[cursor_]; ++cursor_; --remaining;
      continue;
    }

    if (phase_ == Phase::CopyBack || phase_ == Phase::DeltaCopyBack ||
        phase_ == Phase::FrozenDeltaCopyBack) {
      if (cursor_ == orderedEventCount_) {
        // Only the final normalization is copied. Completion still exposes the
        // original caller-owned order pointer, never the scratch pointer.
        mergeReadsScratch_ = false;
        const bool frozenDelta = phase_ == Phase::FrozenDeltaCopyBack;
        const bool delta = phase_ == Phase::DeltaCopyBack || frozenDelta;
        runStart_ = delta ? prefixEventCount_ : 0;
        leftEnd_ = rightEnd_ = outputCursor_ = 0;
        cursor_ = runStart_;
        phase_ = delta ? (frozenDelta ? Phase::FrozenDeltaMerge : Phase::DeltaMerge) : Phase::Merge;
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
  prefixEventCount_ = frozenSourceExtent_ = 0;
  orderedDuringFill_ = true;
  mergeReadsScratch_ = false;
  phase_ = Phase::Idle;
}
