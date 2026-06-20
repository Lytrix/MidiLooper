//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
#include "Utils/LoopStopFinalize.h"
#include <algorithm>

namespace {

bool eventsEquivalent(const MidiEvent& a, const MidiEvent& b) {
  if (a.type != b.type || a.channel != b.channel || a.tick != b.tick) {
    return false;
  }
  if (a.type == midi::NoteOn || a.type == midi::NoteOff) {
    return a.data.noteData.note == b.data.noteData.note &&
           a.data.noteData.velocity == b.data.noteData.velocity;
  }
  if (a.type == midi::ControlChange) {
    return a.data.ccData.cc == b.data.ccData.cc &&
           a.data.ccData.value == b.data.ccData.value;
  }
  return true;
}

bool isDuplicateCaptureEvent(const Loop& loop, const MidiEvent& candidate) {
  const uint32_t lo = (candidate.tick > Config::DUPLICATE_TICK_TOLERANCE)
                          ? (candidate.tick - Config::DUPLICATE_TICK_TOLERANCE)
                          : 0;
  const uint32_t hi = candidate.tick + Config::DUPLICATE_TICK_TOLERANCE;

  const size_t captureCount = loop.capture.store.size();
  for (size_t i = captureCount; i > 0; --i) {
    const MidiEvent& evt = loop.capture.store.at(i - 1);
    if (evt.tick < lo) {
      break;
    }
    if (evt.tick > hi) {
      continue;
    }
    if (eventsEquivalent(evt, candidate)) {
      return true;
    }
  }
  return false;
}

void sortCaptureStoreByTick(LoopEventStore& store) {
  MidiEventVec sorted;
  store.flatten(sorted);
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  store.clear();
  store.loadFromFlat(sorted);
}

void markPreviewSpan(CapturePreview& preview, uint32_t startTick, uint32_t endTick, uint32_t ticksPerBar) {
  if (ticksPerBar == 0) {
    return;
  }
  const uint32_t startBar = startTick / ticksPerBar;
  const uint32_t endBar = endTick / ticksPerBar;
  for (uint32_t bar = startBar; bar <= endBar; ++bar) {
    preview.markBarDirty(bar);
  }
}

void applyCaptureEventToPreview(CapturePreview& preview, const MidiEvent& evt, uint32_t ticksPerBar) {
  if (evt.isNoteOn()) {
    NoteUtils::DisplayNote note{};
    note.note = evt.data.noteData.note;
    note.velocity = evt.data.noteData.velocity;
    note.startTick = evt.tick;
    note.endTick = evt.tick;
    preview.notes.push_back(note);
    markPreviewSpan(preview, evt.tick, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }

  if (!evt.isNoteOff()) {
    return;
  }

  for (auto it = preview.notes.rbegin(); it != preview.notes.rend(); ++it) {
    if (it->note != evt.data.noteData.note) {
      continue;
    }
    if (it->endTick != it->startTick) {
      continue;
    }
    const uint32_t start = it->startTick;
    it->endTick = evt.tick;
    markPreviewSpan(preview, start, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }
}

void rebuildCapturePreviewFromStore(Loop& loop) {
  MidiEventVec flat;
  loop.capture.store.flatten(flat);
  loop.capturePreview.notes = NoteUtils::reconstructNotes(flat, loop.loopLengthTicks, false);
  loop.capturePreview.dirtyBars.clear();
  if (loop.loopLengthTicks > 0) {
    for (const auto& note : loop.capturePreview.notes) {
      const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
      markPreviewSpan(loop.capturePreview, note.startTick, endTick, Config::TICKS_PER_BAR);
    }
  }
  ++loop.capturePreview.revision;
}

}  // namespace

bool Loop::hasPublishedEvents() const {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.chunkRefs.empty()) {
    return true;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.chunkRefs.empty()) {
      return true;
    }
  }
  return false;
}

