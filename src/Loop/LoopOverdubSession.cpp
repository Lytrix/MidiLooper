//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"
#include <algorithm>

LOOP_COLD_MEM void Loop::syncNextOverdubSessionIndexFromPasses() {
  uint8_t maxIndex = kUngroupedOverdubSessionIndex;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.overdubSessionIndex > maxIndex) {
      maxIndex = pass.overdubSessionIndex;
    }
  }
  if (maxIndex == kUngroupedOverdubSessionIndex || maxIndex == 255) {
    nextOverdubSessionIndex_ = 1;
  } else {
    nextOverdubSessionIndex_ = static_cast<uint8_t>(maxIndex + 1);
  }
  currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
}

LOOP_COLD_MEM void Loop::openOverdubSession(uint32_t sessionPlayheadPhaseTick, bool forceNew) {
  if (hasOverdubSession() && !forceNew) {
    return;
  }
  // Record-stop may still hold sealed rows when visual has not consumed that
  // revision. Those rows describe the previous session; wrapping them into this
  // one leaves a 336→336 hold beside the occupy suffix (`001647`).
  clearSealedCapturePreview();
  playheadPhaseTick = sessionPlayheadPhaseTick;
  currentOverdubSessionIndex_ = nextOverdubSessionIndex_;
  if (nextOverdubSessionIndex_ == 255) {
    nextOverdubSessionIndex_ = 1;
  } else {
    ++nextOverdubSessionIndex_;
  }
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
  ++overdubSessionUndoGeneration_;
  overdubSessionPassIds_.clear();
  overdubSessionCompanionIds_.clear();
  overdubSessionCursor_ = 0;
  overdubSessionLiveUndoEvents_.clear();
  // A successor session may hand off even while an unconsumed receipt sits in
  // the ring; the flag is per session, not per ring.
  sessionExitHandedOff_ = false;
}

LOOP_COLD_MEM bool Loop::markSessionExitTailReady(size_t ringIndex, PassId lastPassId) {
  SessionExitSnapshotHandoff* receipt = sessionExitReceiptByRingIndex(ringIndex);
  if (receipt == nullptr || !receipt->pending || receipt->ready) {
    return false;
  }
  receipt->lastPassId = lastPassId;
  receipt->ready = true;
  return true;
}

LOOP_COLD_MEM void Loop::closeOverdubSession() {
  retireSealedCapturePreviewForDisplay();
  ++overdubSessionUndoGeneration_;
  playheadPhaseTick = UINT32_MAX;
  currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
  overdubSessionPassIds_.clear();
  overdubSessionCompanionIds_.clear();
  overdubSessionCursor_ = 0;
  overdubSessionLiveUndoEvents_.clear();
  clearOverdubSourceView();
  discardUnpublishedSnapshotIfNotSessionExit();
}

LOOP_COLD_MEM bool Loop::beginCloseOverdubSession(SessionClose& work) {
  using Phase = SessionClose::Phase;
  if ((work.phase != Phase::Idle && work.phase != Phase::Complete) ||
      !hasOverdubSession() || overdubSessionUndoGeneration_ == UINT64_MAX ||
      (pendingSnapshotPublishBox_ && !pendingSnapshotSessionExitQueued())) return false;
  work = {};
  work.owner = this;
  work.generation = ++overdubSessionUndoGeneration_;
  work.phase = Phase::Passes;
  return true;
}

LOOP_COLD_MEM Loop::SessionClose::Phase Loop::stepCloseOverdubSession(
    SessionClose& work, size_t quantum) {
  using Phase = SessionClose::Phase;
  if (work.owner != this || !quantum) return Phase::Denied;
  if (work.generation != overdubSessionUndoGeneration_ ||
      (pendingSnapshotPublishBox_ && !pendingSnapshotSessionExitQueued())) {
    work.phase = Phase::Denied;
    return work.phase;
  }
  while (quantum-- && work.phase >= Phase::Passes && work.phase <= Phase::Finish) {
    ++work.units;
    switch (work.phase) {
      case Phase::Passes:
        if (!overdubSessionPassIds_.empty()) overdubSessionPassIds_.pop_back();
        else work.phase = Phase::Companions;
        break;
      case Phase::Companions:
        if (overdubSessionCompanionIds_.empty()) work.phase = Phase::LiveEvents;
        else if (!overdubSessionCompanionIds_.back().empty())
          overdubSessionCompanionIds_.back().pop_back();
        else overdubSessionCompanionIds_.pop_back();
        break;
      case Phase::LiveEvents:
        if (!overdubSessionLiveUndoEvents_.empty()) overdubSessionLiveUndoEvents_.pop_back();
        else work.phase = Phase::SourceEvents;
        break;
      case Phase::SourceEvents:
        if (!overdubSourceViewEvents_.empty()) overdubSourceViewEvents_.pop_back();
        else work.phase = Phase::SourceNotes;
        break;
      case Phase::SourceNotes:
        if (!overdubSourceViewNotes_.empty()) overdubSourceViewNotes_.pop_back();
        else work.phase = Phase::Finish;
        break;
      case Phase::Finish:
        playheadPhaseTick = UINT32_MAX;
        currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
        overdubWrapArmed_ = overdubWrapSuppressNext_ = false;
        overdubSessionCursor_ = 0;
        overdubSourceViewLoopLengthTicks_ = 0;
        overdubSourceViewEstablished_ = false;
        retireSealedCapturePreviewForDisplay();
        work.phase = Phase::Complete;
        break;
      default: break;
    }
  }
  return work.phase;
}

