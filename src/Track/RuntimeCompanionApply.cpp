// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "RuntimeCompanionApply.h"
#include "LoopEventStore.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/NoteEditMem.h"
#include <cstdlib>

NOTE_EDIT_MEM RuntimeCompanionApply::RuntimeCompanionApply() = default;
NOTE_EDIT_MEM RuntimeCompanionApply::~RuntimeCompanionApply() {
  if (provenance_ == nullptr) return;
  if (isInExternalMemoryPool(provenance_)) extmem_free(provenance_);
  else std::free(provenance_);
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::prewarm(size_t capacity) {
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView) return false;
  if (!candidate_.prewarm(capacity)) return false;
  if (provenance_ != nullptr) return candidate_.capacity == capacity;
  provenance_ = static_cast<uint32_t*>(extmem_malloc(capacity * sizeof(uint32_t)));
  if (provenance_ == nullptr) provenance_ = static_cast<uint32_t*>(std::malloc(capacity * sizeof(uint32_t)));
  return provenance_ != nullptr;
}
NOTE_EDIT_MEM size_t RuntimeCompanionApply::requiredGrainsUpperBound(size_t capacity, size_t quantum) {
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView || quantum == 0) return SIZE_MAX;
  const size_t work = 32 * capacity * capacity + 64 * capacity +
                      RuntimePlaybackOrderBuilder::workEntriesUpperBound(capacity);
  // Includes per-child rounding and scalar transition/action turns. This is
  // a component bound, not an extension of the production stop reservation.
  return 1 + (work - 1) / quantum + 32 * capacity + 64;
}

