// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
//
// Read side of the prepared playback bridge: the two-way merge cursor and the
// stream adapter that exposes it to the existing emit/ledger policy. The compile
// side (capture fill and ranked build) stays in `RuntimePlaybackBridge.cpp`.
// Both TUs are off the linker flash allow-list, so placement is per function and
// unchanged by this split: `selectAfterPhase`/`matches`/`reset`/`stream`/
// `source`/`acknowledgeThroughPhase` keep their cold marker, while
// `insertionFirst`/`peek`/`advance` stay unmarked in the ITCM default.

#include "RuntimePlaybackBridge.h"
#include "Utils/NoteEditMem.h"
#include "Utils/PlaybackEventOrder.h"

#include <algorithm>

// Placement alias: the subsystem's cold code already shares `NOTE_EDIT_MEM` (see the sibling
// companion TUs), so this name documents the local rationale without redefining placement.
#define PLAYBACK_BRIDGE_MAINTENANCE_MEM NOTE_EDIT_MEM

namespace {
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

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeCursor::selectAfterPhase(const RuntimePlaybackView& base,
    const FixedPlaybackBridgeStorage& bridge, uint32_t cycle, uint32_t completedPhase) {
  if (!base.published || !base.complete || !bridge.complete || !bridge.streamPrepared ||
      !bridge.selected || !(base.source == bridge.baseSource) ||
      base.storageGeneration != bridge.baseStorageGeneration ||
      bridge.baseEventCount > base.storage.eventCount || bridge.nextCycleBaseEventCount > base.storage.eventCount ||
      bridge.secondCycleBaseEventCount > base.storage.eventCount || bridge.steadyBaseEventCount > base.storage.eventCount ||
      static_cast<int32_t>(cycle - bridge.cursorAnchor.projectionCycle) < 0 ||
      (bridge.protectsOpenNotes && !bridge.hasRetainedClosure) ||
      (cycle == bridge.cursorAnchor.projectionCycle && bridge.protectsOpenNotes &&
       (completedPhase == UINT32_MAX ? bridge.preparedAdmissionPhase != 0 :
        completedPhase >= bridge.retainedClosurePhaseLimit || completedPhase + 1 < bridge.preparedAdmissionPhase)) ||
      (completedPhase != UINT32_MAX && completedPhase >= base.coverage.loopLengthTicks)) return false;
  base_ = &base;
  baseStorageGeneration_ = base.storageGeneration;
  bridge_ = &bridge;
  bridgeStorageGeneration_ = bridge.storageGeneration;
  const uint32_t age = cycle - bridge.cursorAnchor.projectionCycle;
  baseOrder_ = age == 0 ? bridge.baseOrder : age == 1 ? bridge.nextCycleBaseOrder :
               age == 2 ? bridge.secondCycleBaseOrder : bridge.steadyBaseOrder;
  baseCount_ = age == 0 ? bridge.baseEventCount : age == 1 ? bridge.nextCycleBaseEventCount :
               age == 2 ? bridge.secondCycleBaseEventCount : bridge.steadyBaseEventCount;
  insertionOrder_ = age == 0 ? bridge.currentCycleInsertionOrder : age == 1 ? bridge.nextCycleInsertionOrder :
                    age == 2 ? bridge.secondCycleInsertionOrder : bridge.insertions.order;
  insertionCount_ = age == 0 ? bridge.currentCycleInsertionCount : age == 1 ? bridge.nextCycleInsertionCount :
                    age == 2 ? bridge.secondCycleInsertionCount : bridge.steadyInsertionCount;
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
  return PlaybackEventOrder::mergeKeyBefore(
      PlaybackEventOrder::mergeKey(leftPhase, left.tick, bridge_->insertionRanks[insertionIndex],
                                   bridge_->insertionProvenance[insertionIndex], left),
      PlaybackEventOrder::mergeKey(rightPhase, right.tick, bridge_->baseRanks[baseIndex], baseIndex,
                                   right));
}

const MidiEvent* RuntimePlaybackBridgeCursor::peek() const {
  if (bridge_ == nullptr || base_ == nullptr || !base_->published || !base_->complete ||
      !bridge_->selected || !bridge_->complete || bridge_->storageGeneration != bridgeStorageGeneration_ ||
      base_->storageGeneration != baseStorageGeneration_ ||
      baseStorageGeneration_ != bridge_->baseStorageGeneration ||
      (baseCursor_ == baseCount_ &&
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
  ++sourceGeneration_;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeStream::matches(const RuntimePlaybackView& base,
    const FixedPlaybackBridgeStorage& bridge, uint32_t cycle) const {
  return base_ == &base && bridge_ == &bridge && cycle_ == cycle &&
         baseSource_ == base.source && bridgeSource_ == bridge.source && bridge.selected &&
         base.published && base.complete && bridge.complete && cursor.matchesBaseStorage(base) &&
         cursor.matchesBridgeStorage(bridge) &&
         base.storageGeneration == bridge.baseStorageGeneration;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM void RuntimePlaybackBridgeStream::reset() {
  const auto next = sourceGeneration_ + 1;
  *this = RuntimePlaybackBridgeStream{};
  sourceGeneration_ = next;
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

PLAYBACK_BRIDGE_MAINTENANCE_MEM RuntimePlaybackViewSource RuntimePlaybackBridgeStream::source() {
  RuntimePlaybackViewSource out;
  out.context = this; out.count = count; out.generationToken = sourceGeneration_;
  out.size = [](const void* p) { return static_cast<const RuntimePlaybackBridgeStream*>(p)->count; };
  out.generation = [](const void* p) {
    const auto& owner = *static_cast<const RuntimePlaybackBridgeStream*>(p);
    return owner.base_ && owner.bridge_ && owner.matches(*owner.base_, *owner.bridge_, owner.cycle_)
        ? owner.sourceGeneration_ : uint64_t(0);
  };
  out.eventAt = [](const void* p, size_t) -> const MidiEvent& {
    auto& owner = *const_cast<RuntimePlaybackBridgeStream*>(static_cast<const RuntimePlaybackBridgeStream*>(p));
    const auto* event = owner.cursor.peek();
    if (owner.cursor.advance()) ++owner.position;
    return *event;
  };
  return out;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM void RuntimePlaybackBridgeCursor::acknowledgeThroughPhase(uint32_t phase) {
  if (!base_ || !bridge_) return;
  baseCursor_ = std::max(baseCursor_, afterPhase(base_->storage, baseOrder_, baseCount_, phase));
  insertionCursor_ = std::max(insertionCursor_, afterPhase(bridge_->insertions, insertionOrder_, insertionCount_, phase));
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM void RuntimePlaybackBridgeStream::acknowledgeThroughPhase(uint32_t phase) {
  cursor.acknowledgeThroughPhase(phase);
  position = playbackIndex = count - cursor.remaining();
}