LOOP_COLD_MEM void Loop::armOverdubWrapAfterLeavingStart(uint32_t currentPhase) {
  if (!hasOverdubSession() || overdubWrapArmed_ || overdubWrapSuppressNext_) {
    return;
  }
  if (currentPhase != playheadPhaseTick) {
    overdubWrapArmed_ = true;
  }
}

LOOP_COLD_MEM bool Loop::shouldCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) const {
  if (!hasOverdubSession() || !overdubWrapArmed_ || loopLengthTicks == 0) {
    return false;
  }
  // DEC-051: seal when playhead crosses loop end (phase 0). S stays session identity.
  return IntervalProjection::didPlayheadCrossPhase(prevPhase, currentPhase, 0, loopLengthTicks);
}

LOOP_COLD_MEM void Loop::noteOverdubWrapCommitted() {
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
}

LOOP_COLD_MEM void Loop::suppressNextOverdubWrap() {
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = true;
}

LOOP_COLD_MEM bool Loop::consumeSuppressedOverdubWrapCrossing(uint32_t prevPhase,
                                                             uint32_t currentPhase) {
  if (!hasOverdubSession() || !overdubWrapSuppressNext_ || loopLengthTicks == 0) {
    return false;
  }
  if (!IntervalProjection::didPlayheadCrossPhase(prevPhase, currentPhase, 0, loopLengthTicks)) {
    return false;
  }
  overdubWrapSuppressNext_ = false;
  overdubWrapArmed_ = false;
  return true;
}

LOOP_COLD_MEM size_t Loop::extractOpenCaptureNoteOns(SessionMidiEventVec& out) {
  out.clear();
  if (!captureActive() || capture.store.empty() || loopLengthTicks == 0) {
    return 0;
  }
  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  if (flat.empty()) {
    return 0;
  }
  // Preview pairs in append order (wrap-held On@2976 + Off@96). Display
  // materialize / playback sort the store, so findOpenNoteOns on ticks treats
  // that On as open when a later same-pitch On is still held (014937).
  std::vector<char> extractAt(flat.size(), 0);
  bool usedPreview = false;
  for (const uint32_t previewIndex : capturePreview.openNoteIndices) {
    if (previewIndex >= capturePreview.notes.size() ||
        previewIndex >= capturePreview.noteStates.size() ||
        !capturePreview.noteStates[previewIndex].open) {
      continue;
    }
    usedPreview = true;
    const NoteUtils::DisplayNote& note = capturePreview.notes[previewIndex];
    for (size_t i = flat.size(); i > 0; --i) {
      const size_t eventIndex = i - 1;
      if (extractAt[eventIndex] != 0) {
        continue;
      }
      const MidiEvent& evt = flat[eventIndex];
      if (evt.isNoteOn() && evt.data.noteData.note == note.note && evt.tick == note.startTick) {
        extractAt[eventIndex] = 1;
        break;
      }
    }
  }
  if (!usedPreview) {
    const std::vector<NoteUtils::OpenNoteOn> opens =
        NoteUtils::findOpenNoteOns(flat, loopLengthTicks);
    for (const NoteUtils::OpenNoteOn& open : opens) {
      if (open.eventIndex < flat.size()) {
        extractAt[open.eventIndex] = 1;
      }
    }
  }
  SessionMidiEventVec kept;
  kept.reserve(flat.size());
  for (size_t eventIndex = 0; eventIndex < flat.size(); ++eventIndex) {
    if (extractAt[eventIndex] != 0) {
      out.push_back(flat[eventIndex]);
    } else {
      kept.push_back(flat[eventIndex]);
    }
  }
  if (out.empty()) {
    return 0;
  }
  capture.store.clear();
  if (!kept.empty()) {
    capture.store.loadFromEvents(kept);
  }
  captureEventsSortDirty = true;
  ensureCaptureEventsSorted();
  rebuildCapturePreviewFromStore(*this);
  ++captureDisplayRevision;
  return out.size();
}

