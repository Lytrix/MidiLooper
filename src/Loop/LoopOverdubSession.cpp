//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"

LOOP_COLD_MEM void Loop::openOverdubSession(uint32_t sessionPlayheadPhaseTick) {
  if (hasOverdubSession()) {
    return;
  }
  playheadPhaseTick = sessionPlayheadPhaseTick;
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
  overdubSessionPassIds_.clear();
  overdubSessionCompanionIds_.clear();
  overdubSessionCursor_ = 0;
  overdubSessionLiveUndoEvents_.clear();
}

LOOP_COLD_MEM void Loop::closeOverdubSession() {
  playheadPhaseTick = UINT32_MAX;
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
  overdubSessionPassIds_.clear();
  overdubSessionCompanionIds_.clear();
  overdubSessionCursor_ = 0;
  overdubSessionLiveUndoEvents_.clear();
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
  return IntervalProjection::didPlayheadCrossPhase(prevPhase, currentPhase, playheadPhaseTick,
                                                   loopLengthTicks);
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
  if (!IntervalProjection::didPlayheadCrossPhase(prevPhase, currentPhase, playheadPhaseTick,
                                                 loopLengthTicks)) {
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
  // Append order, not tick order: wrap-held On@tail + Off@0 must pair before a
  // later same-pitch On in the same wrap (012925 note 30 @ 2976 / 0 / 672).
  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  if (flat.empty()) {
    return 0;
  }
  const std::vector<NoteUtils::OpenNoteOn> opens =
      NoteUtils::findOpenNoteOns(flat, loopLengthTicks);
  if (opens.empty()) {
    return 0;
  }
  SessionMidiEventVec kept;
  kept.reserve(flat.size());
  for (size_t eventIndex = 0; eventIndex < flat.size(); ++eventIndex) {
    const MidiEvent& evt = flat[eventIndex];
    bool extract = false;
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      for (const NoteUtils::OpenNoteOn& open : opens) {
        // Index, not pitch+tick: the same grid can hold a completed pair and a
        // later held ON (012925 note 30 @ 2880 every wrap).
        if (open.eventIndex == eventIndex) {
          extract = true;
          break;
        }
      }
    }
    if (extract) {
      out.push_back(evt);
    } else {
      kept.push_back(evt);
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
  overdubSessionPassIds_.push_back(passId);
  overdubSessionCompanionIds_.push_back(std::move(companionIds));
  overdubSessionCursor_ = overdubSessionPassIds_.size();
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

LOOP_COLD_MEM void Loop::dropOverdubSessionRedoTail() {
  if (overdubSessionCursor_ < overdubSessionPassIds_.size()) {
    overdubSessionPassIds_.resize(overdubSessionCursor_);
    overdubSessionCompanionIds_.resize(overdubSessionCursor_);
  }
  overdubSessionLiveUndoEvents_.clear();
}

LOOP_COLD_MEM bool Loop::canUndoOverdubSession() const {
  if (!hasOverdubSession()) {
    return false;
  }
  if (capture.phase == CapturePhase::Overdub && !capture.store.empty()) {
    return true;
  }
  return overdubSessionCursor_ > 0;
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
  size_t depth = overdubSessionCursor_;
  if (capture.phase == CapturePhase::Overdub && !capture.store.empty()) {
    ++depth;
  }
  return depth;
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
  if (overdubSessionCursor_ > 0) {
    --overdubSessionCursor_;
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    disableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Disabled);
    suppressNextOverdubWrap();
    return ok;
  }
  return false;
}

LOOP_COLD_MEM bool Loop::redoOverdubSession() {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ < overdubSessionPassIds_.size()) {
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    enableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Active);
    if (ok) {
      ++overdubSessionCursor_;
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
