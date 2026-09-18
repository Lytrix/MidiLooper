//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCompanionBridgeMap.h"
#include "Utils/NoteEditMem.h"
#include <cstdlib>
#include <new>
#include <algorithm>

namespace {
bool sameCompanionOrigin(RuntimeCompanionPasses::Origin a, RuntimeCompanionPasses::Origin b) {
  return a.pass == b.pass && a.event == b.event;
}
NOTE_EDIT_MEM bool companionOriginBefore(RuntimeCompanionPasses::Origin a, RuntimeCompanionPasses::Origin b) {
  return a.pass < b.pass || (a.pass == b.pass && a.event < b.event);
}
NOTE_EDIT_MEM bool sameCompanionInputEvent(const MidiEvent& a, const MidiEvent& b) {
  if (a.tick != b.tick || a.type != b.type || a.channel != b.channel || a.noteId != b.noteId) return false;
  switch (a.type) {
    case midi::NoteOn: case midi::NoteOff:
      return a.data.noteData.note == b.data.noteData.note && a.data.noteData.velocity == b.data.noteData.velocity;
    case midi::AfterTouchPoly:
      return a.data.polyATData.note == b.data.polyATData.note && a.data.polyATData.pressure == b.data.polyATData.pressure;
    case midi::ControlChange: return a.data.ccData.cc == b.data.ccData.cc && a.data.ccData.value == b.data.ccData.value;
    case midi::ProgramChange: return a.data.program == b.data.program;
    case midi::AfterTouchChannel: return a.data.channelPressure == b.data.channelPressure;
    case midi::PitchBend: return a.data.pitchBend == b.data.pitchBend;
    case midi::SystemExclusive: return a.data.sysexData.data == b.data.sysexData.data && a.data.sysexData.length == b.data.sysexData.length;
    case midi::TimeCodeQuarterFrame: return a.data.timeCode == b.data.timeCode;
    case midi::SongPosition: return a.data.songPosition == b.data.songPosition;
    case midi::SongSelect: return a.data.songNumber == b.data.songNumber;
    default: return true; // Remaining MIDI types have no payload.
  }
}
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::inputEventsMatch(const MidiEvent& a, const MidiEvent& b) {
  return sameCompanionInputEvent(a, b);
}
NOTE_EDIT_MEM RuntimeCompanionBridgeMap::~RuntimeCompanionBridgeMap() {
  if (records_ != nullptr)
    for (size_t i = 0; i < capacity_; ++i) records_[i].~PlaybackBridgeRecord();
  externalFirstFree(records_); externalFirstFree(recordRanks_); externalFirstFree(baseRanks_);
  externalFirstFree(baseIndex_);
}
NOTE_EDIT_MEM uint64_t RuntimeCompanionBridgeMap::requiredPerPassGrainsUpperBound(
    size_t baseCount, size_t candidateCount, size_t quantum) {
  if (!quantum || baseCount > RuntimePlaybackViewConfig::kEventsPerView ||
      candidateCount > RuntimePlaybackViewConfig::kEventsPerView) return UINT64_MAX;
  const uint64_t b = baseCount, a = candidateCount;
  size_t levels = 0, searchLevels = 0;
  for (size_t width = 1; width < baseCount; width *= 2) ++levels;
  for (size_t count = baseCount; count; count /= 2) ++searchLevels;
  const uint64_t visits = 4*b + 2*b*levels + a*(searchLevels+2);
  return RuntimePlaybackOrderBuilder::ceilGrains(visits, quantum) + 4;
}

NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::prewarm(size_t capacity) {
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kRecordsPerBridge) return false;
  if (capacity_ != 0) return capacity == capacity_;
  auto* records = static_cast<PlaybackBridgeRecord*>(externalFirstAlloc(capacity * sizeof(PlaybackBridgeRecord)));
  auto* ranks = static_cast<uint16_t*>(externalFirstAlloc(capacity * sizeof(uint16_t)));
  auto* baseRanks = static_cast<uint16_t*>(externalFirstAlloc(capacity * sizeof(uint16_t)));
  auto* baseIndex = static_cast<uint16_t*>(externalFirstAlloc(capacity * sizeof(uint16_t)));
  if (records == nullptr || ranks == nullptr || baseRanks == nullptr || baseIndex == nullptr) {
    externalFirstFree(records); externalFirstFree(ranks); externalFirstFree(baseRanks); externalFirstFree(baseIndex); return false;
  }
  for (size_t i = 0; i < capacity; ++i) new (&records[i]) PlaybackBridgeRecord();
  records_ = records; recordRanks_ = ranks; baseRanks_ = baseRanks; capacity_ = capacity;
  baseIndex_ = baseIndex;
  return true;
}
NOTE_EDIT_MEM void RuntimeCompanionBridgeMap::discard() {
  ++mapGeneration_;
  applied_ = nullptr; prepared_ = nullptr; baseOrigins_ = nullptr; baseIdentity_ = nullptr;
  baseView_ = nullptr;
  scan_ = 0; base_ = nullptr; count_ = cursor_ = lastStepInputs_ = 0; phase_ = Phase::Idle;
  sortWidth_ = 1; sortStart_ = sortOutput_ = searchEnd_ = 0;
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::begin(
    const RuntimeCompanionApply& applied, const MidiEvent* base, size_t baseCount,
    const PlaybackViewSourceIdentity& source) {
  if (!available() ||
      capacity_ == 0 || baseCount > capacity_ || applied.size() > capacity_ ||
      applied.isHistoryBatch() || !applied.matchesInput(base, baseCount, source)) return false;
  discard(); applied_ = &applied; base_ = base; baseCount_ = baseCount;
  candidateCount_ = applied.size(); source_ = source; generation_ = applied.generation();
  phase_ = Phase::Clear; return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::begin(
    const RuntimeCompanionPasses& prepared, const MidiEvent* base, size_t baseCount,
    const RuntimeCompanionPasses::Origin* baseOrigins,
    const PlaybackViewSourceIdentity& baseSource, const PlaybackViewSourceIdentity& preparedSource) {
  if (!available() ||
      capacity_ == 0 || baseCount > capacity_ || prepared.size() > capacity_ ||
      (baseCount != 0 && (base == nullptr || baseOrigins == nullptr)) || !prepared.ready(preparedSource)) return false;
  discard(); prepared_ = &prepared; base_ = base; baseCount_ = baseCount; baseOrigins_ = baseOrigins;
  baseIdentity_ = &baseSource; source_ = baseSource; preparedSource_ = preparedSource;
  candidateCount_ = prepared.size(); generation_ = prepared.generation(); phase_ = Phase::Clear;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::begin(
    const RuntimeCompanionPasses& prepared, const RuntimePlaybackView& base,
    const PlaybackViewSourceIdentity& preparedSource) {
  if (!base.published || !base.complete || !base.sourceFilled || !base.passOriginsReady ||
      base.passOrigins == nullptr || !(base.passOriginSource == base.source) ||
      !base.coverage.isValid()) return false;
  if (!begin(prepared, base.storage.events, base.storage.eventCount, base.passOrigins, base.source, preparedSource)) return false;
  baseView_ = &base;
  baseStorageGeneration_ = base.storageGeneration;
  coverage_ = base.coverage;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::valid() const {
  if (baseView_ && (baseView_->storageGeneration != baseStorageGeneration_ || !baseView_->published || !baseView_->complete || !baseView_->sourceFilled || !baseView_->passOriginsReady ||
      baseView_->passOrigins != baseOrigins_ || !(baseView_->passOriginSource == source_) ||
      baseView_->storage.events != base_ || baseView_->storage.eventCount != baseCount_ ||
      baseView_->coverage.startPhase != coverage_.startPhase ||
      baseView_->coverage.lengthTicks != coverage_.lengthTicks ||
      baseView_->coverage.loopLengthTicks != coverage_.loopLengthTicks)) return false;
  if (prepared_ != nullptr) return prepared_->generation() == generation_ &&
      prepared_->ready(preparedSource_) && prepared_->size() == candidateCount_ &&
      sourceIdentityMatches();
  return applied_ != nullptr && applied_->generation() == generation_ &&
         !applied_->isHistoryBatch() && applied_->matchesInput(base_, baseCount_, source_) && applied_->size() == candidateCount_;
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::ready(const PlaybackViewSourceIdentity& source) const {
  return phase_ == Phase::Complete && source == source_ && valid();
}
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::matchesBase(
    const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source) const {
  return ready(source) && events == base_ && count == baseCount_;
}
NOTE_EDIT_MEM const PlaybackBridgeRecord* RuntimeCompanionBridgeMap::records() const {
  return ready(source_) ? records_ : nullptr;
}
NOTE_EDIT_MEM const uint16_t* RuntimeCompanionBridgeMap::recordRanks() const {
  return ready(source_) ? recordRanks_ : nullptr;
}
NOTE_EDIT_MEM const uint16_t* RuntimeCompanionBridgeMap::baseRanks() const {
  return ready(source_) ? baseRanks_ : nullptr;
}
NOTE_EDIT_MEM size_t RuntimeCompanionBridgeMap::size() const { return ready(source_) ? count_ : 0; }
NOTE_EDIT_MEM bool RuntimeCompanionBridgeMap::append(
    PlaybackBridgeOperation op, uint32_t origin, const MidiEvent& event, uint16_t rank) {
  if (count_ == capacity_) return false;
  records_[count_] = {op, origin, event}; recordRanks_[count_++] = rank; return true;
}
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionBridgeMap::deny() {
  phase_ = Phase::Denied; return RuntimePlaybackOrderBuildResult::Denied;
}
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionBridgeMap::step(size_t quantum) {
  lastStepInputs_ = 0;
  if (quantum == 0 || phase_ == Phase::Idle) return RuntimePlaybackOrderBuildResult::Denied;
  if (phase_ == Phase::Denied || !valid()) return deny();
  if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  // At most Q entry/comparison visits across every phase, including unchanged rows.
  while (lastStepInputs_ < quantum) {
    if (phase_ == Phase::Clear) {
      if (cursor_ < baseCount_) {
        baseIndex_[cursor_] = static_cast<uint16_t>(cursor_); baseRanks_[cursor_++] = UINT16_MAX;
        ++lastStepInputs_; continue;
      }
      cursor_ = 0; phase_ = prepared_ != nullptr ? Phase::ValidateBase : Phase::Candidate;
    }
    if (phase_ == Phase::ValidateBase) {
      if (cursor_ < baseCount_) {
        ++lastStepInputs_;
        const auto* input = prepared_->inputEvent(baseOrigins_[cursor_]);
        if (input == nullptr || !sameCompanionInputEvent(*input, base_[cursor_])) return deny();
        ++cursor_; continue;
      }
      cursor_ = 0; phase_ = baseCount_ > 1 ? Phase::SortOrigins : Phase::BaseDuplicates;
    }
    if (phase_ == Phase::SortOrigins) {
      if (sortStart_ == baseCount_) { cursor_ = 0; phase_ = Phase::CopyOrigins; continue; }
      if (sortOutput_ == sortStart_) {
        sortLeft_ = sortStart_; sortLeftEnd_ = std::min(sortStart_ + sortWidth_, baseCount_);
        sortRight_ = sortLeftEnd_; sortRightEnd_ = std::min(sortLeftEnd_ + sortWidth_, baseCount_);
      }
      const bool left = sortRight_ == sortRightEnd_ ||
          (sortLeft_ < sortLeftEnd_ && !companionOriginBefore(baseOrigins_[baseIndex_[sortRight_]], baseOrigins_[baseIndex_[sortLeft_]]));
      // Record ranks are unpublished and unused until candidate mapping starts.
      recordRanks_[sortOutput_++] = baseIndex_[left ? sortLeft_++ : sortRight_++];
      ++lastStepInputs_;
      if (sortOutput_ == sortRightEnd_) sortStart_ = sortRightEnd_;
      continue;
    }
    if (phase_ == Phase::CopyOrigins) {
      if (cursor_ < baseCount_) { baseIndex_[cursor_] = recordRanks_[cursor_]; ++cursor_; ++lastStepInputs_; continue; }
      sortWidth_ *= 2; sortStart_ = sortOutput_ = cursor_ = 0;
      phase_ = sortWidth_ >= baseCount_ ? Phase::BaseDuplicates : Phase::SortOrigins;
      continue;
    }
    if (phase_ == Phase::BaseDuplicates) {
      if (cursor_ < baseCount_) {
        const size_t index = cursor_++; ++lastStepInputs_;
        if (index && sameCompanionOrigin(baseOrigins_[baseIndex_[index-1]], baseOrigins_[baseIndex_[index]])) return deny();
        continue;
      }
      cursor_ = 0; phase_ = Phase::Candidate;
    }
    if (phase_ == Phase::Candidate) {
      if (cursor_ < candidateCount_) {
        const size_t rank = cursor_++; ++lastStepInputs_;
        const auto& event = prepared_ != nullptr ? prepared_->data()[rank] : applied_->data()[rank];
        if (baseView_ && !coverage_.contains(event.tick % coverage_.loopLengthTicks)) continue;
        if (prepared_ != nullptr && prepared_->origins()[rank].event != UINT16_MAX) {
          scan_ = 0; searchEnd_ = baseCount_; phase_ = Phase::FindOrigin; continue;
        }
        const uint32_t origin = prepared_ != nullptr ? UINT32_MAX : applied_->provenance()[rank];
        if (origin == UINT32_MAX) {
          if (!event.isNoteOff() || event.noteId != kInvalidNoteId ||
              !append(PlaybackBridgeOperation::Insert, 0, event, rank)) return deny();
        } else {
          if (origin >= baseCount_ || baseRanks_[origin] != UINT16_MAX) return deny();
          baseRanks_[origin] = rank;
          // Delete/Length application changes only ticks on surviving rows;
          // payload identity is guaranteed by the typed apply owner.
          if (event.tick != base_[origin].tick &&
              !append(PlaybackBridgeOperation::Replace, origin, event, rank)) return deny();
        }
        continue;
      }
      cursor_ = 0; phase_ = Phase::Suppress;
    }
    if (phase_ == Phase::FindOrigin) {
      const size_t rank = cursor_ - 1;
      const auto& event = prepared_->data()[rank];
      const auto target = prepared_->origins()[rank];
      if (scan_ < searchEnd_) {
        const size_t middle = scan_ + (searchEnd_ - scan_)/2;
        if (companionOriginBefore(baseOrigins_[baseIndex_[middle]], target)) scan_ = middle+1;
        else searchEnd_ = middle;
        ++lastStepInputs_; continue;
      }
      ++lastStepInputs_; // Charge the final equality check and mapping action.
      if (scan_ < baseCount_ && sameCompanionOrigin(baseOrigins_[baseIndex_[scan_]], target)) {
        const size_t origin = baseIndex_[scan_];
        if (baseRanks_[origin] != UINT16_MAX) return deny();
        baseRanks_[origin] = rank;
        if (event.tick != base_[origin].tick && !append(PlaybackBridgeOperation::Replace, origin, event, rank)) return deny();
      } else if (!append(PlaybackBridgeOperation::Insert, 0, event, rank)) return deny();
      phase_ = Phase::Candidate; continue;
    }
    if (phase_ == Phase::Suppress) {
      if (cursor_ < baseCount_) {
        const size_t origin = cursor_++; ++lastStepInputs_;
        if (baseRanks_[origin] == UINT16_MAX &&
            !append(PlaybackBridgeOperation::Suppress, origin, base_[origin], UINT16_MAX)) return deny();
        continue;
      }
      phase_ = Phase::Complete; break;
    }
  }
  // A zero-input completion transition does not require another owner turn.
  if (phase_ == Phase::Suppress && cursor_ == baseCount_) phase_ = Phase::Complete;
  return phase_ == Phase::Complete ? RuntimePlaybackOrderBuildResult::Complete
                                  : RuntimePlaybackOrderBuildResult::Pending;
}