LOOP_COLD_MEM void Loop::pushOverdubSessionPass(PassId passId, EditPassIdList companionIds) {
  if (passId == kInvalidPassId) {
    return;
  }
  dropOverdubSessionRedoTail();
  ++overdubSessionUndoGeneration_;
  overdubSessionPassIds_.push_back(passId);
  overdubSessionCompanionIds_.push_back(std::move(companionIds));
  overdubSessionCursor_ = overdubSessionPassIds_.size();
}

LOOP_COLD_MEM bool Loop::collectLastOverdubSessionPass(PassId& passId,
                                                      EditPassIdList& companionIds) const {
  passId = kInvalidPassId;
  companionIds.clear();
  if (overdubSessionCursor_ == 0 || overdubSessionPassIds_.empty()) {
    return false;
  }
  const size_t index = overdubSessionCursor_ - 1;
  if (index >= overdubSessionPassIds_.size()) {
    return false;
  }
  passId = overdubSessionPassIds_[index];
  if (passId == kInvalidPassId) {
    return false;
  }
  if (index < overdubSessionCompanionIds_.size()) {
    companionIds = overdubSessionCompanionIds_[index];
  }
  return true;
}

LOOP_COLD_MEM void Loop::collectOverdubSessionUndoPasses(PassIdList& passIds,
                                                        EditPassIdList& companionIds) const {
  passIds.clear();
  companionIds.clear();
  for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionPassIds_.size(); ++i) {
    const PassId passId = overdubSessionPassIds_[i];
    if (passId == kInvalidPassId) {
      continue;
    }
    passIds.push_back(passId);
    if (i < overdubSessionCompanionIds_.size()) {
      for (const EditPassId id : overdubSessionCompanionIds_[i]) {
        companionIds.push_back(id);
      }
    }
  }
}

LOOP_COLD_MEM bool Loop::gatherSessionExitReceiptUndoIdentities(size_t ringIndex, PassIdList& passIds,
                                                              EditPassIdList& companionIds) const {
  passIds.clear();
  companionIds.clear();
  // DEC-068: the receipt is this interval's own; it left the shared session at its stop admission, so
  // it is the only source that cannot describe a successor that reentered the session meanwhile.
  const SessionExitSnapshotHandoff* receipt = sessionExitReceiptByRingIndex(ringIndex);
  if (receipt == nullptr) return false;
  passIds.reserve(receipt->passIds.size());
  for (const PassId id : receipt->passIds) {
    if (id != kInvalidPassId) passIds.push_back(id);
  }
  for (const EditPassIdList& row : receipt->companionIds) {
    for (const EditPassId companionId : row) {
      if (companionId != kInvalidEditPassId) companionIds.push_back(companionId);
    }
  }
  return !passIds.empty();
}

LOOP_COLD_MEM uint64_t Loop::requiredSessionUndoCollectionGrains(
    size_t rows, size_t companions, size_t quantum) {
  if (!quantum || rows > 2048 || companions > 2048) return UINT64_MAX;
  // One pass visit, one companion-row boundary per valid pass, each companion,
  // and a final completion visit. Invalid pass rows need only the pass visit.
  return 1 + (uint64_t(rows) * 2 + companions) / quantum;
}

LOOP_COLD_MEM bool Loop::beginCollectOverdubSessionUndoPasses(
    SessionUndoCollection& work, PassIdList& passIds, EditPassIdList& companionIds,
    const SessionExitSnapshotHandoff* receipt) const {
  using Phase = SessionUndoCollection::Phase;
  const bool receiptSourced = receipt != nullptr;
  const size_t limit = receiptSourced
                           ? receipt->passIds.size()
                           : std::min(overdubSessionCursor_, overdubSessionPassIds_.size());
  if ((work.phase >= Phase::Pass && work.phase <= Phase::FinalCompanions) || limit > 2048 ||
      !passIds.empty() || !companionIds.empty() || passIds.capacity() < limit ||
      static_cast<const void*>(&passIds) == static_cast<const void*>(&companionIds)) return false;
  work = {};
  work.owner = this; work.generation = overdubSessionUndoGeneration_;
  work.passIds = &passIds; work.companionIds = &companionIds; work.limit = limit;
  if (receiptSourced) {
    work.receiptPassIds = receipt->passIds.data();
    work.receiptPassIdCount = receipt->passIds.size();
    work.receiptCompanionRows = receipt->companionIds.data();
    work.receiptCompanionRowCount = receipt->companionIds.size();
    work.receiptIdentity = receipt;
  }
  work.phase = Phase::Pass;
  return true;
}