NOTE_EDIT_MEM void RuntimeCompanionApply::resetWork() {
  if (reconstruction_ != nullptr) reconstruction_->discard();
  if (spans_ != nullptr) spans_->discard();
  if (deletion_ != nullptr) deletion_->discard();
  if (overlaps_ != nullptr) overlaps_->discard();
  if (pair_ != nullptr) pair_->discard();
  order_.discard();
}
NOTE_EDIT_MEM uint64_t RuntimeCompanionApply::requiredHistoryGrainsUpperBound(
    size_t capacity, size_t rows, size_t quantum) {
  if (capacity == 0 || capacity > RuntimePlaybackViewConfig::kEventsPerView ||
      rows > RuntimePlaybackViewConfig::kRecordsPerBridge || quantum == 0) return UINT64_MAX;
  if (rows == 0) return 1 + (capacity - 1) / quantum;
  const auto ceilGrains = [quantum](uint64_t visits) -> uint64_t {
    return visits == 0 ? 0 : 1 + (visits - 1) / quantum;
  };
  // Successful prior Create payloads are each <= C. Charge every history row
  // and payload event to the identity scan, and a separate collapse scan.
  return (uint64_t(rows) + 1) * (requiredGrainsUpperBound(capacity, quantum) +
      ceilGrains(uint64_t(rows) * (capacity + 1)) + ceilGrains(rows) + 8);
}
NOTE_EDIT_MEM void RuntimeCompanionApply::releaseWork() {
  resetWork();
  reconstruction_ = nullptr; spans_ = nullptr; deletion_ = nullptr; overlaps_ = nullptr; pair_ = nullptr;
}
NOTE_EDIT_MEM void RuntimeCompanionApply::discard() {
  ++generation_;
  releaseWork();
  candidate_.clear();
  source_ = nullptr; input_ = nullptr;
  inputCount_ = cursor_ = write_ = 0;
  prefix_ = {};
  batch_ = canonicalSequence_ = history_ = false; rows_ = nullptr; rowCount_ = rowCursor_ = 0; loopHint_ = 0;
  identityDeleted_ = false; historyPayloadCursor_ = 0;
  selectedRows_ = nullptr;
  phase_ = Phase::Idle;
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::sourceMatches() const {
  return source_ != nullptr && *source_ == expected_;
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::ready(const PlaybackViewSourceIdentity& source) const {
  return phase_ == Phase::Complete && sourceMatches() && source == expected_;
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::matchesInput(
    const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source) const {
  return ready(source) && input_ == events && inputCount_ == count;
}
NOTE_EDIT_MEM const MidiEvent* RuntimeCompanionApply::data() const {
  return phase_ == Phase::Complete && sourceMatches() ? candidate_.events : nullptr;
}
NOTE_EDIT_MEM const uint32_t* RuntimeCompanionApply::provenance() const {
  return data() != nullptr ? provenance_ : nullptr;
}
NOTE_EDIT_MEM size_t RuntimeCompanionApply::size() const { return data() != nullptr ? candidate_.eventCount : 0; }
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionApply::deny() {
  releaseWork(); phase_ = Phase::Denied;
  return RuntimePlaybackOrderBuildResult::Denied;
}
NOTE_EDIT_MEM void RuntimeCompanionApply::complete() {
  if (batch_ && rowCursor_ < rowCount_) { resetWork(); phase_ = Phase::NextRow; }
  else { releaseWork(); phase_ = Phase::Complete; }
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::begin(
    const Work& work, const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source,
    Kind kind, NoteId target, uint32_t newEnd, uint32_t loopLength) {
  if ((phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Denied) ||
      !candidate_.isPrewarmed() || provenance_ == nullptr || count > candidate_.capacity ||
      (events == nullptr && count != 0) || !work.pair.available() ||
      !work.deletion.available() || !work.spans.available() ||
      !work.reconstruction.available() || !work.overlaps.available() ||
      (kind != Kind::Delete && kind != Kind::Length)) return false;
  const uintptr_t inputStart = reinterpret_cast<uintptr_t>(events);
  const uintptr_t ownStart = reinterpret_cast<uintptr_t>(candidate_.events);
  if (count != 0 && inputStart < ownStart + candidate_.capacity * sizeof(MidiEvent) &&
      ownStart < inputStart + count * sizeof(MidiEvent)) return false;
  discard();
  pair_ = &work.pair; deletion_ = &work.deletion; spans_ = &work.spans;
  reconstruction_ = &work.reconstruction; overlaps_ = &work.overlaps;
  input_ = events; inputCount_ = count; source_ = &source; expected_ = source;
  kind_ = kind; target_ = target; newEnd_ = newEnd; loopLength_ = loopLength; maxTick_ = 0;
  cycleActive_ = false; phase_ = Phase::Copy;
  return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::beginBatch(
    const Work& work, const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source,
    const EditPass* rows, size_t rowCount, uint32_t loopLength, bool canonicalSequence) {
  if ((rows == nullptr && rowCount != 0) || rowCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return false;
  if (!begin(work, events, count, source, Kind::Delete, kInvalidNoteId, 0, loopLength)) return false;
  batch_ = true; canonicalSequence_ = canonicalSequence; rows_ = rows; rowCount_ = rowCount; rowCursor_ = 0; loopHint_ = loopLength;
  return true;
}

NOTE_EDIT_MEM bool RuntimeCompanionApply::beginContinuationBatch(
    const Work& work, const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source,
    const EditPass* rows, size_t rowCount, uint32_t loopLength, HistoryPrefix history) {
  if (history.count > RuntimePlaybackViewConfig::kRecordsPerBridge ||
      (history.count && !history.rows && !history.selected) || (history.rows && history.selected)) return false;
  if (!beginBatch(work, events, count, source, rows, rowCount, loopLength, true)) return false;
  prefix_ = history; return true;
}
NOTE_EDIT_MEM uint64_t RuntimeCompanionApply::requiredContinuationGrainsUpperBound(
    size_t capacity, size_t historyCount, size_t rowCount, size_t quantum) {
  if (!capacity || capacity > RuntimePlaybackViewConfig::kEventsPerView || !quantum ||
      historyCount > RuntimePlaybackViewConfig::kRecordsPerBridge ||
      rowCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return UINT64_MAX;
  if (rowCount == 0) return 1 + (capacity - 1) / quantum;
  const uint64_t visits = uint64_t(historyCount) * (capacity + 1) + rowCount;
  const uint64_t scan = visits ? 1 + (visits - 1) / quantum : 0;
  return (uint64_t(rowCount) + 1) * (requiredGrainsUpperBound(capacity, quantum) + scan + 3);
}

NOTE_EDIT_MEM bool RuntimeCompanionApply::beginHistoryBatch(
    const Work& work, const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source,
    const EditPass* rows, size_t rowCount, uint32_t loopLength) {
  if (!beginBatch(work, events, count, source, rows, rowCount, loopLength, true)) return false;
  history_ = true;
  return true;
}

NOTE_EDIT_MEM bool RuntimeCompanionApply::selectBatchRow() {
  const auto* selected = rowAt(rowCursor_ - 1);
  if (!selected) return false;
  const auto& row = *selected;
  if (row.passType != EditPassType::Note ||
      (!history_ && !row.addedEvents.empty()) ||
      (history_ && row.state != EditPassState::Active)) return false;
  if (history_ && row.actionType == EditActionType::Create) {
    if (row.addedEvents.size() > candidate_.capacity - candidate_.eventCount) return false;
    cursor_ = 0; phase_ = Phase::CreateAppend;
    return true;
  }
  if (row.actionType == EditActionType::Delete) kind_ = Kind::Delete;
  else if (row.actionType == EditActionType::Update && row.propertyType == EditPropertyType::Length)
    kind_ = Kind::Length;
  else if (history_ && row.actionType == EditActionType::Update) {
    if (row.propertyType == EditPropertyType::Pitch) kind_ = Kind::Pitch;
    else if (row.propertyType == EditPropertyType::Velocity) kind_ = Kind::Velocity;
    else if (row.propertyType == EditPropertyType::NoteRange) kind_ = Kind::NoteRange;
    else { complete(); return true; } // Canonical Update default is a no-op, but remains a collapse barrier.
  } else return false;
  target_ = row.targetNoteId; newEnd_ = row.endTick; loopLength_ = loopHint_;
  newStart_ = row.startTick;
  newValue_ = kind_ == Kind::Pitch ? row.pitch : row.velocity;
  if (canonicalSequence_) {
    if (target_ == kInvalidNoteId) complete();
    else {
      cursor_ = historyPayloadCursor_ = 0; identityDeleted_ = false;
      phase_ = Phase::PriorDeletes;
    }
    return true;
  }
  return startBatchRow();
}

NOTE_EDIT_MEM bool RuntimeCompanionApply::startBatchRow() {
  if (loopLength_ == 0) { maxTick_ = 0; cursor_ = 0; phase_ = Phase::InferLength; }
  else if (kind_ == Kind::Delete || kind_ == Kind::Pitch || kind_ == Kind::NoteRange) { if (!startDelete(target_)) return false; }
  else startFind(Find::Initial, target_);
  return true;
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::beginSelectedHistoryBatch(
    const Work& work, const MidiEvent* events, size_t count, const PlaybackViewSourceIdentity& source,
    const EditPass* const* rows, size_t rowCount, uint32_t loopLength) {
  if ((!rows && rowCount) || rowCount > RuntimePlaybackViewConfig::kRecordsPerBridge) return false;
  if (!beginHistoryBatch(work, events, count, source, nullptr, 0, loopLength)) return false;
  selectedRows_ = rows; rowCount_ = rowCount; return true;
}

NOTE_EDIT_MEM void RuntimeCompanionApply::startFind(Find purpose, NoteId id) {
  find_ = purpose; findId_ = id; on_ = off_ = -1; cursor_ = 0; phase_ = Phase::Find;
}
NOTE_EDIT_MEM bool RuntimeCompanionApply::startDelete(NoteId id) {
  if (!deletion_->begin(*pair_, candidate_.events, candidate_.eventCount, id,
                        loopLength_, candidate_.retained)) return false;
  phase_ = Phase::DeleteLookup;
  return true;
}
NOTE_EDIT_MEM void RuntimeCompanionApply::resumeOverlaps() {
  overlaps_->consumeAction(); phase_ = Phase::Overlaps;
}
NOTE_EDIT_MEM RuntimePlaybackOrderBuildResult RuntimeCompanionApply::step(size_t quantum) {
  if (phase_ == Phase::Idle || quantum == 0) return RuntimePlaybackOrderBuildResult::Denied;
  if (phase_ == Phase::Denied || !sourceMatches()) return deny();
  if (phase_ == Phase::Complete) return RuntimePlaybackOrderBuildResult::Complete;
  // Exactly one phase/child per turn. Completion transitions only set up the
  // next child; they do not execute another content-sized phase in this turn.
  if (phase_ == Phase::Copy) {
    while (quantum-- != 0 && cursor_ < inputCount_) {
      const auto& event = input_[cursor_];
      if (!candidate_.append(event)) return deny();
      provenance_[cursor_] = static_cast<uint32_t>(cursor_);
      ++cursor_;
      if (event.tick > maxTick_) maxTick_ = event.tick;
    }
    if (cursor_ == inputCount_) {
      // No caller-supplied length: derive from the last tick and clamp to one bar minimum.
      if (loopLength_ == 0) {
        loopLength_ = maxTick_ + 1u;
        if (loopLength_ < LoopEventStoreConfig::BAR_TICKS) loopLength_ = LoopEventStoreConfig::BAR_TICKS;
      }
      if (batch_) {
        if (rowCount_ == 0) complete();
        else phase_ = Phase::NextRow;
      } else if (kind_ == Kind::Delete) { if (!startDelete(target_)) return deny(); }
      else startFind(Find::Initial, target_);
    }
  } else if (phase_ == Phase::NextRow) {
    ++rowCursor_;
    if (history_) phase_ = Phase::CollapseRows;
    else if (!selectBatchRow()) return deny();
  } else if (phase_ == Phase::CollapseRows) {
    while (quantum-- != 0 && rowCursor_ < rowCount_) {
      const auto* selected = rowAt(rowCursor_ - 1);
      const auto* following = rowAt(rowCursor_);
      if (!selected || !following) return deny();
      const auto& row = *selected;
      const auto& next = *following;
      if (row.actionType != EditActionType::Update || row.targetNoteId == kInvalidNoteId ||
          (row.propertyType != EditPropertyType::Pitch && row.propertyType != EditPropertyType::Velocity) ||
          next.actionType != EditActionType::Update || next.targetNoteId != row.targetNoteId ||
          next.propertyType != row.propertyType) {
        if (!selectBatchRow()) return deny();
        break;
      }
      // Enforce the filtered active-Note input contract even for collapsed rows.
      if (row.state != EditPassState::Active || row.passType != EditPassType::Note) return deny();
      ++rowCursor_;
    }
    if (phase_ == Phase::CollapseRows && rowCursor_ == rowCount_ && !selectBatchRow()) return deny();
  } else if (phase_ == Phase::PriorDeletes) {
    // Sequence membership outlives individual event mutations. Delete removes
    // an identity even if a duplicate On survives; a later Create On restores it.
    const size_t priorCount = prefix_.count + rowCursor_ - 1;
    while (quantum-- != 0 && cursor_ < priorCount) {
      const bool inPrefix = cursor_ < prefix_.count;
      const auto* selected = inPrefix
          ? (prefix_.selected ? prefix_.selected[cursor_] : prefix_.rows + cursor_)
          : rowAt(cursor_ - prefix_.count);
      if (!selected) return deny();
      const auto& prior = *selected;
      if ((history_ || inPrefix) && prior.actionType == EditActionType::Create &&
          historyPayloadCursor_ < prior.addedEvents.size()) {
        const auto& event = prior.addedEvents[historyPayloadCursor_++];
        if (event.isNoteOn() && event.noteId == target_) identityDeleted_ = false;
        continue;
      }
      ++cursor_; historyPayloadCursor_ = 0;
      if (prior.actionType == EditActionType::Delete && prior.targetNoteId == target_) {
        identityDeleted_ = true;
      }
    }
    if (cursor_ == priorCount) {
      if (identityDeleted_) complete();
      else if (!startBatchRow()) return deny();
    }
  } else if (phase_ == Phase::CreateAppend) {
    const auto* selected = rowAt(rowCursor_ - 1);
    if (!selected) return deny();
    const auto& added = selected->addedEvents;
    while (quantum-- != 0 && cursor_ < added.size()) {
      const size_t index = candidate_.eventCount;
      if (!candidate_.append(added[cursor_++])) return deny();
      provenance_[index] = UINT32_MAX;
    }
    if (cursor_ == added.size()) {
      if (!order_.beginCanonicalCapture(candidate_)) return deny();
      phase_ = Phase::Order;
    }
  } else if (phase_ == Phase::InferLength) {
    while (quantum-- != 0 && cursor_ < candidate_.eventCount) {
      const uint32_t tick = candidate_.events[cursor_++].tick;
      if (tick > maxTick_) maxTick_ = tick;
    }
    if (cursor_ == candidate_.eventCount) {
      // Derive the length from the last tick, clamped to one bar minimum.
      loopLength_ = maxTick_ + 1u;
      if (loopLength_ < LoopEventStoreConfig::BAR_TICKS) loopLength_ = LoopEventStoreConfig::BAR_TICKS;
      if (kind_ == Kind::Delete || kind_ == Kind::Pitch || kind_ == Kind::NoteRange) { if (!startDelete(target_)) return deny(); }
      else startFind(Find::Initial, target_);
    }
  } else if (phase_ == Phase::Find) {
    while (quantum-- != 0 && cursor_ < candidate_.eventCount) {
      const size_t index = cursor_++;
      if (candidate_.retained[index] && findId_ != kInvalidNoteId &&
          candidate_.events[index].isNoteOn() && candidate_.events[index].noteId == findId_) {
        on_ = static_cast<int>(index); break;
      }
    }
    if (on_ >= 0) {
      if (kind_ == Kind::Velocity) {
        candidate_.events[on_].data.noteData.velocity = newValue_;
        complete();
      } else {
        if (!pair_->begin(candidate_.events, candidate_.eventCount, on_, candidate_.retained)) return deny();
        phase_ = Phase::Pair;
      }
    } else if (cursor_ == candidate_.eventCount) {
      if (find_ == Find::Initial) complete();
      else if (find_ == Find::Action) resumeOverlaps();
      else { phase_ = Phase::Write; }
    }
  } else if (phase_ == Phase::Pair) {
    const auto result = pair_->step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      off_ = pair_->result();
      if (find_ == Find::Initial) {
        const auto& on = candidate_.events[on_];
        start_ = on.tick; pitch_ = on.data.noteData.note;
        const uint32_t refEnd = off_ >= 0 ? candidate_.events[off_].tick : start_;
        if ((off_ < 0 && newEnd_ == loopLength_) || newEnd_ == refEnd) complete();
        else if (newEnd_ < refEnd) { writeEnd_ = newEnd_; phase_ = Phase::Write; }
        else {
          if (!reconstruction_->begin(*spans_, *pair_, candidate_.events, candidate_.eventCount,
                                       pitch_, loopLength_, expected_, candidate_.retained)) return deny();
          phase_ = Phase::Reconstruct;
        }
      } else { phase_ = Phase::Write; }
    }
  } else if (phase_ == Phase::Write) {
    const uint32_t end = find_ == Find::Final ? newEnd_ : writeEnd_;
    if (off_ >= 0) candidate_.events[off_].tick = end;
    else if (find_ == Find::Final && on_ >= 0) {
      const auto on = candidate_.events[on_];
      const size_t index = candidate_.eventCount;
      if (!candidate_.append(MidiEvent::NoteOff(end, on.channel, on.data.noteData.note))) return deny();
      provenance_[index] = UINT32_MAX;
    }
    if (find_ == Find::Initial) complete();
    else if (find_ == Find::Action) resumeOverlaps();
    else {
      // The legacy same-tick Off helper copies each event back to its original
      // index, so its net event mutation is identity. Preserve the actual
      // reference result, then apply its stable raw-tick sort.
      if (!order_.beginCanonicalCapture(candidate_)) return deny();
      phase_ = Phase::Order;
    }
  } else if (phase_ == Phase::Reconstruct) {
    const auto result = reconstruction_->step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      if (!reconstruction_->ready(expected_) || !overlaps_->begin(reconstruction_->data(), reconstruction_->size(),
                                      target_, pitch_, start_, newEnd_, loopLength_)) return deny();
      phase_ = Phase::Overlaps;
    }
  } else if (phase_ == Phase::Overlaps) {
    const auto result = overlaps_->step(quantum);
    if (result == RuntimeCompanionOverlapResult::Denied) return deny();
    if (result == RuntimeCompanionOverlapResult::Complete) { cursor_ = write_ = 0; phase_ = Phase::Compact; }
    else if (result == RuntimeCompanionOverlapResult::ActionReady) {
      const auto& action = overlaps_->action();
      if (action.kind == RuntimeCompanionOverlapWalk::ActionKind::Delete) {
        if (!startDelete(action.noteId)) return deny();
      } else { writeEnd_ = action.endTick; startFind(Find::Action, action.noteId); }
    }
  } else if (phase_ == Phase::DeleteLookup) {
    const auto result = deletion_->step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      deleteRows_[0] = deletion_->onIndex(); deleteRows_[1] = deletion_->offIndex(); deleteRows_[2] = deletion_->wrapOffIndex();
      deleteCursor_ = 0;
      phase_ = kind_ == Kind::Pitch || kind_ == Kind::NoteRange ? Phase::HistoryWrite : Phase::DeleteRows;
    }
  } else if (phase_ == Phase::HistoryWrite) {
    const int onIndex = deleteRows_[0], lifoOff = deleteRows_[1], wrapOff = deleteRows_[2];
    if (onIndex < 0) complete();
    else if (kind_ == Kind::Pitch) {
      while (quantum-- != 0 && deleteCursor_ < 3) {
        const int index = deleteRows_[deleteCursor_++];
        if (index >= 0) candidate_.events[index].data.noteData.note = newValue_;
      }
      if (deleteCursor_ == 3) complete();
    } else {
      // A fixed-size mutation of one resolved note; compaction is a later grain.
      candidate_.events[onIndex].tick = newStart_;
      const int selectedOff = wrapOff >= 0 ? wrapOff : lifoOff;
      if (selectedOff >= 0) candidate_.events[selectedOff].tick = newEnd_;
      else {
        const auto on = candidate_.events[onIndex];
        const size_t index = candidate_.eventCount;
        if (!candidate_.append(MidiEvent::NoteOff(newEnd_, on.channel, on.data.noteData.note, 0))) return deny();
        provenance_[index] = UINT32_MAX;
      }
      if (wrapOff >= 0 && lifoOff >= 0 && lifoOff != wrapOff) {
        candidate_.retained[lifoOff] = 0;
        cursor_ = write_ = 0; phase_ = Phase::Compact;
      } else complete();
    }
  } else if (phase_ == Phase::DeleteRows) {
    while (quantum-- != 0 && deleteCursor_ < 3) {
      const int index = deleteRows_[deleteCursor_++];
      if (index >= 0) candidate_.retained[index] = 0;
    }
    if (deleteCursor_ == 3) {
      if (kind_ == Kind::Delete) { cursor_ = write_ = 0; phase_ = Phase::Compact; }
      else resumeOverlaps();
    }
  } else if (phase_ == Phase::Compact) {
    while (quantum-- != 0 && cursor_ < candidate_.eventCount) {
      const size_t index = cursor_++;
      if (!candidate_.retained[index]) continue;
      candidate_.events[write_] = candidate_.events[index];
      provenance_[write_] = provenance_[index];
      candidate_.retained[write_++] = 1;
    }
    if (cursor_ == candidate_.eventCount) {
      candidate_.eventCount = write_;
      if (kind_ == Kind::Delete || kind_ == Kind::NoteRange) complete();
      else startFind(Find::Final, target_);
    }
  } else if (phase_ == Phase::Order) {
    const auto result = order_.step(quantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) return deny();
    if (result == RuntimePlaybackOrderBuildResult::Complete) { cursor_ = 0; phase_ = Phase::ClearVisit; }
  } else if (phase_ == Phase::ClearVisit) {
    while (quantum-- != 0 && cursor_ < candidate_.eventCount) candidate_.retained[cursor_++] = 0;
    if (cursor_ == candidate_.eventCount) { cursor_ = 0; cycleActive_ = false; phase_ = Phase::Permute; }
  } else if (phase_ == Phase::Permute) {
    while (quantum != 0 && (cycleActive_ || cursor_ < candidate_.eventCount)) {
      --quantum;
      if (!cycleActive_) {
        const size_t index = cursor_++;
        if (candidate_.retained[index]) continue;
        cycleStart_ = cycleDestination_ = index; cycleEvent_ = candidate_.events[index];
        cycleProvenance_ = provenance_[index]; cycleActive_ = true;
      } else {
        const size_t next = candidate_.order[cycleDestination_];
        candidate_.events[cycleDestination_] = next == cycleStart_ ? cycleEvent_ : candidate_.events[next];
        provenance_[cycleDestination_] = next == cycleStart_ ? cycleProvenance_ : provenance_[next];
        candidate_.retained[cycleDestination_] = 1;
        candidate_.order[cycleDestination_] = static_cast<uint16_t>(cycleDestination_);
        cycleDestination_ = next;
        if (next == cycleStart_) cycleActive_ = false;
      }
    }
    if (!cycleActive_ && cursor_ == candidate_.eventCount) complete();
  }
  return phase_ == Phase::Complete ? RuntimePlaybackOrderBuildResult::Complete : RuntimePlaybackOrderBuildResult::Pending;
}
