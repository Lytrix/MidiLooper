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

void collectActiveTakesSorted(const Loop& loop, std::vector<const Take*>& out) {
  out.clear();
  out.reserve(loop.takes.size());
  for (const Take& take : loop.takes) {
    if (take.state == TakeState::Active && !take.chunkRefs.empty()) {
      out.push_back(&take);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Take* a, const Take* b) { return a->mergeSequence < b->mergeSequence; });
}

/// Merge Active take chunk data into a flat vector without allocating new pool chunks.
void flattenActiveTakeChunksToVec(const std::vector<const Take*>& active, MidiEventVec& out) {
  out.clear();
  if (active.empty()) {
    return;
  }

  for (const Take* take : active) {
    MidiEventVec takeFlat;
    LoopEventStore::appendFlattenedChunkIds(take->chunkRefs, takeFlat);
    if (takeFlat.empty()) {
      continue;
    }
    if (out.empty()) {
      out = std::move(takeFlat);
      continue;
    }
    MidiEventVec merged;
    merged.reserve(out.size() + takeFlat.size());
    std::merge(out.begin(), out.end(), takeFlat.begin(), takeFlat.end(),
               std::back_inserter(merged),
               [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    out = std::move(merged);
  }
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

  // Overdub layers new material on published takes. Only dedupe within the active
  // capture buffer so a second loop pass (or automation re-hits) is not blocked
  // unless the same capture session already stored an identical event.
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
  for (const Take& take : takes) {
    if (take.state == TakeState::Active && !take.chunkRefs.empty()) {
      return true;
    }
  }
  return false;
}

void Loop::flattenActiveTakes(MidiEventVec& out) const {
  std::vector<const Take*> active;
  collectActiveTakesSorted(*this, active);
  flattenActiveTakeChunksToVec(active, out);
}

void Loop::materializeEditFlatFromTakes() const {
  Loop* self = const_cast<Loop*>(this);
  const bool storeEmptyPublished =
      self->hasPublishedEvents() && self->editFlat_.readStore().empty();
  if (!editFlatStale_ && !storeEmptyPublished) {
    return;
  }
  MidiEventVec flat;
  self->flattenActiveTakes(flat);
  self->editFlat_.mutStore().clear();
  if (!flat.empty()) {
    self->editFlat_.mutStore().loadFromFlat(flat);
  }
  self->editFlat_.discardFlatCache();
  self->editFlatStale_ = false;
}

void Loop::freeActiveTakeChunks() {
  for (Take& take : takes) {
    if (take.state != TakeState::Active) {
      continue;
    }
    LoopEventStore staging;
    staging.adoptChunkIds(take.chunkRefs);
    staging.clear();
    take.chunkRefs.clear();
  }
}

void Loop::syncEditFlatToTakes(bool allowEmptyClear) {
  if (editFlat_.isFlatDirty()) {
    editFlat_.syncFlatToStore();
  }

  LoopEventStore& store = editFlat_.mutStore();
  if (store.empty()) {
    if (!allowEmptyClear && hasPublishedEvents()) {
      discardEditFlatMaterialization();
      return;
    }
    freeActiveTakeChunks();
    takes.erase(std::remove_if(takes.begin(), takes.end(),
                                [](const Take& e) { return e.state == TakeState::Active; }),
                 takes.end());
    lastPublishedTakeId_ = kInvalidTakeId;
    ++playbackRevision;
    discardEditFlatMaterialization();
    rebuildVisualCacheFromTakes();
    return;
  }

  ChunkIdList refs;
  store.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveTakeChunks();
  takes.erase(std::remove_if(takes.begin(), takes.end(),
                              [](const Take& e) { return e.state == TakeState::Active; }),
               takes.end());

  Take rebuilt{};
  rebuilt.id = nextTakeId_++;
  rebuilt.mergeSequence = nextMergeSequence_++;
  rebuilt.state = TakeState::Active;
  rebuilt.type = TakeType::Record;
  rebuilt.chunkRefs = std::move(refs);
  takes.push_back(rebuilt);
  lastPublishedTakeId_ = rebuilt.id;
  ++playbackRevision;
  materializeEditFlatFromTakes();
  rebuildVisualCacheFromTakes();
}

void Loop::markTakeDerivedStale() {
  editFlatStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

MidiEventVec& Loop::midiEvents() {
  materializeEditFlatFromTakes();
  return editFlat_.mutFlat();
}

const MidiEventVec& Loop::midiEvents() const {
  materializeEditFlatFromTakes();
  return editFlat_.readFlat();
}

LoopEventStore& Loop::mutEditStore() {
  materializeEditFlatFromTakes();
  return editFlat_.mutStore();
}

const LoopEventStore& Loop::readEditStore() const {
  materializeEditFlatFromTakes();
  return editFlat_.readStore();
}

std::shared_ptr<const LoopEventStore> Loop::shareEditSnapshot() const {
  materializeEditFlatFromTakes();
  return editFlat_.shareForSnapshot();
}

void Loop::restoreEditSnapshot(const MidiSnapshotRef& snapshot) {
  editFlat_.restoreFromSnapshot(snapshot);
  syncEditFlatToTakes(true);
}

void Loop::discardEditFlatMaterialization() {
  editFlat_.mutStore().clear();
  editFlat_.discardFlatCache();
  editFlatStale_ = true;
}

void Loop::commitStopFinalizeFromStore(LoopEventStore& merged) {
  const TakeId preserveId = lastPublishedTakeId_;
  TakeType preserveType = TakeType::Overdub;
  uint32_t preserveMergeSeq = 0;
  for (const Take& take : takes) {
    if (take.state == TakeState::Active && take.id == preserveId) {
      preserveType = take.type;
      preserveMergeSeq = take.mergeSequence;
      break;
    }
  }

  ChunkIdList refs;
  merged.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveTakeChunks();
  takes.erase(std::remove_if(takes.begin(), takes.end(),
                              [](const Take& e) { return e.state == TakeState::Active; }),
               takes.end());

  Take rebuilt{};
  rebuilt.id = (preserveId != kInvalidTakeId) ? preserveId : nextTakeId_++;
  if (rebuilt.id >= nextTakeId_) {
    nextTakeId_ = rebuilt.id + 1;
  }
  rebuilt.mergeSequence = preserveMergeSeq;
  rebuilt.type = preserveType;
  rebuilt.state = TakeState::Active;
  rebuilt.chunkRefs = std::move(refs);
  takes.push_back(rebuilt);
  lastPublishedTakeId_ = rebuilt.id;
  ++playbackRevision;
  discardEditFlatMaterialization();
  rebuildVisualCacheFromTakes();
}

void Loop::importPublishedStore(LoopEventStore& store) {
  resetTakeTimeline();
  if (store.empty()) {
    return;
  }
  editFlat_.mutStore().adoptAll(store);
  editFlatStale_ = false;
  syncEditFlatToTakes(true);
  rebuildVisualCacheFromTakes();
}

void Loop::flushEditStoreToTakes() {
  if (editFlatStale_ || !editFlat_.isFlatDirty()) {
    return;
  }
  editFlat_.syncFlatToStore();
  syncEditFlatToTakes(true);
}

void Loop::shiftActiveTakeTicks(int64_t delta) {
  if (delta == 0 || !hasPublishedEvents()) {
    return;
  }
  materializeEditFlatFromTakes();
  editFlat_.mutStore().shiftAllTicks(delta);
  syncEditFlatToTakes(true);
}

void Loop::beginCapture(CapturePhase phase) {
  discardPendingTake();
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
  if (hasPendingTake_) {
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
  size_t count = 0;
  for (const Take& take : takes) {
    if (take.state == TakeState::Active) {
      LoopEventStore staging;
      MidiEventVec flat;
      LoopEventStore::appendFlattenedChunkIds(take.chunkRefs, flat);
      count += flat.size();
    }
  }
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
  flattenActiveTakes(out);
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

void Loop::resetTakeTimeline() {
  discardPendingTake();
  for (Take& take : takes) {
    LoopEventStore staging;
    staging.adoptChunkIds(take.chunkRefs);
    staging.clear();
  }
  takes.clear();
  nextTakeId_ = 1;
  nextMergeSequence_ = 0;
  lastPublishedTakeId_ = kInvalidTakeId;
  playbackRevision = 0;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  editFlat_.mutStore().clear();
  editFlat_.discardFlatCache();
  editFlatStale_ = true;
}

bool Loop::setTakeState(TakeId id, TakeState state) {
  for (Take& take : takes) {
    if (take.id != id) {
      continue;
    }
    if (take.state == state) {
      return true;
    }
    take.state = state;
    ++playbackRevision;
    markTakeDerivedStale();
    return true;
  }
  return false;
}

CommitResult Loop::commitTake(CommitReason reason, uint32_t sealedAtTick) {
  (void)reason;
  if (capture.store.empty()) {
    discardCapture();
    return CommitResult::Skipped;
  }

  const SealOutcome seal = sealCapture(sealedAtTick);
  if (seal != SealOutcome::Ok) {
    return CommitResult::SealFailed;
  }

  if (!publishPendingTake()) {
    return CommitResult::SealFailed;
  }

  markTakeDerivedStale();
  rebuildVisualCacheFromTakes();
  return CommitResult::Published;
}

size_t Loop::activeTakeCount() const {
  size_t count = 0;
  for (const Take& take : takes) {
    if (take.state == TakeState::Active) {
      ++count;
    }
  }
  return count;
}

void Loop::discardPendingTake() {
  if (!hasPendingTake_) {
    return;
  }
  LoopEventStore staging;
  staging.adoptChunkIds(pendingTake_.chunkRefs);
  staging.clear();
  pendingTake_ = Take{};
  hasPendingTake_ = false;
  pendingVisualDelta.clear();
}

void Loop::rebuildVisualCacheFromTakes() {
  MidiEventVec flat;
  flattenActiveTakes(flat);
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
  rebuildVisualCacheFromTakes();
}

void Loop::markDisplayCachesStale() {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
}

SealOutcome Loop::sealCapture(uint32_t sealedAtTick) {
  if (hasPendingTake_) {
    return SealOutcome::AlreadyPending;
  }
  if (capture.store.empty()) {
    return SealOutcome::SkippedEmpty;
  }
  if (takes.size() >= TakeConfig::MAX_TAKES_PER_LOOP) {
    return SealOutcome::AtTakeCap;
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

  pendingTake_ = Take{};
  pendingTake_.id = nextTakeId_++;
  pendingTake_.mergeSequence = nextMergeSequence_++;
  pendingTake_.type = takeTypeForCapturePhase(capture.phase);
  pendingTake_.state = TakeState::Pending;
  pendingTake_.sealedAtTick = sealedAtTick;
  capture.store.detachChunksTo(pendingTake_.chunkRefs);

  if (pendingTake_.chunkRefs.empty()) {
    pendingTake_ = Take{};
    return SealOutcome::FailedValidation;
  }

  pendingVisualDelta.clear();
  hasPendingTake_ = true;
  return SealOutcome::Ok;
}

bool Loop::publishPendingTake() {
  if (!hasPendingTake_) {
    return false;
  }

  Take published = pendingTake_;
  published.state = TakeState::Active;
  takes.push_back(published);
  lastPublishedTakeId_ = published.id;

  pendingTake_ = Take{};
  hasPendingTake_ = false;

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

void Loop::clearCaptureOnNewTake() {
  discardCapture();
}