void Loop::flattenActiveCapturePasses(MidiEventVec& out) const {
  out.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.chunkRefs.empty()) {
    LoopEventStore::appendFlattenedChunkIds(passes.recordPass.chunkRefs, out);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.chunkRefs.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    MidiEventVec layer;
    LoopEventStore::appendFlattenedChunkIds(pass->chunkRefs, layer);
    if (out.empty()) {
      out = std::move(layer);
      continue;
    }
    if (layer.empty()) {
      continue;
    }
    MidiEventVec merged;
    merged.reserve(out.size() + layer.size());
    std::merge(out.begin(), out.end(), layer.begin(), layer.end(),
               std::back_inserter(merged),
               [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    out = std::move(merged);
  }
}

void Loop::materializeEditViewFromPasses() const {
  Loop* self = const_cast<Loop*>(this);
  const bool storeEmptyPublished =
      self->hasPublishedEvents() && self->editFlat_.readStore().empty() &&
      self->passes.editPasses.empty();
  if (!editFlatStale_ && !storeEmptyPublished) {
    return;
  }
  passes.materialize(self->editFlat_.mutStore(), self->loopLengthTicks);
  self->editFlat_.discardFlatCache();
  self->editFlatStale_ = false;
}

void Loop::rematerializeEditView(LoopEventStore& store) const {
  passes.materialize(store, loopLengthTicks);
}

EditPassId Loop::saveNoteEditPass(uint8_t noteEditPassIndex, EditChangeList changes) {
  if (changes.empty()) {
    return kInvalidEditPassId;
  }
  EditPass editPass;
  editPass.id = nextPassId_++;
  editPass.kind = EditPassKind::NoteEdit;
  editPass.noteEditPassIndex = noteEditPassIndex;
  editPass.state = EditPassState::Active;
  editPass.changes = std::move(changes);
  passes.editPasses.push_back(std::move(editPass));
  ++playbackRevision;
  editStateDirty_ = true;
  markPassDerivedStale();
  return passes.editPasses.back().id;
}

void Loop::disableEditPasses(const EditPassIdList& ids) {
  for (const EditPassId id : ids) {
    for (EditPass& editPass : passes.editPasses) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
      }
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::freeActiveCapturePassChunks() {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    LoopEventStore staging;
    staging.adoptChunkIds(passes.recordPass.chunkRefs);
    staging.clear();
    passes.recordPass.chunkRefs.clear();
    passes.recordPass.id = kInvalidPassId;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active) {
      continue;
    }
    LoopEventStore staging;
    staging.adoptChunkIds(pass.chunkRefs);
    staging.clear();
    pass.chunkRefs.clear();
  }
  passes.overdubPasses.erase(
      std::remove_if(passes.overdubPasses.begin(), passes.overdubPasses.end(),
                     [](const OverdubPass& pass) {
                       return pass.state == CapturePassState::Active;
                     }),
      passes.overdubPasses.end());
}

void Loop::commitMaterializedStoreImpl(bool allowEmptyClear) {
  if (editFlat_.isFlatDirty()) {
    editFlat_.syncFlatToStore();
  }

  LoopEventStore& store = editFlat_.mutStore();
  if (store.empty()) {
    if (!allowEmptyClear && hasPublishedEvents()) {
      discardEditFlatMaterialization();
      return;
    }
    freeActiveCapturePassChunks();
    lastPublishedPassId_ = kInvalidPassId;
    ++playbackRevision;
    discardEditFlatMaterialization();
    rebuildVisualCacheFromPasses();
    return;
  }

  ChunkIdList refs;
  store.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveCapturePassChunks();

  RecordPass rebuilt{};
  rebuilt.id = nextPassId_++;
  rebuilt.state = CapturePassState::Active;
  rebuilt.chunkRefs = std::move(refs);
  passes.recordPass = rebuilt;
  lastPublishedPassId_ = rebuilt.id;
  ++playbackRevision;
  materializeEditViewFromPasses();
  rebuildVisualCacheFromPasses();
}