LOOP_COLD_MEM bool Loop::collectedOverdubSessionUndoPassesCurrent(
    const SessionUndoCollection& work) const {
  return work.phase == SessionUndoCollection::Phase::Complete && work.owner == this &&
      (work.receiptIdentity != nullptr || work.generation == overdubSessionUndoGeneration_) &&
      work.passIds && work.companionIds &&
      (!work.finalReceipt || (work.receiptGeneration == preparedCompanionHistory_.preparationGeneration &&
          preparedCompanionHistory_.storageGeneration == canonicalStorageGeneration &&
          preparedCompanionHistory_.revision == playbackRevision && preparedCompanionHistory_.firstId == nextPassId_ &&
          (preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Ready ||
           preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Committed) &&
          work.receiptCount == preparedCompanionHistory_.companionIds.size())) &&
      work.passIds->size() == work.passCount && work.companionIds->size() == work.companionCount;
}

LOOP_COLD_MEM Loop::SessionUndoCollection::Phase Loop::stepCollectOverdubSessionUndoPasses(
    SessionUndoCollection& work, size_t quantum) const {
  using Phase = SessionUndoCollection::Phase;
  // A call on the wrong owner or with no quantum must not cancel valid work.
  if (!quantum || work.owner != this) return Phase::Denied;
  if (work.phase == Phase::Complete && collectedOverdubSessionUndoPassesCurrent(work)) return Phase::Complete;
  if ((work.phase < Phase::Pass || work.phase > Phase::FinalCompanions) ||
      (work.finalReceipt && (work.receiptGeneration != preparedCompanionHistory_.preparationGeneration ||
          preparedCompanionHistory_.storageGeneration != canonicalStorageGeneration ||
          preparedCompanionHistory_.revision != playbackRevision || preparedCompanionHistory_.firstId != nextPassId_ ||
          (preparedCompanionHistory_.phase != PreparedCompanionHistory::Phase::Ready &&
           preparedCompanionHistory_.phase != PreparedCompanionHistory::Phase::Committed) ||
          work.receiptCount != preparedCompanionHistory_.companionIds.size())) ||
      (work.receiptIdentity == nullptr && work.generation != overdubSessionUndoGeneration_) ||
      !work.passIds || !work.companionIds ||
      work.passIds->size() != work.passCount || work.companionIds->size() != work.companionCount) {
    work.phase = Phase::Denied;
    return work.phase;
  }
  // DEC-068: a deferred interval reads its own session-exit receipt, never the live session lists
  // (a successor may already own those). Both sources are index-aligned row lists.
  const PassId* sessionPassIds =
      work.receiptIdentity != nullptr ? work.receiptPassIds : overdubSessionPassIds_.data();
  const size_t sessionPassIdCount =
      work.receiptIdentity != nullptr ? work.receiptPassIdCount : overdubSessionPassIds_.size();
  const EditPassIdList* sessionCompanionRows =
      work.receiptIdentity != nullptr ? work.receiptCompanionRows : overdubSessionCompanionIds_.data();
  const size_t sessionCompanionRowCount =
      work.receiptIdentity != nullptr ? work.receiptCompanionRowCount
                                      : overdubSessionCompanionIds_.size();
  while (quantum-- && work.phase != Phase::Complete) {
    if (work.phase == Phase::Pass) {
      if (work.row == work.limit) {
        work.phase = work.finalReceipt ? Phase::FinalPass : Phase::Complete;
        continue;
      }
      const PassId id = work.row < sessionPassIdCount ? sessionPassIds[work.row] : kInvalidPassId;
      if (id == kInvalidPassId) { ++work.row; continue; }
      if (work.passCount == work.passIds->capacity()) { work.phase = Phase::Denied; break; }
      work.passIds->push_back(id); ++work.passCount;
      work.companion = 0; work.phase = Phase::Companions;
    } else if (work.phase == Phase::Companions) {
      if (work.row >= sessionCompanionRowCount ||
          work.companion == sessionCompanionRows[work.row].size()) {
        ++work.row; work.phase = Phase::Pass;
      } else {
        if (work.companionCount == 2048 || work.companionCount == work.companionIds->capacity()) {
          work.phase = Phase::Denied; break;
        }
        work.companionIds->push_back(sessionCompanionRows[work.row][work.companion++]);
        ++work.companionCount;
      }
    } else if (work.phase == Phase::FinalPass) {
      if (work.includeFinalPass && work.finalPassId != kInvalidPassId &&
          (work.passIds->empty() || work.passIds->back() != work.finalPassId)) {
        if (work.passCount == 2048 || work.passCount == work.passIds->capacity()) {
          work.phase = Phase::Denied; break;
        }
        work.passIds->push_back(work.finalPassId); ++work.passCount;
      }
      work.phase = Phase::FinalCompanions;
    } else {
      if (work.receiptIndex == work.receiptCount) { work.phase = Phase::Complete; continue; }
      if (work.companionCount == 2048 || work.companionCount == work.companionIds->capacity()) {
        work.phase = Phase::Denied; break;
      }
      work.companionIds->push_back(preparedCompanionHistory_.companionIds[work.receiptIndex++]);
      ++work.companionCount;
    }
  }
  return work.phase;
}

