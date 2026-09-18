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
  predecessor_ = nullptr; predecessorGeneration_ = bridgeStorageGeneration_ = 0;
  initialWindow_ = false; inheritMasks_ = true; predecessorSource_ = {}; rebinding_ = false;
  mapping_ = nullptr; mappingGeneration_ = 0; mappedRecordsCopied_ = 0; ranked_ = false;
  order_.discard();
  phase_ = Phase::Idle;
  cursor_ = recordCount_ = insertionCursor_ = 0;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM uint64_t RuntimePlaybackBridgeBuild::requiredRankedGrainsUpperBound(
    size_t baseCount, size_t recordCount, size_t quantum) {
  if (!quantum || baseCount > RuntimePlaybackViewConfig::kEventsPerView ||
      recordCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return UINT64_MAX;
  const uint64_t linear = 3*uint64_t(baseCount) + 3*uint64_t(recordCount) +
      (RuntimePlaybackViewConfig::kEventsPerView - baseCount); // Mask rows beyond the existing base-clear walk.
  return (linear ? 1 + (linear-1)/quantum : 0) +
      RuntimePlaybackOrderBuilder::requiredGrains(baseCount, quantum) +
      RuntimePlaybackOrderBuilder::requiredGrains(recordCount, quantum) + 6;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM uint64_t RuntimePlaybackBridgeBuild::requiredRetainedClosureGrains(
    size_t ledgerCount, size_t quantum) {
  if (!quantum || ledgerCount > ActiveNoteLedger::kMaxOpenNotes) return UINT64_MAX;
  // Each copied Off adds inheritance/copy, closure and output visits. It can
  // also cross a merge-sort width boundary. Overbound that delta at the fixed
  // power-of-two insertion horizon, even though an overflowing build denies.
  constexpr size_t horizon = RuntimePlaybackViewConfig::kRecordsPerBridge;
  static_assert((horizon & (horizon - 1)) == 0, "sort delta assumes a power-of-two horizon");
  return 4 * (ledgerCount ? 1 + (ledgerCount - 1) / quantum : 0) + 3 +
      RuntimePlaybackOrderBuilder::requiredGrains(horizon + ledgerCount, quantum) -
      RuntimePlaybackOrderBuilder::requiredGrains(horizon, quantum);
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::begin(const RuntimePlaybackView& base,
                                       FixedPlaybackBridgeStorage& bridge,
                                       const RuntimeCompanionBridgeMap* mapping,
                                       const FixedPlaybackBridgeStorage* retained) {
  discard();
  if (!base.published || !base.complete || !base.coverage.isValid() ||
      bridge.selected || bridge.complete || !bridge.coverage.isValid() ||
      bridge.ledgerBoundary.overflowed || bridge.ledgerBoundary.count > bridge.heldClosures.size() ||
      bridge.coverage.loopLengthTicks != base.coverage.loopLengthTicks ||
      base.storage.eventCount > bridge.baseCapacity || bridge.records == nullptr ||
      bridge.baseOrder == nullptr || bridge.baseRetained == nullptr || bridge.insertionProvenance == nullptr ||
      bridge.baseRanks == nullptr || bridge.insertionRanks == nullptr || bridge.canonicalCycleMasks == nullptr ||
      bridge.rankedBaseOrder == nullptr || bridge.baseOrderScratch == nullptr ||
      bridge.nextCycleBaseOrder == nullptr || bridge.currentCycleInsertionOrder == nullptr ||
      bridge.secondCycleBaseOrder == nullptr || bridge.steadyBaseOrder == nullptr ||
      bridge.nextCycleInsertionOrder == nullptr || bridge.secondCycleInsertionOrder == nullptr ||
      bridge.cursorAnchor.phase >= bridge.coverage.loopLengthTicks || bridge.insertionCycleMask == nullptr ||
      !bridge.insertions.isPrewarmed() || bridge.recordCount > bridge.capacity) return false;
  // A partial window does not yet own closures beyond its coverage. Preserve
  // the existing held-note guard until those obligations span window rotation.
  if (mapping != nullptr && ((base.coverage.lengthTicks != base.coverage.loopLengthTicks &&
      bridge.ledgerBoundary.count != 0) ||
      bridge.coverage.startPhase != base.coverage.startPhase ||
      bridge.coverage.lengthTicks != base.coverage.lengthTicks ||
      bridge.recordCount != 0 || mapping->size() > bridge.capacity || mapping->candidateCount() > bridge.baseCapacity ||
      !mapping->matchesBase(base.storage.events, base.storage.eventCount, base.source))) return false;
  if (retained && (!mapping || retained == &bridge || !retained->selected ||
      !retained->complete || !retained->streamPrepared || !retained->hasRetainedClosure ||
      !(retained->source == base.source) ||
      retained->coverage.loopLengthTicks != base.coverage.loopLengthTicks ||
      static_cast<int32_t>(bridge.cursorAnchor.projectionCycle - retained->cursorAnchor.projectionCycle) < 0)) return false;
  mapping_ = mapping; ranked_ = mapping != nullptr;
  mappingGeneration_ = mapping != nullptr ? mapping->generation() : 0;
  base_ = &base;
  bridge_ = &bridge;
  bridgeStorageGeneration_ = bridge.storageGeneration;
  bridge.baseSource = base.source;
  bridge.baseStorageGeneration = base.storageGeneration;
  source_ = bridge.source;
  recordCount_ = mapping != nullptr ? mapping->size() : bridge.recordCount;
  bridge.canonicalRankCount = mapping ? mapping->candidateCount() : 0;
  bridge.canonicalMasksReady = false;
  bridge.affectedPitches.fill(0); bridge.affectedIdentities.fill(0);
  bridge.heldClosureCount = bridge.ledgerBoundary.count;
  for (size_t i = 0; i < bridge.heldClosureCount; ++i) {
    auto& closure = bridge.heldClosures[i];
    closure.flags = 0;
    closure.relativeTick = UINT64_MAX;
    closure.nextOnPhase = UINT32_MAX;
  }
  closureIndex_.fill(UINT16_MAX);
  bridge.baseEventCount = 0;
  bridge.nextCycleBaseEventCount = 0;
  bridge.secondCycleBaseEventCount = bridge.steadyBaseEventCount = 0;
  bridge.currentCycleInsertionCount = bridge.nextCycleInsertionCount = bridge.secondCycleInsertionCount = bridge.steadyInsertionCount = 0;
  bridge.protectsOpenNotes = false;
  bridge.hasRetainedClosure = false;
  bridge.retainedClosurePhaseLimit = UINT32_MAX;
  bridge.preparedAdmissionPhase = bridge.cursorAnchor.phase;
  bridge.insertions.clear();
  bridge.streamPrepared = false;
  phase_ = Phase::Clear;
  if (retained) {
    predecessor_ = retained; predecessorGeneration_ = retained->storageGeneration;
    predecessorSource_ = retained->source; inheritMasks_ = false;
    phase_ = Phase::Inherit;
  }
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM uint64_t RuntimePlaybackBridgeBuild::requiredBoundaryGrains(
    size_t baseCount, size_t insertionCount, size_t recordCount, size_t ledgerCount, size_t quantum) {
  if (!quantum || ledgerCount > ActiveNoteLedger::kMaxOpenNotes ||
      baseCount > RuntimePlaybackViewConfig::kEventsPerView ||
      insertionCount > RuntimePlaybackViewConfig::kRecordsPerBridge ||
      recordCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return UINT64_MAX;
  const auto turns = [quantum](uint64_t count) { return count ? 1 + (count-1)/quantum : 0; };
  return turns(ledgerCount) + turns(recordCount) + 2*turns(uint64_t(baseCount)+insertionCount) + 4;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::rebindBoundary(
    const RuntimePlaybackView& base, FixedPlaybackBridgeStorage& bridge,
    const RuntimeCompanionBridgeMap& mapping, const ActiveNoteLedger& ledger,
    const EmissionCursor& anchor, const FixedPlaybackBridgeStorage* retained) {
  if (phase_ != Phase::Complete || !ranked_ || base_ != &base || bridge_ != &bridge ||
      mappingGeneration_ != mapping.generation() ||
      !base.published || !base.complete || !bridge.complete || !bridge.streamPrepared ||
      bridge.selected || !bridge.canonicalMasksReady || ledger.overflowed() || !base.coverage.isValid() ||
      base.coverage.lengthTicks != base.coverage.loopLengthTicks ||
      bridge.baseStorageGeneration != base.storageGeneration || !(bridge.baseSource == base.source) ||
      !mapping.matchesBase(base.storage.events, base.storage.eventCount, base.source) ||
      mapping.size() != bridge.recordCount || mapping.candidateCount() != bridge.canonicalRankCount ||
      anchor.phase >= base.coverage.loopLengthTicks ||
      anchor.effectiveSourceGeneration != bridge.source.sourceGeneration ||
      (retained && (retained == &bridge || !retained->selected || !retained->complete ||
       !retained->streamPrepared || !(retained->source == base.source) ||
       static_cast<int32_t>(anchor.projectionCycle-retained->cursorAnchor.projectionCycle) < 0))) return false;
  discard();
  base_ = &base; bridge_ = &bridge; source_ = bridge.source;
  bridgeStorageGeneration_ = bridge.storageGeneration;
  mapping_ = &mapping; mappingGeneration_ = mapping.generation();
  mappedRecordsCopied_ = recordCount_ = bridge.recordCount;
  ranked_ = rebinding_ = true; inheritMasks_ = false;
  predecessor_ = retained;
  if (retained) {
    predecessorGeneration_ = retained->storageGeneration; predecessorSource_ = retained->source;
  }
  bridge.ledgerBoundary.capture(ledger); bridge.cursorAnchor = anchor;
  bridge.heldClosureCount = bridge.ledgerBoundary.count;
  for (size_t i=0; i<bridge.heldClosureCount; ++i) bridge.heldClosures[i] = {};
  closureIndex_.fill(UINT16_MAX);
  bridge.baseEventCount = bridge.nextCycleBaseEventCount = 0;
  bridge.secondCycleBaseEventCount = bridge.steadyBaseEventCount = 0;
  bridge.currentCycleInsertionCount = bridge.nextCycleInsertionCount = 0;
  bridge.secondCycleInsertionCount = bridge.steadyInsertionCount = 0;
  bridge.protectsOpenNotes = bridge.hasRetainedClosure = false;
  bridge.retainedClosurePhaseLimit = UINT32_MAX;
  bridge.preparedAdmissionPhase = anchor.phase;
  bridge.complete = bridge.streamPrepared = bridge.canonicalMasksReady = false;
  phase_ = retained ? Phase::Inherit : Phase::Records;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM uint64_t RuntimePlaybackBridgeBuild::requiredSuccessorGrainsUpperBound(
    size_t baseCount, size_t ledgerCount, size_t quantum) {
  if (!quantum || ledgerCount > ActiveNoteLedger::kMaxOpenNotes) return UINT64_MAX;
  const auto compile = requiredRankedGrainsUpperBound(baseCount, ledgerCount, quantum);
  if (compile == UINT64_MAX) return compile;
  // Treat every ledger entry as a possible copied Off; include inheritance and
  // copy walks plus their phase transitions. This also overbounds the unused
  // ranked-base sort, keeping the existing compiler's order estimate reusable.
  return compile + 2 * (ledgerCount ? 1 + (ledgerCount - 1) / quantum : 0) + 3;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::beginSuccessor(
    const RuntimePlaybackView& base, FixedPlaybackBridgeStorage& bridge,
    const FixedPlaybackBridgeStorage& predecessor) {
  if (&bridge == &predecessor || !predecessor.selected || !predecessor.complete ||
      !predecessor.streamPrepared || !predecessor.hasRetainedClosure || !predecessor.canonicalMasksReady ||
      predecessor.canonicalRankCount > bridge.baseCapacity || !base.canonicalRanksReady || !base.canonicalRanks ||
      !predecessor.coverage.isValid() || !base.coverage.isValid() ||
      !(predecessor.source == base.source) || !(bridge.source == base.source) || bridge.recordCount ||
      predecessor.coverage.loopLengthTicks != base.coverage.loopLengthTicks ||
      bridge.coverage.startPhase != base.coverage.startPhase ||
      bridge.coverage.lengthTicks != base.coverage.lengthTicks ||
      bridge.cursorAnchor.phase != base.coverage.startPhase ||
      predecessor.heldClosureCount != predecessor.ledgerBoundary.count) return false;
  const uint64_t end = uint64_t(predecessor.coverage.startPhase) + predecessor.coverage.lengthTicks;
  const uint32_t length = base.coverage.loopLengthTicks;
  if (base.coverage.startPhase != end % length ||
      bridge.cursorAnchor.projectionCycle != uint32_t(predecessor.cursorAnchor.projectionCycle + end / length))
    return false;
  if (!begin(base, bridge)) return false;
  bridge.canonicalRankCount = predecessor.canonicalRankCount;
  predecessor_ = &predecessor;
  predecessorGeneration_ = predecessor.storageGeneration;
  predecessorSource_ = predecessor.source;
  phase_ = Phase::Inherit;
  return true;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM bool RuntimePlaybackBridgeBuild::beginInitialWindow(
    const RuntimePlaybackView& base, FixedPlaybackBridgeStorage& bridge,
    const FixedPlaybackBridgeStorage& context) {
  if (&bridge == &context || !context.complete || !context.streamPrepared ||
      !context.hasRetainedClosure || !context.canonicalMasksReady ||
      !base.canonicalRanksReady || !base.canonicalRanks || !base.coverage.isValid() ||
      !context.coverage.isValid() || context.coverage.startPhase != 0 ||
      context.coverage.lengthTicks != context.coverage.loopLengthTicks ||
      context.coverage.loopLengthTicks != base.coverage.loopLengthTicks ||
      !(context.source == base.source) || !(bridge.source == base.source) ||
      bridge.recordCount || context.canonicalRankCount > bridge.baseCapacity ||
      bridge.coverage.startPhase != base.coverage.startPhase ||
      bridge.coverage.lengthTicks != base.coverage.lengthTicks ||
      bridge.cursorAnchor.projectionCycle != context.cursorAnchor.projectionCycle ||
      bridge.cursorAnchor.phase != context.cursorAnchor.phase ||
      bridge.cursorAnchor.effectiveSourceGeneration != base.source.sourceGeneration ||
      !base.coverage.contains(bridge.cursorAnchor.phase) ||
      context.ledgerBoundary.overflowed ||
      context.ledgerBoundary.count > context.heldClosures.size() ||
      context.heldClosureCount != context.ledgerBoundary.count ||
      bridge.ledgerBoundary.count != context.ledgerBoundary.count) return false;
  // A different ledger needs a new closure proof, not a projection of this one.
  for (size_t i = 0; i < bridge.ledgerBoundary.count; ++i) {
    const auto& a = bridge.ledgerBoundary.openNotes[i];
    const auto& b = context.ledgerBoundary.openNotes[i];
    if (!activeNoteEntriesMatch(a, b)) return false;
  }
  if (!begin(base, bridge)) return false;
  bridge.canonicalRankCount = context.canonicalRankCount;
  predecessor_ = &context;
  predecessorGeneration_ = context.storageGeneration;
  predecessorSource_ = context.source;
  initialWindow_ = true;
  phase_ = Phase::Inherit;
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
      if (mark) bridge_->heldClosures[i].flags |= PlaybackHeldNoteClosure::kAffected;
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
  uint64_t onTime = event.isNoteOn() ? phase : bridge_->heldClosures[openIndex].nextOnPhase;
  if (onTime == UINT32_MAX) onTime = phase;
  if (onTime < bridge_->heldClosures[openIndex].relativeTick)
    onTime += ((bridge_->heldClosures[openIndex].relativeTick - onTime + length - 1) / length) * uint64_t(length);
  uint64_t time = onTime;
  if (event.isNoteOff()) {
    const uint32_t onPhase = static_cast<uint32_t>(onTime % length);
    time = onTime - onPhase + phase + (phase <= onPhase ? uint64_t(length) : 0);
  }
  return static_cast<uint8_t>(0x0f << (time / length)) & 0x0f;
}

PLAYBACK_BRIDGE_MAINTENANCE_MEM RuntimePlaybackOrderBuildResult RuntimePlaybackBridgeBuild::step(size_t quantum) {
  if (quantum == 0 || base_ == nullptr || bridge_ == nullptr ||
      phase_ == Phase::Denied || bridge_->storageGeneration != bridgeStorageGeneration_ ||
      bridge_->selected || !base_->published || !base_->complete ||
      base_->storageGeneration != bridge_->baseStorageGeneration ||
      bridge_->heldClosureCount != bridge_->ledgerBoundary.count ||
      !(base_->source == bridge_->baseSource) || !(bridge_->source == source_) ||
      ((mapping_ == nullptr ? recordCount_ : mappedRecordsCopied_) != bridge_->recordCount)) return RuntimePlaybackOrderBuildResult::Denied;
  if (mapping_ != nullptr && (mapping_->generation() != mappingGeneration_ ||
      !mapping_->matchesBase(base_->storage.events, base_->storage.eventCount, base_->source))) {
    phase_ = Phase::Denied; return RuntimePlaybackOrderBuildResult::Denied;
  }
  if (predecessor_ && (predecessor_->storageGeneration != predecessorGeneration_ ||
      (!initialWindow_ && !predecessor_->selected) || !predecessor_->complete || !(predecessor_->source == predecessorSource_) ||
      (inheritMasks_ && (!predecessor_->canonicalMasksReady ||
       predecessor_->canonicalRankCount != bridge_->canonicalRankCount ||
       !base_->canonicalRanksReady || !base_->canonicalRanks))))
    return RuntimePlaybackOrderBuildResult::Denied;
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
    if (phase_ == Phase::Inherit) {
      if (cursor_ < bridge_->heldClosureCount) {
        const size_t index = cursor_++;
        --quantum;
        const auto& open = bridge_->ledgerBoundary.openNotes[index];
        const uint64_t elapsed = uint64_t(bridge_->cursorAnchor.projectionCycle -
            predecessor_->cursorAnchor.projectionCycle) * base_->coverage.loopLengthTicks;
        for (size_t i = 0; i < predecessor_->heldClosureCount; ++i) {
          const auto* previous = predecessor_->heldClosure(i);
          const auto& old = predecessor_->ledgerBoundary.openNotes[i];
          if (!previous || !activeNoteEntriesMatch(open, old) ||
              previous->relativeTick < elapsed + bridge_->cursorAnchor.phase) continue;
          auto& closure = bridge_->heldClosures[index];
          closure = *previous;
          closure.relativeTick -= elapsed;
          closure.flags |= PlaybackHeldNoteClosure::kInherited;
          bridge_->markAffected(closure.originalOff);
          bridge_->protectsOpenNotes = true;
          break;
        }
        continue;
      }
      cursor_ = 0;
      phase_ = rebinding_ ? Phase::Records : Phase::Clear;
      return RuntimePlaybackOrderBuildResult::Pending;
    }
    if (phase_ == Phase::Clear) {
      if (cursor_ < base_->storage.eventCount || cursor_ < bridge_->canonicalRankCount) {
        const size_t index = cursor_++;
        if (index < bridge_->canonicalRankCount) {
          uint8_t mask = PlaybackBridgeCycleMask::kAll;
          if (predecessor_ && inheritMasks_) {
            mask = predecessor_->canonicalCycleMasks[index];
            const uint32_t age = bridge_->cursorAnchor.projectionCycle - predecessor_->cursorAnchor.projectionCycle;
            mask = age >= 3 ? ((mask & PlaybackBridgeCycleMask::kSteady) ? PlaybackBridgeCycleMask::kAll : 0)
                : uint8_t((mask >> age) | ((mask & PlaybackBridgeCycleMask::kSteady) ? (PlaybackBridgeCycleMask::kAll << (4-age)) : 0)) & PlaybackBridgeCycleMask::kAll;
          }
          bridge_->canonicalCycleMasks[index] = mask;
        }
        if (index < base_->storage.eventCount) {
          const uint32_t rank = mapping_ ? mapping_->baseRanks()[index]
              : predecessor_ ? base_->canonicalRanks[index] : index;
          if (predecessor_ && inheritMasks_ && rank >= bridge_->canonicalRankCount) return deny();
          bridge_->baseRanks[index] = rank == UINT16_MAX ? index : rank;
          bridge_->baseRetained[index] = 15;
        }
        --quantum;
        continue;
      }
      cursor_ = 0;
      phase_ = Phase::Records;
    }
    if (phase_ == Phase::Records) {
      if (cursor_ < recordCount_) {
        const size_t recordIndex = cursor_++;
        if (!rebinding_ && mapping_ != nullptr && (bridge_->recordCount != recordIndex ||
            !bridge_->append(mapping_->records()[recordIndex]))) return deny();
        if (!rebinding_ && mapping_ != nullptr) ++mappedRecordsCopied_;
        const PlaybackBridgeRecord& record = bridge_->records[recordIndex];
        bridge_->markAffected(record.event);
        --quantum;
        if (record.operation != PlaybackBridgeOperation::Insert &&
            record.operation != PlaybackBridgeOperation::Suppress &&
            record.operation != PlaybackBridgeOperation::Replace) return deny();
        if (record.operation != PlaybackBridgeOperation::Insert) {
          const size_t index = record.eventProvenance;
          if (index >= base_->storage.eventCount || (!rebinding_ && bridge_->baseRetained[index] == 0)) return deny();
          bridge_->markAffected(base_->storage.events[index]);
          bridge_->protectsOpenNotes = touchesOpenNote(base_->storage.events[index], true) ||
                                      bridge_->protectsOpenNotes;
          bridge_->baseRetained[index] = 0;
        }
        if (record.operation != PlaybackBridgeOperation::Suppress) {
          if (rebinding_) {
            bridge_->protectsOpenNotes = touchesOpenNote(record.event, true) || bridge_->protectsOpenNotes;
            continue;
          }
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
      phase_ = predecessor_ && !rebinding_ ? Phase::CarryOffs : Phase::Closures;
    }
    if (phase_ == Phase::CarryOffs) {
      if (cursor_ < bridge_->heldClosureCount) {
        const size_t i = cursor_++;
        --quantum;
        const auto& closure = bridge_->heldClosures[i];
        if (!(closure.flags & PlaybackHeldNoteClosure::kInherited) || !base_->coverage.contains(closure.originalOff.tick % base_->coverage.loopLengthTicks))
          continue;
        const size_t index = bridge_->insertions.eventCount;
        if (!bridge_->insertions.append(closure.originalOff)) return deny();
        bridge_->insertionProvenance[index] = bridge_->baseCapacity + recordCount_ + i;
        bridge_->insertionRanks[index] = closure.rank;
        bridge_->insertionCycleMask[index] = 0;
        closureIndex_[i] = bridge_->baseCapacity + index;
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
          if (!(bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kAffected) ||
              !(inserted ? insertionMatches(index, i) : matchesOpenNote(event, bridge_->ledgerBoundary.openNotes[i])))
            continue;
          if (!inserted && event.isNoteOff() && !(bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kInherited)) {
            const uint64_t time = uint64_t(phase) + (phase < bridge_->preparedAdmissionPhase ? base_->coverage.loopLengthTicks : 0);
            if (time < bridge_->heldClosures[i].relativeTick) {
              bridge_->heldClosures[i].relativeTick = time;
              bridge_->heldClosures[i].originalOff = event;
              bridge_->heldClosures[i].rank = bridge_->baseRanks[index];
              closureIndex_[i] = static_cast<uint16_t>(index);
            }
            bridge_->heldClosures[i].flags |= PlaybackHeldNoteClosure::kOriginalOffFound;
          }
          if (rebinding_ && inserted && (bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kInherited) &&
              bridge_->insertionProvenance[index] >= bridge_->baseCapacity + recordCount_ &&
              event.tick == bridge_->heldClosures[i].originalOff.tick &&
              event.noteId == bridge_->heldClosures[i].originalOff.noteId) {
            closureIndex_[i] = static_cast<uint16_t>(bridge_->baseCapacity + index);
          }
          if (event.isNoteOn() && (inserted || bridge_->baseRetained[index])) {
            if (bridge_->heldClosures[i].nextOnPhase == UINT32_MAX || phase > bridge_->heldClosures[i].nextOnPhase) bridge_->heldClosures[i].nextOnPhase = phase;
          }
        }
        continue;
      }
      bridge_->hasRetainedClosure = true;
      for (size_t i = 0; i < bridge_->ledgerBoundary.count; ++i) {
        if (bridge_->heldClosures[i].flags == PlaybackHeldNoteClosure::kAffected) bridge_->hasRetainedClosure = false;
        if (rebinding_ && (bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kInherited) && closureIndex_[i] == UINT16_MAX)
          bridge_->hasRetainedClosure = false;
        if ((bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kAffected) && bridge_->heldClosures[i].relativeTick < base_->coverage.loopLengthTicks &&
            bridge_->heldClosures[i].relativeTick < bridge_->retainedClosurePhaseLimit)
          bridge_->retainedClosurePhaseLimit = bridge_->heldClosures[i].relativeTick;
      }
      if (rebinding_) {
        // Both complete sorted orders survive reconciliation in owned storage.
        cursor_ = insertionCursor_ = 0; phase_ = Phase::BaseOrder;
        return RuntimePlaybackOrderBuildResult::Pending;
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
          const uint16_t left = (rebinding_ ? bridge_->insertions.mergeScratch : bridge_->insertions.order)[insertionCursor_];
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
        const uint16_t index = inserted ? (rebinding_ ? bridge_->insertions.mergeScratch : bridge_->insertions.order)[insertionCursor_++] : (ranked_ ? bridge_->rankedBaseOrder : base_->storage.order)[cursor_++];
        if (inserted && !rebinding_) bridge_->insertions.mergeScratch[insertionCursor_-1] = index;
        --quantum;
        const auto& event = inserted ? bridge_->insertions.events[index] : base_->storage.events[index];
        uint8_t mask = inserted ? PlaybackBridgeCycleMask::kAll : bridge_->baseRetained[index];
        if (inserted && bridge_->insertionProvenance[index] >= bridge_->baseCapacity + recordCount_)
          mask = 0; // Copied original Offs are enabled only by their live closure.
        if (predecessor_ && inheritMasks_ && !inserted) mask &= bridge_->canonicalCycleMasks[base_->canonicalRanks[index]];
        uint8_t closureMask = 0;
        if (bridge_->hasRetainedClosure && (!predecessor_ || !inheritMasks_ || inserted) && (event.isNoteOn() || event.isNoteOff())) {
          for (size_t i = 0; i < bridge_->ledgerBoundary.count; ++i) {
            if (!(bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kAffected) ||
                !(inserted ? insertionMatches(index, i) : matchesOpenNote(event, bridge_->ledgerBoundary.openNotes[i]))) continue;
            // Ordered traversal pairs each Off with its preceding canonical
            // On, including repeated NoteIds and pairs that cross phase zero.
            if (event.isNoteOn() && (inserted || bridge_->baseRetained[index]))
              bridge_->heldClosures[i].nextOnPhase = event.tick % base_->coverage.loopLengthTicks;
            mask &= effectiveMask(event, i);
            if (closureIndex_[i] == (inserted ? bridge_->baseCapacity + index : index)) {
              closureMask |= 1u << (bridge_->heldClosures[i].relativeTick / base_->coverage.loopLengthTicks);
              if (inserted && (bridge_->heldClosures[i].flags & PlaybackHeldNoteClosure::kInherited)) mask = 0;
            }
          }
        }
        mask |= closureMask;
        if (mapping_) {
          const auto rank = inserted
              ? (bridge_->insertionProvenance[index] >= bridge_->baseCapacity + recordCount_
                  ? UINT16_MAX : bridge_->insertionRanks[index])
              : bridge_->baseRetained[index] ? mapping_->baseRanks()[index] : UINT16_MAX;
          if (rank != UINT16_MAX && rank < bridge_->canonicalRankCount) bridge_->canonicalCycleMasks[rank] = mask;
        }
        if (inserted) {
          bridge_->insertionCycleMask[index] = mask;
          if (mask & PlaybackBridgeCycleMask::kCurrent) bridge_->currentCycleInsertionOrder[bridge_->currentCycleInsertionCount++] = index;
          if (mask & PlaybackBridgeCycleMask::kNext) bridge_->nextCycleInsertionOrder[bridge_->nextCycleInsertionCount++] = index;
          if (mask & PlaybackBridgeCycleMask::kSecond) bridge_->secondCycleInsertionOrder[bridge_->secondCycleInsertionCount++] = index;
          // Compact only already-read order slots. Closure-only inserts must
          // disappear from steady playback rather than repeat each loop.
          if (mask & PlaybackBridgeCycleMask::kSteady) bridge_->insertions.order[bridge_->steadyInsertionCount++] = index;
        } else {
          if (mask & PlaybackBridgeCycleMask::kCurrent) bridge_->baseOrder[bridge_->baseEventCount++] = index;
          if (mask & PlaybackBridgeCycleMask::kNext) bridge_->nextCycleBaseOrder[bridge_->nextCycleBaseEventCount++] = index;
          if (mask & PlaybackBridgeCycleMask::kSecond) bridge_->secondCycleBaseOrder[bridge_->secondCycleBaseEventCount++] = index;
          if (mask & PlaybackBridgeCycleMask::kSteady) bridge_->steadyBaseOrder[bridge_->steadyBaseEventCount++] = index;
        }
        continue;
      }
      bridge_->canonicalMasksReady = predecessor_ || mapping_;
      predecessor_ = nullptr;
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
  if (leftPhase != rightPhase) return leftPhase < rightPhase;
  if (eventKind(left) != eventKind(right)) return eventKind(left) < eventKind(right);
  if (left.tick != right.tick) return left.tick < right.tick;
  if (bridge_->insertionRanks[insertionIndex] != bridge_->baseRanks[baseIndex])
    return bridge_->insertionRanks[insertionIndex] < bridge_->baseRanks[baseIndex];
  return bridge_->insertionProvenance[insertionIndex] < baseIndex;
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