void Loop::markPassDerivedStale() {
  editFlatStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

MidiEventVec& Loop::midiEvents() {
  materializeEditViewFromPasses();
  return editFlat_.mutFlat();
}

const MidiEventVec& Loop::midiEvents() const {
  materializeEditViewFromPasses();
  return editFlat_.readFlat();
}

LoopEventStore& Loop::mutEditStore() {
  materializeEditViewFromPasses();
  return editFlat_.mutStore();
}

const LoopEventStore& Loop::readEditStore() const {
  materializeEditViewFromPasses();
  return editFlat_.readStore();
}

std::shared_ptr<const LoopEventStore> Loop::shareEditSnapshot() const {
  Loop* self = const_cast<Loop*>(this);
  self->materializeEditViewFromPasses();
  if (self->editFlat_.isFlatDirty()) {
    self->editFlat_.syncFlatToStore();
  }
  return editFlat_.shareForSnapshot();
}

void Loop::restoreEditSnapshot(const MidiSnapshotRef& snapshot) {
  editFlat_.restoreFromSnapshot(snapshot);
  passes.editPasses.clear();
  commitMaterializedStoreImpl(true);
}

void Loop::discardEditFlatMaterialization() {
  editFlat_.mutStore().clear();
  editFlat_.discardFlatCache();
  editFlatStale_ = true;
}

void Loop::commitStopFinalizeFromStore(LoopEventStore& merged) {
  const PassId preserveId = lastPublishedPassId_;
  CapturePassPhase preservePhase = CapturePassPhase::Overdub;
  uint32_t preserveMergeSeq = 0;
  if (passes.hasRecordPass() && passes.recordPass.id == preserveId) {
    preservePhase = CapturePassPhase::Record;
  } else {
    for (const OverdubPass& pass : passes.overdubPasses) {
      if (pass.state == CapturePassState::Active && pass.id == preserveId) {
        preserveMergeSeq = pass.mergeSequence;
        break;
      }
    }
  }

  ChunkIdList refs;
  merged.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveCapturePassChunks();

  if (preservePhase == CapturePassPhase::Record) {
    RecordPass rebuilt{};
    rebuilt.id = (preserveId != kInvalidPassId) ? preserveId : nextPassId_++;
    if (rebuilt.id >= nextPassId_) {
      nextPassId_ = rebuilt.id + 1;
    }
    rebuilt.state = CapturePassState::Active;
    rebuilt.chunkRefs = std::move(refs);
    passes.recordPass = rebuilt;
    lastPublishedPassId_ = rebuilt.id;
  } else {
    OverdubPass rebuilt{};
    rebuilt.id = (preserveId != kInvalidPassId) ? preserveId : nextPassId_++;
    if (rebuilt.id >= nextPassId_) {
      nextPassId_ = rebuilt.id + 1;
    }
    rebuilt.mergeSequence = preserveMergeSeq;
    rebuilt.state = CapturePassState::Active;
    rebuilt.chunkRefs = std::move(refs);
    passes.overdubPasses.push_back(rebuilt);
    lastPublishedPassId_ = rebuilt.id;
  }

  ++playbackRevision;
  discardEditFlatMaterialization();
  rebuildVisualCacheFromPasses();
}

void Loop::importPublishedStore(LoopEventStore& store) {
  resetPassTimeline();
  if (store.empty()) {
    return;
  }
  editFlat_.mutStore().adoptAll(store);
  editFlatStale_ = false;
  commitMaterializedStoreImpl(true);
  rebuildVisualCacheFromPasses();
}

void Loop::shiftActiveCapturePassTicks(int64_t delta) {
  if (delta == 0 || !hasPublishedEvents()) {
    return;
  }
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.chunkRefs.empty()) {
    MidiEventVec flat;
    LoopEventStore::appendFlattenedChunkIds(passes.recordPass.chunkRefs, flat);
    if (!flat.empty()) {
      LoopEventStore staging;
      staging.loadFromFlat(flat);
      staging.shiftAllTicks(delta);
      LoopEventStore temp;
      temp.adoptAll(staging);
      passes.recordPass.chunkRefs.clear();
      temp.detachChunksTo(passes.recordPass.chunkRefs);
    }
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.chunkRefs.empty()) {
      continue;
    }
    MidiEventVec flat;
    LoopEventStore::appendFlattenedChunkIds(pass.chunkRefs, flat);
    if (flat.empty()) {
      continue;
    }
    LoopEventStore staging;
    staging.loadFromFlat(flat);
    staging.shiftAllTicks(delta);
    LoopEventStore temp;
    temp.adoptAll(staging);
    pass.chunkRefs.clear();
    temp.detachChunksTo(pass.chunkRefs);
  }
  for (EditPass& editPass : passes.editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    for (EditChange& change : editPass.changes) {
      change.target.startTick =
          static_cast<uint32_t>(static_cast<int64_t>(change.target.startTick) + delta);
      change.target.endTick =
          static_cast<uint32_t>(static_cast<int64_t>(change.target.endTick) + delta);
      change.newStartTick =
          static_cast<uint32_t>(static_cast<int64_t>(change.newStartTick) + delta);
      change.newEndTick =
          static_cast<uint32_t>(static_cast<int64_t>(change.newEndTick) + delta);
      for (MidiEvent& evt : change.addedEvents) {
        evt.tick = static_cast<uint32_t>(static_cast<int64_t>(evt.tick) + delta);
      }
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::beginCapture(CapturePhase phase) {
  discardPendingCapturePass();
  capture.phase = phase;
  capture.store.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
}

void Loop::discardCapture() {
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();
}

bool Loop::appendCaptureEvent(const MidiEvent& evt) {
  if (capture.phase == CapturePhase::None) {
    return false;
  }
  if (hasPendingCapturePass_) {
    return false;
  }
  if (isDuplicateCaptureEvent(*this, evt)) {
    return false;
  }
  if (!capture.store.append(evt)) {
    return false;
  }
  captureEventsSortDirty = true;
  applyCaptureEventToPreview(capturePreview, evt, Config::TICKS_PER_BAR);
  return true;
}

size_t Loop::liveEventCount() const {
  MidiEventVec flat;
  passes.materializeToFlat(flat, loopLengthTicks);
  size_t count = flat.size();
  if (captureActive()) {
    count += capture.store.size();
  }
  return count;
}

bool Loop::captureActive() const {
  return capture.phase != CapturePhase::None;
}

bool Loop::ensureCaptureEventsSorted() {
  if (!captureEventsSortDirty) {
    return false;
  }
  sortCaptureStoreByTick(capture.store);
  captureEventsSortDirty = false;
  return true;
}

void Loop::buildLiveEventView(MidiEventVec& out) const {
  passes.materializeToFlat(out, loopLengthTicks);
  if (!captureActive() || capture.store.empty()) {
    return;
  }
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();

  MidiEventVec captureFlat;
  capture.store.flatten(captureFlat);
  if (out.empty()) {
    out = std::move(captureFlat);
    return;
  }
  if (captureFlat.empty()) {
    return;
  }

  MidiEventVec merged;
  merged.reserve(out.size() + captureFlat.size());
  std::merge(out.begin(), out.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

void Loop::removeCaptureNoteOffAt(uint8_t channel, uint8_t note, uint32_t tick) {
  if (capture.store.empty()) {
    return;
  }

  MidiEventVec flat;
  capture.store.flatten(flat);
  bool removed = false;
  for (auto it = flat.begin(); it != flat.end(); ++it) {
    if (!it->isNoteOff() || it->channel != channel || it->data.noteData.note != note ||
        it->tick != tick) {
      continue;
    }
    flat.erase(it);
    removed = true;
    break;
  }

  if (!removed) {
    return;
  }

  capture.store.clear();
  if (!flat.empty()) {
    capture.store.loadFromFlat(flat);
  }
  captureEventsSortDirty = false;
  ++captureDisplayRevision;
  rebuildCapturePreviewFromStore(*this);
}

void Loop::resetPassTimeline() {
  discardPendingCapturePass();
  if (passes.hasRecordPass()) {
    LoopEventStore staging;
    staging.adoptChunkIds(passes.recordPass.chunkRefs);
    staging.clear();
    passes.recordPass = RecordPass{};
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    LoopEventStore staging;
    staging.adoptChunkIds(pass.chunkRefs);
    staging.clear();
  }
  passes.overdubPasses.clear();
  passes.editPasses.clear();
  nextPassId_ = 1;
  nextMergeSequence_ = 0;
  lastPublishedPassId_ = kInvalidPassId;
  playbackRevision = 0;
  editStateDirty_ = false;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  editFlat_.mutStore().clear();
  editFlat_.discardFlatCache();
  editFlatStale_ = true;
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

CommitResult Loop::commitCapturePass(CommitReason reason, uint32_t sealedAtTick) {
  (void)reason;
  if (capture.store.empty()) {
    discardCapture();
    return CommitResult::Skipped;
  }

  const SealOutcome seal = sealCapture(sealedAtTick);
  if (seal != SealOutcome::Ok) {
    return CommitResult::SealFailed;
  }

  if (!publishPendingCapturePass()) {
    return CommitResult::SealFailed;
  }

  markPassDerivedStale();
  rebuildVisualCacheFromPasses();
  return CommitResult::Published;
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

void Loop::discardPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return;
  }
  LoopEventStore staging;
  staging.adoptChunkIds(pendingCapturePass_.chunkRefs);
  staging.clear();
  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;
  pendingVisualDelta.clear();
}

void Loop::rebuildVisualCacheFromPasses() {
  MidiEventVec flat;
  flattenActiveCapturePasses(flat);
  visualCache.notes = NoteUtils::reconstructNotes(flat, loopLengthTicks, false);
  visualCache.dirtyBars.clear();
  for (const auto& n : visualCache.notes) {
    const uint32_t endTick = n.endTick >= n.startTick ? n.endTick : n.startTick;
    const uint32_t startBar = visualBarForTick(n.startTick, Config::TICKS_PER_BAR);
    const uint32_t endBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    for (uint32_t bar = startBar; bar <= endBar; ++bar) {
      visualCache.markBarDirty(bar);
    }
  }
  ++visualCache.revision;
  visualCacheDirty = false;
}

void Loop::ensureVisualCacheBuilt() {
  if (!visualCacheDirty) {
    return;
  }
  rebuildVisualCacheFromPasses();
}

void Loop::markDisplayCachesStale() {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
}

SealOutcome Loop::sealCapture(uint32_t sealedAtTick) {
  if (hasPendingCapturePass_) {
    return SealOutcome::AlreadyPending;
  }
  if (capture.store.empty()) {
    return SealOutcome::SkippedEmpty;
  }
  if (passes.capturePassCount() >= PassConfig::MAX_CAPTURE_PASSES_PER_LOOP) {
    return SealOutcome::AtPassCap;
  }

  ensureCaptureEventsSorted();

  if (loopLengthTicks > 0 && capture.phase == CapturePhase::Record) {
    const LoopStopFinalize::Result fin =
        LoopStopFinalize::finalizeWrapWindowOnStore(capture.store, loopLengthTicks);
    (void)fin;
    if (capture.store.empty()) {
      return SealOutcome::FailedValidation;
    }
  }

  const CapturePassPhase phase =
      effectiveCapturePassPhase(capture.phase, passes.hasRecordPass());

  pendingCapturePass_ = PendingCapturePass{};
  pendingCapturePass_.id = nextPassId_++;
  pendingCapturePass_.mergeSequence = nextMergeSequence_++;
  pendingCapturePass_.phase = phase;
  pendingCapturePass_.sealedAtTick = sealedAtTick;
  capture.store.detachChunksTo(pendingCapturePass_.chunkRefs);

  if (pendingCapturePass_.chunkRefs.empty()) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  pendingVisualDelta.clear();
  hasPendingCapturePass_ = true;
  return SealOutcome::Ok;
}

bool Loop::publishPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return false;
  }

  PendingCapturePass published = std::move(pendingCapturePass_);
  if (published.phase == CapturePassPhase::Record) {
    RecordPass record{};
    record.id = published.id;
    record.state = CapturePassState::Active;
    record.sealedAtTick = published.sealedAtTick;
    record.chunkRefs = std::move(published.chunkRefs);
    passes.recordPass = std::move(record);
  } else {
    OverdubPass overdub{};
    overdub.id = published.id;
    overdub.mergeSequence = published.mergeSequence;
    overdub.state = CapturePassState::Active;
    overdub.sealedAtTick = published.sealedAtTick;
    overdub.chunkRefs = std::move(published.chunkRefs);
    passes.overdubPasses.push_back(std::move(overdub));
  }
  lastPublishedPassId_ = published.id;

  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;

  ++playbackRevision;
  pendingVisualDelta.clear();

  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();

  return true;
}

void Loop::invalidateCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
  visualCacheDirty = true;
  playbackOrderDirty = true;
}

void Loop::invalidatePlaybackCaches() {
  playbackOrderDirty = true;
  editFlat_.discardFlatCache();
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
}

void Loop::clearCaptureOnNewPass() {
  discardCapture();
}
