// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimePlaybackBridge.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define PLAYBACK_BRIDGE_MAINTENANCE_MEM FLASHMEM
#else
#define PLAYBACK_BRIDGE_MAINTENANCE_MEM
#endif

namespace {
uint8_t eventKind(const MidiEvent& event) {
  return event.isNoteOff() ? 0 : (event.isNoteOn() ? 1 : 2);
}

bool matchesOpenNote(const MidiEvent& event, const ActiveNoteLedger::Entry& open) {
  return (event.noteId != kInvalidNoteId && open.noteId == event.noteId) ||
         ((event.noteId == kInvalidNoteId || open.noteId == kInvalidNoteId) &&
          open.note == event.data.noteData.note);
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM size_t afterPhase(const FixedPlaybackEventStorage& storage, const uint16_t* order,
                  size_t count, uint32_t phase) {
  if (phase == UINT32_MAX) return 0;
  size_t first = 0;
  while (first < count) {
    const size_t middle = first + (count - first) / 2;
    if (storage.phases[order[middle]] <= phase) first = middle + 1;
    else count = middle;
  }
  return first;
}
}  // namespace

PLAYBACK_BRIDGE_MAINTENANCE_MEM void RuntimeCaptureBridgeFill::discard() {
  *this = RuntimeCaptureBridgeFill{};
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimeCaptureBridgeFill::begin(
    const CaptureAdmissionStaging& capture, uint32_t frozenGeneration,
    const PlaybackBridgeRecord* companionRecords, size_t companionCount,
    FixedPlaybackBridgeStorage& candidate) {
  if (capture_ != nullptr || !capture.frozen || capture.captureGeneration != frozenGeneration ||
      capture.frozenGeneration != frozenGeneration || capture.openReservationCount() != 0 ||
      !capture.storage.isPrewarmed() || candidate.records == nullptr || candidate.selected ||
      candidate.complete || candidate.recordCount != 0 || !candidate.coverage.isValid() ||
      (companionCount != 0 && companionRecords == nullptr) || companionCount > candidate.capacity ||
      capture.storage.eventCount > candidate.capacity - companionCount) return false;
  // Prove the entire output capacity before touching the unpublished bridge.
  // Filtered rows may make this conservative; they never consume live input
  // admission credits again or cause the accepted capture to be discarded.
  capture_ = &capture;
  companions_ = companionRecords;
  candidate_ = &candidate;
  target_ = candidate.source;
  generation_ = frozenGeneration;
  captureCount_ = capture.storage.eventCount;
  companionCount_ = companionCount;
  cursor_ = outputCount_ = 0;
  complete_ = false;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM RuntimePlaybackOrderBuildResult RuntimeCaptureBridgeFill::step(size_t quantum) {
  if (quantum == 0 || capture_ == nullptr || candidate_ == nullptr ||
      !capture_->frozen || capture_->captureGeneration != generation_ ||
      capture_->frozenGeneration != generation_ || capture_->storage.eventCount != captureCount_ ||
      candidate_->selected || candidate_->complete || !(candidate_->source == target_) ||
      candidate_->recordCount != outputCount_) return RuntimePlaybackOrderBuildResult::Denied;
  if (complete_) return RuntimePlaybackOrderBuildResult::Complete;
  while (quantum != 0 && cursor_ < companionCount_ + captureCount_) {
    const size_t index = cursor_++;
    --quantum;
    if (index < companionCount_) {
      if (!candidate_->append(companions_[index])) return RuntimePlaybackOrderBuildResult::Denied;
    } else {
      const size_t captureIndex = index - companionCount_;
      if (!capture_->storage.eventRetained(captureIndex)) continue;
      if (!candidate_->append({PlaybackBridgeOperation::Insert, 0, capture_->storage.events[captureIndex]}))
        return RuntimePlaybackOrderBuildResult::Denied;
    }
    ++outputCount_;
  }
  complete_ = cursor_ == companionCount_ + captureCount_;
  return complete_ ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM void RuntimePlaybackBridgeBuild::discard() {
  base_ = nullptr;
  bridge_ = nullptr;
  mapping_ = nullptr; mappingGeneration_ = 0; mappedRecordsCopied_ = 0; ranked_ = false;
  order_.discard();
  phase_ = Phase::Idle;
  cursor_ = recordCount_ = insertionCursor_ = 0;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM uint64_t RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(
    size_t baseCount, size_t recordCount, size_t quantum) {
  if (!quantum || baseCount > RuntimePlaybackViewConfig::kEventsPerView ||
      recordCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return UINT64_MAX;
  const uint64_t linear = 3*uint64_t(baseCount) + 3*uint64_t(recordCount);
  return (linear ? 1 + (linear-1)/quantum : 0) +
      RuntimePlaybackOrderBuilder::requiredGrains(baseCount, quantum) +
      RuntimePlaybackOrderBuilder::requiredGrains(recordCount, quantum) + 6;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::begin(const RuntimePlaybackView& base,
                                       FixedPlaybackBridgeStorage& bridge,
                                       const RuntimeCompanionBridgeMap* mapping) {
  discard();
  if (!base.published || !base.complete || !base.coverage.isValid() ||
      bridge.selected || bridge.complete || !bridge.coverage.isValid() ||
      bridge.coverage.loopLengthTicks != base.coverage.loopLengthTicks ||
      base.storage.eventCount > bridge.baseCapacity || bridge.records == nullptr ||
      bridge.baseOrder == nullptr || bridge.baseRetained == nullptr || bridge.insertionProvenance == nullptr ||
      bridge.baseRanks == nullptr || bridge.insertionRanks == nullptr ||
      bridge.rankedBaseOrder == nullptr || bridge.baseOrderScratch == nullptr ||
      bridge.nextCycleBaseOrder == nullptr || bridge.currentCycleInsertionOrder == nullptr ||
      bridge.secondCycleBaseOrder == nullptr || bridge.steadyBaseOrder == nullptr ||
      bridge.nextCycleInsertionOrder == nullptr || bridge.secondCycleInsertionOrder == nullptr ||
      bridge.cursorAnchor.phase >= bridge.coverage.loopLengthTicks || bridge.insertionCycleMask == nullptr ||
      !bridge.insertions.isPrewarmed() || bridge.recordCount > bridge.capacity) return false;
  if (mapping != nullptr && (base.coverage.lengthTicks != base.coverage.loopLengthTicks ||
      bridge.recordCount != 0 || mapping->size() > bridge.capacity ||
      !mapping->matchesBase(base.storage.events, base.storage.eventCount, base.source))) return false;
  mapping_ = mapping; ranked_ = mapping != nullptr;
  mappingGeneration_ = mapping != nullptr ? mapping->generation() : 0;
  base_ = &base;
  bridge_ = &bridge;
  bridge.baseSource = base.source;
  source_ = bridge.source;
  recordCount_ = mapping != nullptr ? mapping->size() : bridge.recordCount;
  openNoteFlags_.fill(0);
  closureTime_.fill(UINT64_MAX);
  newOnPhase_.fill(UINT32_MAX);
  closureIndex_.fill(UINT16_MAX);
  bridge.baseEventCount = 0;
  bridge.nextCycleBaseEventCount = 0;
  bridge.secondCycleBaseEventCount = bridge.steadyBaseEventCount = 0;
  bridge.currentCycleInsertionCount = bridge.nextCycleInsertionCount = bridge.secondCycleInsertionCount = 0;
  bridge.protectsOpenNotes = false;
  bridge.hasRetainedClosure = false;
  bridge.retainedClosurePhaseLimit = UINT32_MAX;
  bridge.preparedAdmissionPhase = bridge.cursorAnchor.phase;
  bridge.insertions.clear();
  bridge.streamPrepared = false;
  phase_ = Phase::Clear;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::touchesOpenNote(const MidiEvent& event, bool mark) {
  if (!event.isNoteOn() && !event.isNoteOff()) return false;
  // The bounded ledger summary is captured by admission, not reconstructed
  // from canonical content. Be conservative about channel remapping here.
  bool found = false;
  for (size_t i = 0; i < bridge_->ledgerBoundary.count; ++i) {
    const auto& open = bridge_->ledgerBoundary.openNotes[i];
    if (matchesOpenNote(event, open)) {
      if (mark) openNoteFlags_[i] |= 1;
      found = true;
    }
  }
  return found;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::insertionMatches(size_t index, size_t openIndex) const {
  const auto& event = bridge_->insertions.events[index];
  if (!event.isNoteOn() && !event.isNoteOff()) return false;
  const auto& open = bridge_->ledgerBoundary.openNotes[openIndex];
  if (matchesOpenNote(event, open)) return true;
  // Replacement provenance carries the old identity even if the edit assigns
  // a new NoteId or pitch to both members of the pair.
  const uint32_t provenance = bridge_->insertionProvenance[index];
  if (provenance >= base_->storage.eventCount) return false;
  const auto& previous = base_->storage.events[provenance];
  return (previous.isNoteOn() || previous.isNoteOff()) && matchesOpenNote(previous, open);
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM uint8_t RuntimePlaybackBridgeBuild::effectiveMask(
    const MidiEvent& event, size_t openIndex) const {
  const uint32_t length = base_->coverage.loopLengthTicks;
  const uint32_t phase = event.tick % length;
  // The open instance owns its original Off. Start the new pair on its first
  // On after that closure; its Off may belong to a further projection cycle.
  uint64_t onTime = event.isNoteOn() ? phase : newOnPhase_[openIndex];
  if (onTime == UINT32_MAX) onTime = phase;
  if (onTime < closureTime_[openIndex])
    onTime += ((closureTime_[openIndex] - onTime + length - 1) / length) * uint64_t(length);
  uint64_t time = onTime;
  if (event.isNoteOff()) {
    const uint32_t onPhase = static_cast<uint32_t>(onTime % length);
    time = onTime - onPhase + phase + (phase <= onPhase ? uint64_t(length) : 0);
  }
  return static_cast<uint8_t>(0x0f << (time / length)) & 0x0f;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM RuntimePlaybackOrderBuildResult RuntimePlaybackBridgeBuild::step(size_t quantum) {
  if (quantum == 0 || base_ == nullptr || bridge_ == nullptr ||
      phase_ == Phase::Denied || bridge_->selected ||
      !(base_->source == bridge_->baseSource) || !(bridge_->source == source_) ||
      ((mapping_ == nullptr ? recordCount_ : mappedRecordsCopied_) != bridge_->recordCount)) return RuntimePlaybackOrderBuildResult::Denied;
  if (mapping_ != nullptr && (mapping_->generation() != mappingGeneration_ ||
      !mapping_->matchesBase(base_->storage.events, base_->storage.eventCount, base_->source))) {
    phase_ = Phase::Denied; return RuntimePlaybackOrderBuildResult::Denied;
  }
  if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  if (bridge_->ledgerBoundary.overflowed ||
      bridge_->ledgerBoundary.count > bridge_->ledgerBoundary.openNotes.size()) {
    phase_ = Phase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  }
  auto deny = [&]() {
    phase_ = Phase::Denied;
    return RuntimePlaybackOrderBuildResult::Denied;
  };
  while (quantum != 0) {
    if (phase_ == Phase::Clear) {
      if (cursor_ < base_->storage.eventCount) {
        const size_t index = cursor_++;
        const uint32_t rank = mapping_ != nullptr ? mapping_->baseRanks()[index] : index;
        // A removed Off can remain temporarily as a ledger closure. It keeps
        // its old index as a tie key; equal rank collisions use provenance.
        bridge_->baseRanks[index] = rank == UINT16_MAX ? index : rank;
        bridge_->baseRetained[index] = 15;  // Admission, first wrap, second wrap, steady state.
        --quantum;
        continue;
      }
      cursor_ = 0;
      phase_ = Phase::Records;
    }
    if (phase_ == Phase::Records) {
      if (cursor_ < recordCount_) {
        const size_t recordIndex = cursor_++;
        if (mapping_ != nullptr && (bridge_->recordCount != recordIndex ||
            !bridge_->append(mapping_->records()[recordIndex]))) return deny();
        if (mapping_ != nullptr) ++mappedRecordsCopied_;
        const PlaybackBridgeRecord& record = bridge_->records[recordIndex];
        --quantum;
        if (record.operation != PlaybackBridgeOperation::Insert &&
            record.operation != PlaybackBridgeOperation::Suppress &&
            record.operation != PlaybackBridgeOperation::Replace) return deny();
        if (record.operation != PlaybackBridgeOperation::Insert) {
          const size_t index = record.eventProvenance;
          if (index >= base_->storage.eventCount || bridge_->baseRetained[index] == 0) return deny();
          bridge_->protectsOpenNotes = touchesOpenNote(base_->storage.events[index], true) ||
                                      bridge_->protectsOpenNotes;
          bridge_->baseRetained[index] = 0;
        }
        if (record.operation != PlaybackBridgeOperation::Suppress) {
          if (!bridge_->insertions.append(record.event)) return deny();
          bridge_->insertionCycleMask[bridge_->insertions.eventCount - 1] =
              touchesOpenNote(record.event, true) ||
              (record.operation == PlaybackBridgeOperation::Replace &&
               touchesOpenNote(base_->storage.events[record.eventProvenance]));
          bridge_->protectsOpenNotes = bridge_->protectsOpenNotes ||
                                      bridge_->insertionCycleMask[bridge_->insertions.eventCount - 1];
          bridge_->insertionProvenance[bridge_->insertions.eventCount - 1] =
              record.operation == PlaybackBridgeOperation::Replace ? record.eventProvenance
                  : static_cast<uint32_t>(bridge_->baseCapacity + cursor_ - 1);
          bridge_->insertionRanks[bridge_->insertions.eventCount - 1] = mapping_ != nullptr
              ? mapping_->recordRanks()[recordIndex]
              : bridge_->insertionProvenance[bridge_->insertions.eventCount - 1];
        }
        continue;
      }
      cursor_ = 0;
      phase_ = Phase::Closures;
    }
    if (phase_ == Phase::Closures) {
      if (cursor_ < base_->storage.eventCount + bridge_->insertions.eventCount) {
        const bool inserted = cursor_ >= base_->storage.eventCount;
        const size_t index = inserted ? cursor_ - base_->storage.eventCount : cursor_;
        ++cursor_;
        --quantum;
        const auto& event = inserted ? bridge_->insertions.events[index] : base_->storage.events[index];
        if (!event.isNoteOn() && !event.isNoteOff()) continue;
        const uint32_t phase = event.tick % base_->coverage.loopLengthTicks;
        for (size_t i = 0; i < bridge_->ledgerBoundary.count; ++i) {
          if (!(openNoteFlags_[i] & 1) ||
              !(inserted ? insertionMatches(index, i) : matchesOpenNote(event, bridge_->ledgerBoundary.openNotes[i])))
            continue;
          if (!inserted && event.isNoteOff()) {
            const uint64_t time = uint64_t(phase) + (phase < bridge_->preparedAdmissionPhase ? base_->coverage.loopLengthTicks : 0);
            if (time < closureTime_[i]) {
              closureTime_[i] = time;
              closureIndex_[i] = static_cast<uint16_t>(index);
            }
            openNoteFlags_[i] |= 2;
          }
          if (event.isNoteOn() && (inserted || bridge_->baseRetained[index])) {
            if (newOnPhase_[i] == UINT32_MAX || phase > newOnPhase_[i]) newOnPhase_[i] = phase;
          }
        }
        continue;
      }
      bridge_->hasRetainedClosure = true;
      for (size_t i = 0; i < bridge_->ledgerBoundary.count; ++i) {
        if (openNoteFlags_[i] == 1) bridge_->hasRetainedClosure = false;
        if ((openNoteFlags_[i] & 1) && closureTime_[i] < base_->coverage.loopLengthTicks &&
            closureTime_[i] < bridge_->retainedClosurePhaseLimit)
          bridge_->retainedClosurePhaseLimit = closureTime_[i];
      }
      if (!order_.begin(bridge_->insertions, base_->coverage.loopLengthTicks,
                        bridge_->insertionRanks)) return deny();
      phase_ = Phase::Order;
    }
    if (phase_ == Phase::Order) {
      const auto result = order_.step(quantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
      if (result != RuntimePlaybackOrderBuildResult::Complete) return result;
      if (ranked_) {
        if (!order_.beginIndexes(base_->storage, bridge_->rankedBaseOrder, bridge_->baseOrderScratch,
                                 base_->coverage.loopLengthTicks, bridge_->baseRanks)) return deny();
        phase_ = Phase::RankBaseOrder;
      } else { cursor_ = insertionCursor_ = 0; phase_ = Phase::BaseOrder; }
      return RuntimePlaybackOrderBuildResult::Pending;
    }
    if (phase_ == Phase::RankBaseOrder) {
      const auto result = order_.step(quantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
      if (result != RuntimePlaybackOrderBuildResult::Complete) return result;
      cursor_ = insertionCursor_ = 0; phase_ = Phase::BaseOrder;
      return RuntimePlaybackOrderBuildResult::Pending;
    }
    if (phase_ == Phase::BaseOrder) {
      if (cursor_ < base_->storage.eventCount || insertionCursor_ < bridge_->insertions.eventCount) {
        bool inserted = cursor_ == base_->storage.eventCount;
        if (!inserted && insertionCursor_ < bridge_->insertions.eventCount) {
          const uint16_t left = bridge_->insertions.order[insertionCursor_];
          const uint16_t right = (ranked_ ? bridge_->rankedBaseOrder : base_->storage.order)[cursor_];
          const auto& l = bridge_->insertions.events[left];
          const auto& r = base_->storage.events[right];
          const uint32_t lp = bridge_->insertions.phases[left];
          const uint32_t rp = base_->storage.phases[right];
          inserted = lp != rp ? lp < rp : eventKind(l) != eventKind(r) ? eventKind(l) < eventKind(r) :
                     l.tick != r.tick ? l.tick < r.tick :
                     bridge_->insertionRanks[left] != bridge_->baseRanks[right]
                       ? bridge_->insertionRanks[left] < bridge_->baseRanks[right]
                       : bridge_->insertionProvenance[left] < right;
        }
        const uint16_t index = inserted ? bridge_->insertions.order[insertionCursor_++] : (ranked_ ? bridge_->rankedBaseOrder : base_->storage.order)[cursor_++];
        --quantum;
        const auto& event = inserted ? bridge_->insertions.events[index] : base_->storage.events[index];
        uint8_t mask = inserted ? 15 : bridge_->baseRetained[index];
        uint8_t closureMask = 0;
        if (bridge_->hasRetainedClosure && (event.isNoteOn() || event.isNoteOff())) {
          for (size_t i = 0; i < bridge_->ledgerBoundary.count; ++i) {
            if (!(openNoteFlags_[i] & 1) ||
                !(inserted ? insertionMatches(index, i) : matchesOpenNote(event, bridge_->ledgerBoundary.openNotes[i]))) continue;
            // Ordered traversal pairs each Off with its preceding canonical
            // On, including repeated NoteIds and pairs that cross phase zero.
            if (event.isNoteOn() && (inserted || bridge_->baseRetained[index]))
              newOnPhase_[i] = event.tick % base_->coverage.loopLengthTicks;
            mask &= effectiveMask(event, i);
            if (!inserted && closureIndex_[i] == index)
              closureMask |= 1u << (closureTime_[i] / base_->coverage.loopLengthTicks);
          }
        }
        mask |= closureMask;
        if (inserted) {
          bridge_->insertionCycleMask[index] = mask;
          if (mask & 1) bridge_->currentCycleInsertionOrder[bridge_->currentCycleInsertionCount++] = index;
          if (mask & 2) bridge_->nextCycleInsertionOrder[bridge_->nextCycleInsertionCount++] = index;
          if (mask & 4) bridge_->secondCycleInsertionOrder[bridge_->secondCycleInsertionCount++] = index;
        } else {
          if (mask & 1) bridge_->baseOrder[bridge_->baseEventCount++] = index;
          if (mask & 2) bridge_->nextCycleBaseOrder[bridge_->nextCycleBaseEventCount++] = index;
          if (mask & 4) bridge_->secondCycleBaseOrder[bridge_->secondCycleBaseEventCount++] = index;
          if (mask & 8) bridge_->steadyBaseOrder[bridge_->steadyBaseEventCount++] = index;
        }
        continue;
      }
      mapping_ = nullptr;  // The compiled bridge owns all records and tie ranks.
      bridge_->streamPrepared = bridge_->complete = true;
      phase_ = Phase::Complete;
      return RuntimePlaybackOrderBuildResult::Complete;
    }
  }
  return RuntimePlaybackOrderBuildResult::Pending;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeCursor::selectAfterPhase(const RuntimePlaybackView& base,
    const FixedPlaybackBridgeStorage& bridge, uint32_t cycle, uint32_t completedPhase) {
  if (!base.published || !base.complete || !bridge.complete || !bridge.streamPrepared ||
      !bridge.selected || !(base.source == bridge.baseSource) ||
      bridge.baseEventCount > base.storage.eventCount || bridge.nextCycleBaseEventCount > base.storage.eventCount ||
      bridge.secondCycleBaseEventCount > base.storage.eventCount || bridge.steadyBaseEventCount > base.storage.eventCount ||
      static_cast<int32_t>(cycle - bridge.cursorAnchor.projectionCycle) < 0 ||
      (bridge.protectsOpenNotes && !bridge.hasRetainedClosure) ||
      (cycle == bridge.cursorAnchor.projectionCycle && bridge.protectsOpenNotes &&
       (completedPhase == UINT32_MAX ? bridge.preparedAdmissionPhase != 0 :
        completedPhase >= bridge.retainedClosurePhaseLimit || completedPhase + 1 < bridge.preparedAdmissionPhase)) ||
      (completedPhase != UINT32_MAX && completedPhase >= base.coverage.loopLengthTicks)) return false;
  base_ = &base;
  bridge_ = &bridge;
  const uint32_t age = cycle - bridge.cursorAnchor.projectionCycle;
  baseOrder_ = age == 0 ? bridge.baseOrder : age == 1 ? bridge.nextCycleBaseOrder :
               age == 2 ? bridge.secondCycleBaseOrder : bridge.steadyBaseOrder;
  baseCount_ = age == 0 ? bridge.baseEventCount : age == 1 ? bridge.nextCycleBaseEventCount :
               age == 2 ? bridge.secondCycleBaseEventCount : bridge.steadyBaseEventCount;
  insertionOrder_ = age == 0 ? bridge.currentCycleInsertionOrder : age == 1 ? bridge.nextCycleInsertionOrder :
                    age == 2 ? bridge.secondCycleInsertionOrder : bridge.insertions.order;
  insertionCount_ = age == 0 ? bridge.currentCycleInsertionCount : age == 1 ? bridge.nextCycleInsertionCount :
                    age == 2 ? bridge.secondCycleInsertionCount : bridge.insertions.eventCount;
  baseCursor_ = afterPhase(base.storage, baseOrder_, baseCount_, completedPhase);
  insertionCursor_ = afterPhase(bridge.insertions, insertionOrder_, insertionCount_, completedPhase);
  emitted_ = EmissionCursor{cycle, 0, 0, 0, bridge.source.sourceGeneration};
  hasEmitted_ = false;
  return true;
}

bool RuntimePlaybackBridgeCursor::insertionFirst() const {
  if (baseCursor_ == baseCount_) return true;
  if (insertionCursor_ == insertionCount_) return false;
  const uint16_t baseIndex = baseOrder_[baseCursor_];
  const uint16_t insertionIndex = insertionOrder_[insertionCursor_];
  const auto& left = bridge_->insertions.events[insertionIndex];
  const auto& right = base_->storage.events[baseIndex];
  const uint32_t leftPhase = bridge_->insertions.phases[insertionIndex];
  const uint32_t rightPhase = base_->storage.phases[baseIndex];
  if (leftPhase != rightPhase) return leftPhase < rightPhase;
  if (eventKind(left) != eventKind(right)) return eventKind(left) < eventKind(right);
  if (left.tick != right.tick) return left.tick < right.tick;
  if (bridge_->insertionRanks[insertionIndex] != bridge_->baseRanks[baseIndex])
    return bridge_->insertionRanks[insertionIndex] < bridge_->baseRanks[baseIndex];
  return bridge_->insertionProvenance[insertionIndex] < baseIndex;
}

const MidiEvent* RuntimePlaybackBridgeCursor::peek() const {
  if (bridge_ == nullptr || (baseCursor_ == baseCount_ &&
      insertionCursor_ == insertionCount_)) return nullptr;
  return insertionFirst()
      ? &bridge_->insertions.events[insertionOrder_[insertionCursor_]]
      : &base_->storage.events[baseOrder_[baseCursor_]];
}

bool RuntimePlaybackBridgeCursor::advance() {
  if (peek() == nullptr) return false;
  const bool inserted = insertionFirst();
  const uint16_t index = inserted ? insertionOrder_[insertionCursor_++] : baseOrder_[baseCursor_++];
  const uint32_t phase = inserted ? bridge_->insertions.phases[index] : base_->storage.phases[index];
  emitted_.samePhaseOrdinal = hasEmitted_ && emitted_.phase == phase
                                 ? emitted_.samePhaseOrdinal + 1 : 0;
  emitted_.phase = phase;
  emitted_.eventProvenance = inserted ? bridge_->insertionProvenance[index] : index;
  hasEmitted_ = true;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeStream::selectAfterPhase(const RuntimePlaybackView& base,
    const FixedPlaybackBridgeStorage& bridge, uint32_t cycle, uint32_t completedPhase) {
  if (!cursor.selectAfterPhase(base, bridge, cycle, completedPhase)) return false;
  position = 0;
  playbackIndex = 0;
  count = cursor.remaining();
  base_ = &base;
  bridge_ = &bridge;
  baseSource_ = base.source;
  bridgeSource_ = bridge.source;
  cycle_ = cycle;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeStream::matches(const RuntimePlaybackView& base,
    const FixedPlaybackBridgeStorage& bridge, uint32_t cycle) const {
  return base_ == &base && bridge_ == &bridge && cycle_ == cycle &&
         baseSource_ == base.source && bridgeSource_ == bridge.source && bridge.selected;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM void RuntimePlaybackBridgeStream::reset() {
  *this = RuntimePlaybackBridgeStream{};
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM PlaybackEventStream RuntimePlaybackBridgeStream::stream() {
  return PlaybackEventStream{
      this,
      [](const void* ctx) { return static_cast<const RuntimePlaybackBridgeStream*>(ctx)->count; },
      [](const void* ctx, uint16_t index) {
        const auto& stream = *static_cast<const RuntimePlaybackBridgeStream*>(ctx);
        return stream.position == index && stream.cursor.peek() != nullptr;
      },
      [](const void* ctx, uint16_t) -> const MidiEvent& {
        return *static_cast<const RuntimePlaybackBridgeStream*>(ctx)->cursor.peek();
      },
      [](const MidiEvent& event, const ProjectionContext& projection) {
        return IntervalProjection::playbackEventPhase(event.tick, projection.loopLength);
      },
      [](const void* ctx) {
        auto& stream = *const_cast<RuntimePlaybackBridgeStream*>(
            static_cast<const RuntimePlaybackBridgeStream*>(ctx));
        if (stream.cursor.advance()) ++stream.position;
      }};
}
