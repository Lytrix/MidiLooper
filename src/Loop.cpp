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

void collectActiveEpochsSorted(const Loop& loop, std::vector<const Epoch*>& out) {
  out.clear();
  out.reserve(loop.epochs.size());
  for (const Epoch& epoch : loop.epochs) {
    if (epoch.state == EpochState::Active && !epoch.chunkRefs.empty()) {
      out.push_back(&epoch);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Epoch* a, const Epoch* b) { return a->mergeSequence < b->mergeSequence; });
}

/// Merge Active epoch chunk data into a flat vector without allocating new pool chunks.
void flattenActiveEpochChunksToVec(const std::vector<const Epoch*>& active, MidiEventVec& out) {
  out.clear();
  if (active.empty()) {
    return;
  }

  for (const Epoch* epoch : active) {
    MidiEventVec epochFlat;
    LoopEventStore::appendFlattenedChunkIds(epoch->chunkRefs, epochFlat);
    if (epochFlat.empty()) {
      continue;
    }
    if (out.empty()) {
      out = std::move(epochFlat);
      continue;
    }
    MidiEventVec merged;
    merged.reserve(out.size() + epochFlat.size());
    std::merge(out.begin(), out.end(), epochFlat.begin(), epochFlat.end(),
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

  // Overdub layers new material on published epochs. Only dedupe within the active
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
  for (const Epoch& epoch : epochs) {
    if (epoch.state == EpochState::Active && !epoch.chunkRefs.empty()) {
      return true;
    }
  }
  return false;
}

void Loop::flattenActiveEpochs(MidiEventVec& out) const {
  std::vector<const Epoch*> active;
  collectActiveEpochsSorted(*this, active);
  flattenActiveEpochChunksToVec(active, out);
}

void Loop::materializeEditFlatFromEpochs() const {
  Loop* self = const_cast<Loop*>(this);
  const bool storeEmptyPublished =
      self->hasPublishedEvents() && self->editFlat_.readStore().empty();
  if (!editFlatStale_ && !storeEmptyPublished) {
    return;
  }
  MidiEventVec flat;
  self->flattenActiveEpochs(flat);
  self->editFlat_.mutStore().clear();
  if (!flat.empty()) {
    self->editFlat_.mutStore().loadFromFlat(flat);
  }
  self->editFlat_.discardFlatCache();
  self->editFlatStale_ = false;
}

void Loop::freeActiveEpochChunks() {
  for (Epoch& epoch : epochs) {
    if (epoch.state != EpochState::Active) {
      continue;
    }
    LoopEventStore staging;
    staging.adoptChunkIds(epoch.chunkRefs);
    staging.clear();
    epoch.chunkRefs.clear();
  }
}

void Loop::syncEditFlatToEpochs(bool allowEmptyClear) {
  if (editFlat_.isFlatDirty()) {
    editFlat_.syncFlatToStore();
  }

  LoopEventStore& store = editFlat_.mutStore();
  if (store.empty()) {
    if (!allowEmptyClear && hasPublishedEvents()) {
      discardEditFlatMaterialization();
      return;
    }
    freeActiveEpochChunks();
    epochs.erase(std::remove_if(epochs.begin(), epochs.end(),
                                [](const Epoch& e) { return e.state == EpochState::Active; }),
                 epochs.end());
    lastPublishedEpochId_ = kInvalidEpochId;
    ++playbackRevision;
    discardEditFlatMaterialization();
    rebuildVisualCacheFromEpochs();
    return;
  }

  ChunkIdList refs;
  store.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveEpochChunks();
  epochs.erase(std::remove_if(epochs.begin(), epochs.end(),
                              [](const Epoch& e) { return e.state == EpochState::Active; }),
               epochs.end());

  Epoch rebuilt{};
  rebuilt.id = nextEpochId_++;
  rebuilt.mergeSequence = nextMergeSequence_++;
  rebuilt.state = EpochState::Active;
  rebuilt.kind = EpochKind::Record;
  rebuilt.chunkRefs = std::move(refs);
  epochs.push_back(rebuilt);
  lastPublishedEpochId_ = rebuilt.id;
  ++playbackRevision;
  materializeEditFlatFromEpochs();
  rebuildVisualCacheFromEpochs();
}

void Loop::markEpochDerivedStale() {
  editFlatStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

MidiEventVec& Loop::midiEvents() {
  materializeEditFlatFromEpochs();
  return editFlat_.mutFlat();
}

const MidiEventVec& Loop::midiEvents() const {
  materializeEditFlatFromEpochs();
  return editFlat_.readFlat();
}

LoopEventStore& Loop::mutEditStore() {
  materializeEditFlatFromEpochs();
  return editFlat_.mutStore();
}

const LoopEventStore& Loop::readEditStore() const {
  materializeEditFlatFromEpochs();
  return editFlat_.readStore();
}

std::shared_ptr<const LoopEventStore> Loop::shareEditSnapshot() const {
  materializeEditFlatFromEpochs();
  return editFlat_.shareForSnapshot();
}

void Loop::restoreEditSnapshot(const MidiSnapshotRef& snapshot) {
  editFlat_.restoreFromSnapshot(snapshot);
  syncEditFlatToEpochs(true);
}

void Loop::discardEditFlatMaterialization() {
  editFlat_.mutStore().clear();
  editFlat_.discardFlatCache();
  editFlatStale_ = true;
}

void Loop::commitStopFinalizeFromStore(LoopEventStore& merged) {
  const EpochId preserveId = lastPublishedEpochId_;
  EpochKind preserveKind = EpochKind::Overdub;
  uint32_t preserveMergeSeq = 0;
  for (const Epoch& epoch : epochs) {
    if (epoch.state == EpochState::Active && epoch.id == preserveId) {
      preserveKind = epoch.kind;
      preserveMergeSeq = epoch.mergeSequence;
      break;
    }
  }

  ChunkIdList refs;
  merged.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveEpochChunks();
  epochs.erase(std::remove_if(epochs.begin(), epochs.end(),
                              [](const Epoch& e) { return e.state == EpochState::Active; }),
               epochs.end());

  Epoch rebuilt{};
  rebuilt.id = (preserveId != kInvalidEpochId) ? preserveId : nextEpochId_++;
  if (rebuilt.id >= nextEpochId_) {
    nextEpochId_ = rebuilt.id + 1;
  }
  rebuilt.mergeSequence = preserveMergeSeq;
  rebuilt.kind = preserveKind;
  rebuilt.state = EpochState::Active;
  rebuilt.chunkRefs = std::move(refs);
  epochs.push_back(rebuilt);
  lastPublishedEpochId_ = rebuilt.id;
  ++playbackRevision;
  discardEditFlatMaterialization();
  rebuildVisualCacheFromEpochs();
}

void Loop::importPublishedStore(LoopEventStore& store) {
  resetEpochTimeline();
  if (store.empty()) {
    return;
  }
  editFlat_.mutStore().adoptAll(store);
  editFlatStale_ = false;
  syncEditFlatToEpochs(true);
  rebuildVisualCacheFromEpochs();
}

void Loop::flushEditStoreToEpochs() {
  if (editFlatStale_ || !editFlat_.isFlatDirty()) {
    return;
  }
  editFlat_.syncFlatToStore();
  syncEditFlatToEpochs(true);
}

void Loop::shiftActiveEpochTicks(int64_t delta) {
  if (delta == 0 || !hasPublishedEvents()) {
    return;
  }
  materializeEditFlatFromEpochs();
  editFlat_.mutStore().shiftAllTicks(delta);
  syncEditFlatToEpochs(true);
}

void Loop::beginCapture(CapturePhase phase) {
  discardPendingEpoch();
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
  if (hasPendingEpoch_) {
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
  for (const Epoch& epoch : epochs) {
    if (epoch.state == EpochState::Active) {
      LoopEventStore staging;
      MidiEventVec flat;
      LoopEventStore::appendFlattenedChunkIds(epoch.chunkRefs, flat);
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
  flattenActiveEpochs(out);
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

void Loop::resetEpochTimeline() {
  discardPendingEpoch();
  for (Epoch& epoch : epochs) {
    LoopEventStore staging;
    staging.adoptChunkIds(epoch.chunkRefs);
    staging.clear();
  }
  epochs.clear();
  nextEpochId_ = 1;
  nextMergeSequence_ = 0;
  lastPublishedEpochId_ = kInvalidEpochId;
  playbackRevision = 0;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  editFlat_.mutStore().clear();
  editFlat_.discardFlatCache();
  editFlatStale_ = true;
}

bool Loop::setEpochState(EpochId id, EpochState state) {
  for (Epoch& epoch : epochs) {
    if (epoch.id != id) {
      continue;
    }
    if (epoch.state == state) {
      return true;
    }
    epoch.state = state;
    ++playbackRevision;
    markEpochDerivedStale();
    return true;
  }
  return false;
}

CommitResult Loop::commitCaptureData(CommitReason reason, uint32_t sealedAtTick) {
  (void)reason;
  if (capture.store.empty()) {
    discardCapture();
    return CommitResult::Skipped;
  }

  const SealOutcome seal = sealCaptureLayer(sealedAtTick);
  if (seal != SealOutcome::Ok) {
    return CommitResult::SealFailed;
  }

  if (!publishPendingEpoch()) {
    return CommitResult::SealFailed;
  }

  markEpochDerivedStale();
  rebuildVisualCacheFromEpochs();
  return CommitResult::Published;
}

size_t Loop::activeEpochCount() const {
  size_t count = 0;
  for (const Epoch& epoch : epochs) {
    if (epoch.state == EpochState::Active) {
      ++count;
    }
  }
  return count;
}

void Loop::discardPendingEpoch() {
  if (!hasPendingEpoch_) {
    return;
  }
  LoopEventStore staging;
  staging.adoptChunkIds(pendingEpoch_.chunkRefs);
  staging.clear();
  pendingEpoch_ = Epoch{};
  hasPendingEpoch_ = false;
  pendingVisualDelta.clear();
}

void Loop::rebuildVisualCacheFromEpochs() {
  MidiEventVec flat;
  flattenActiveEpochs(flat);
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
  rebuildVisualCacheFromEpochs();
}

void Loop::markDisplayCachesStale() {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
}

SealOutcome Loop::sealCaptureLayer(uint32_t sealedAtTick) {
  if (hasPendingEpoch_) {
    return SealOutcome::AlreadyPending;
  }
  if (capture.store.empty()) {
    return SealOutcome::SkippedEmpty;
  }
  if (epochs.size() >= EpochConfig::MAX_EPOCHS_PER_LOOP) {
    return SealOutcome::AtEpochCap;
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

  pendingEpoch_ = Epoch{};
  pendingEpoch_.id = nextEpochId_++;
  pendingEpoch_.mergeSequence = nextMergeSequence_++;
  pendingEpoch_.kind = epochKindForCapturePhase(capture.phase);
  pendingEpoch_.state = EpochState::Pending;
  pendingEpoch_.sealedAtTick = sealedAtTick;
  capture.store.detachChunksTo(pendingEpoch_.chunkRefs);

  if (pendingEpoch_.chunkRefs.empty()) {
    pendingEpoch_ = Epoch{};
    return SealOutcome::FailedValidation;
  }

  pendingVisualDelta.clear();
  hasPendingEpoch_ = true;
  return SealOutcome::Ok;
}

bool Loop::publishPendingEpoch() {
  if (!hasPendingEpoch_) {
    return false;
  }

  Epoch published = pendingEpoch_;
  published.state = EpochState::Active;
  epochs.push_back(published);
  lastPublishedEpochId_ = published.id;

  pendingEpoch_ = Epoch{};
  hasPendingEpoch_ = false;

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