LOOP_COLD_MEM void Loop::dropOverdubSessionRedoTail() {
  if (overdubSessionCursor_ < overdubSessionPassIds_.size()) {
    ++overdubSessionUndoGeneration_;
    overdubSessionPassIds_.resize(overdubSessionCursor_);
    overdubSessionCompanionIds_.resize(overdubSessionCursor_);
  }
  overdubSessionLiveUndoEvents_.clear();
}

LOOP_COLD_MEM bool Loop::canUndoOverdubSession() const {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ > 0) {
    return true;
  }
  return capture.phase == CapturePhase::Overdub && !capture.store.empty();
}

LOOP_COLD_MEM bool Loop::canRedoOverdubSession() const {
  if (!hasOverdubSession()) {
    return false;
  }
  if (capture.store.empty() && !overdubSessionLiveUndoEvents_.empty()) {
    return true;
  }
  return overdubSessionCursor_ < overdubSessionPassIds_.size();
}

LOOP_COLD_MEM size_t Loop::overdubSessionUndoDepth() const {
  return overdubSessionCursor_;
}

LOOP_COLD_MEM size_t Loop::overdubSessionDisplayDepth() const {
  if (!hasOverdubSession()) {
    return 0;
  }
  return overdubSessionUndoDepth() + 1;
}

LOOP_COLD_MEM size_t Loop::overdubSessionRedoDepth() const {
  size_t depth = overdubSessionPassIds_.size() - overdubSessionCursor_;
  if (capture.store.empty() && !overdubSessionLiveUndoEvents_.empty()) {
    ++depth;
  }
  return depth;
}

LOOP_COLD_MEM bool Loop::undoOverdubSession() {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ > 0) {
    clearLastOverdubSeal();
    ++overdubSessionUndoGeneration_;
    --overdubSessionCursor_;
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    disableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Disabled);
    if (ok && hasOverdubSourceView()) {
      rebuildOverdubSourceView(playheadPhaseTick, "undo");
    }
    return ok;
  }
  if (capture.phase == CapturePhase::Overdub && !capture.store.empty()) {
    overdubSessionLiveUndoEvents_.clear();
    capture.store.copyEventsTo(overdubSessionLiveUndoEvents_);
    capture.store.clear();
    captureEventsSortDirty = false;
    rebuildCapturePreviewFromStore(*this);
    ++captureDisplayRevision;
    suppressNextOverdubWrap();
    return true;
  }
  return false;
}

LOOP_COLD_MEM bool Loop::redoOverdubSession() {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ < overdubSessionPassIds_.size()) {
    clearLastOverdubSeal();
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    enableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Active);
    if (ok) {
      ++overdubSessionUndoGeneration_;
      ++overdubSessionCursor_;
      if (hasOverdubSourceView()) {
        rebuildOverdubSourceView(playheadPhaseTick, "redo");
      }
    }
    return ok;
  }
  if (capture.store.empty() && !overdubSessionLiveUndoEvents_.empty()) {
    capture.store.loadFromEvents(overdubSessionLiveUndoEvents_);
    overdubSessionLiveUndoEvents_.clear();
    captureEventsSortDirty = false;
    rebuildCapturePreviewFromStore(*this);
    ++captureDisplayRevision;
    return true;
  }
  return false;
}
