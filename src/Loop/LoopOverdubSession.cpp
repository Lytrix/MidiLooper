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
  clearOverdubSourceView();
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
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    enableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Active);
    if (ok) {
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
