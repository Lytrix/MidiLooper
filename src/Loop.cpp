//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
#include "Globals.h"
#include "Logger.h"
#include <algorithm>

bool Loop::hasCommittedPasses() const {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    return true;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      return true;
    }
  }
  return false;
}

uint32_t Loop::findLastCommittedEventTick() const {
  uint32_t lastTick = 0;
  // Use chunk lastTick metadata only — never copyEventsTo into MidiEventVec (InternalHeap).
  // Adopt/reconcile during focus LoadLoopJob Commit while PLAYING otherwise abort()s
  // when RAM1 cannot hold a 256-event scratch (session_20260718_235129: silence after
  // parse edits grain, before apply_us).
  auto scanChunkRefs = [&lastTick](const CommittedChunkIdList& committedChunkIds) {
    for (uint16_t chunkId : committedChunkIds) {
      uint32_t firstTick = 0;
      uint32_t chunkLast = 0;
      if (!LoopEventStore::chunkTickSpan(chunkId, firstTick, chunkLast)) {
        continue;
      }
      (void)firstTick;
      if (chunkLast > lastTick) {
        lastTick = chunkLast;
      }
    }
  };

  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    scanChunkRefs(passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      scanChunkRefs(pass.committedChunkIds);
    }
  }
  return lastTick;
}

uint32_t Loop::reconcileLoopLengthWithCommittedPasses(uint32_t candidateLengthTicks) const {
  if (!hasCommittedPasses()) {
    return candidateLengthTicks;
  }
  const uint32_t lastEventTick = findLastCommittedEventTick();
  if (lastEventTick == 0) {
    return candidateLengthTicks;
  }

  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t fullBars = lastEventTick / ticksPerBar;
  const uint32_t rem = lastEventTick % ticksPerBar;
  const uint32_t grace = ticksPerBar / 6;
  uint32_t contentLength = 0;
  if (rem <= grace) {
    contentLength = (fullBars > 0 ? fullBars : 1) * ticksPerBar;
  } else if (lastEventTick < ticksPerBar / 2) {
    contentLength = ticksPerBar;
  } else {
    contentLength = (fullBars + 1) * ticksPerBar;
  }

  if (candidateLengthTicks == 0 || candidateLengthTicks < contentLength) {
    return contentLength;
  }
  return candidateLengthTicks;
}

void Loop::freeActiveCapturePassChunks() {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass.committedChunkIds.clear();
    passes.recordPass.id = kInvalidPassId;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active) {
      continue;
    }
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
    pass.committedChunkIds.clear();
  }
  passes.overdubPasses.erase(
      std::remove_if(passes.overdubPasses.begin(), passes.overdubPasses.end(),
                     [](const OverdubPass& pass) {
                       return pass.state == CapturePassState::Active;
                     }),
      passes.overdubPasses.end());
}

bool Loop::reclaimDisabledCapturePass(PassId id) {
  if (id == kInvalidPassId) {
    return false;
  }
  if (passes.hasRecordPass() && passes.recordPass.id == id &&
      passes.recordPass.state == CapturePassState::Disabled) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass.committedChunkIds.clear();
    passes.recordPass.id = kInvalidPassId;
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  for (auto it = passes.overdubPasses.begin(); it != passes.overdubPasses.end(); ++it) {
    if (it->id != id || it->state != CapturePassState::Disabled) {
      continue;
    }
    LoopEventStore::releaseChunkRefs(it->committedChunkIds);
    passes.overdubPasses.erase(it);
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  return false;
}

void Loop::reclaimUnreferencedDisabledCapturePasses(const SlotPassReferences& refs) {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Disabled &&
      !refs.referencesCapturePass(passes.recordPass.id)) {
    reclaimDisabledCapturePass(passes.recordPass.id);
  }
  for (size_t i = passes.overdubPasses.size(); i > 0; --i) {
    const OverdubPass& pass = passes.overdubPasses[i - 1];
    if (pass.state == CapturePassState::Disabled && !refs.referencesCapturePass(pass.id)) {
      reclaimDisabledCapturePass(pass.id);
    }
  }
}

void Loop::reclaimUnreferencedDisabledEditPasses(const SlotPassReferences& refs) {
  passes.editPasses.erase(
      std::remove_if(passes.editPasses.begin(), passes.editPasses.end(),
                     [&](const EditPass& editPass) {
                       return editPass.state == EditPassState::Disabled &&
                              !refs.referencesEditPass(editPass.id);
                     }),
      passes.editPasses.end());
}

void Loop::reclaimUnreferencedDisabledPasses(const SlotPassReferences& refs) {
  reclaimUnreferencedDisabledCapturePasses(refs);
  reclaimUnreferencedDisabledEditPasses(refs);
}

void Loop::markPassDerivedStale() {
  passesMaterializedStoreStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

NoteId Loop::allocateNoteId() {
  return nextNoteId_++;
}

void Loop::assignMissingNoteIds(SessionMidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIds(MidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIdsInStore(LoopEventStore& store) {
  store.assignMissingNoteIdsToNoteOns([this]() { return allocateNoteId(); });
}

void Loop::resetPassTimeline() {
  discardPendingCapturePass();
  if (passes.hasRecordPass()) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass = RecordPass{};
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
  }
  passes.overdubPasses.clear();
  passes.editPasses.clear();
  nextPassId_ = 1;
  nextNoteId_ = 1;
  nextMergeSequence_ = 0;
  lastCommittedPassId_ = kInvalidPassId;
  playbackRevision = 0;
  editStateDirty_ = false;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardEventsCache();
  passesMaterializedStoreStale_ = true;
}

bool Loop::setCapturePassState(PassId id, CapturePassState state) {
  if (passes.hasRecordPass() && passes.recordPass.id == id) {
    if (passes.recordPass.state == state) {
      return true;
    }
    passes.recordPass.state = state;
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.id != id) {
      continue;
    }
    if (pass.state == state) {
      return true;
    }
    pass.state = state;
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  return false;
}

size_t Loop::activeCapturePassCount() const {
  size_t count = 0;
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    ++count;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      ++count;
    }
  }
  return count;
}

void Loop::invalidateCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
  visualCacheDirty = true;
  playbackOrderDirty = true;
  passesMaterializedStoreStale_ = true;
}

void Loop::invalidatePlaybackCaches() {
  playbackOrderDirty = true;
  passesMaterializedStore_.discardEventsCache();
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
}
