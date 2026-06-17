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

  if (loop.capture.phase != CapturePhase::Overdub) {
    return false;
  }

  const LoopEventStore& committed = loop.committedEvents.readStore();
  const size_t startIdx = committed.lowerBoundIndex(lo);
  const size_t committedCount = committed.size();
  for (size_t i = startIdx; i < committedCount; ++i) {
    const MidiEvent& baseline = committed.at(i);
    if (baseline.tick > hi) {
      break;
    }
    if (eventsEquivalent(baseline, candidate)) {
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
  if (capture.phase == CapturePhase::None) {
    return committedEvents.size();
  }
  return committedEvents.size() + capture.store.size();
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
  if (!captureActive() || capture.store.empty()) {
    committedEvents.readStore().flatten(out);
    return;
  }
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();

  MidiEventVec committedFlat;
  committedEvents.readStore().flatten(committedFlat);
  if (committedFlat.empty()) {
    capture.store.flatten(out);
    return;
  }

  MidiEventVec captureFlat;
  capture.store.flatten(captureFlat);
  out.clear();
  out.reserve(committedFlat.size() + captureFlat.size());
  std::merge(committedFlat.begin(), committedFlat.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(out),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
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

void Loop::commitCapture() {
  if (capture.store.empty()) {
    capture.phase = CapturePhase::None;
    captureNextEventIndex = 0;
    captureEventsSortDirty = false;
    return;
  }

  ensureCaptureEventsSorted();

  LoopEventStore& committed = committedEvents.mutStore();
  if (committed.empty()) {
    committed.adoptAll(capture.store);
  } else {
    committed.mergeFrom(capture.store);
  }

  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  playbackOrderDirty = true;
  invalidatePlaybackCaches();
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
}

void Loop::ensureCommittedMigratedToEpoch() {
  if (!epochs.empty() || committedEvents.empty()) {
    return;
  }

  std::shared_ptr<LoopEventStore> clone = committedEvents.readStore().cloneShared();
  if (!clone || clone->empty()) {
    return;
  }

  ChunkIdList refs;
  clone->detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  Epoch migrated{};
  migrated.id = nextEpochId_++;
  migrated.mergeSequence = nextMergeSequence_++;
  migrated.state = EpochState::Active;
  migrated.kind = EpochKind::Record;
  migrated.chunkRefs = std::move(refs);
  epochs.push_back(migrated);
  lastPublishedEpochId_ = migrated.id;
  ++playbackRevision;
}

void Loop::syncCommittedEventsFromEpochs() {
  std::vector<const Epoch*> active;
  active.reserve(epochs.size());
  for (const Epoch& epoch : epochs) {
    if (epoch.state == EpochState::Active) {
      active.push_back(&epoch);
    }
  }
  if (active.empty()) {
    LoopEventStore& committed = committedEvents.mutStore();
    committed.clear();
    playbackOrderDirty = true;
    invalidatePlaybackCaches();
    visualCache.clear();
    visualCacheDirty = true;
    return;
  }

  std::sort(active.begin(), active.end(),
            [](const Epoch* a, const Epoch* b) { return a->mergeSequence < b->mergeSequence; });

  LoopEventStore merged;
  for (const Epoch* epoch : active) {
    LoopEventStore epochStore;
    MidiEventVec flat;
    LoopEventStore::appendFlattenedChunkIds(epoch->chunkRefs, flat);
    if (flat.empty()) {
      continue;
    }
    epochStore.loadFromFlat(flat);
    if (merged.empty()) {
      merged.adoptAll(epochStore);
    } else {
      merged.mergeFrom(epochStore);
    }
  }

  LoopEventStore& committed = committedEvents.mutStore();
  committed.clear();
  if (!merged.empty()) {
    committed.adoptAll(merged);
  }
  playbackOrderDirty = true;
  invalidatePlaybackCaches();
  rebuildVisualCacheFromCommitted();
}

void Loop::rebuildEpochTimelineFromCommitted() {
  discardPendingEpoch();
  for (Epoch& epoch : epochs) {
    LoopEventStore staging;
    staging.adoptChunkIds(epoch.chunkRefs);
    staging.clear();
  }
  epochs.clear();
  nextMergeSequence_ = 0;

  if (committedEvents.empty()) {
    nextEpochId_ = 1;
    lastPublishedEpochId_ = kInvalidEpochId;
    playbackRevision = 0;
    return;
  }

  std::shared_ptr<LoopEventStore> clone = committedEvents.readStore().cloneShared();
  if (!clone || clone->empty()) {
    return;
  }

  ChunkIdList refs;
  clone->detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  Epoch rebuilt{};
  rebuilt.id = nextEpochId_++;
  rebuilt.mergeSequence = nextMergeSequence_++;
  rebuilt.state = EpochState::Active;
  rebuilt.kind = EpochKind::Record;
  rebuilt.chunkRefs = std::move(refs);
  epochs.push_back(rebuilt);
  lastPublishedEpochId_ = rebuilt.id;
  ++playbackRevision;
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
    syncCommittedEventsFromEpochs();
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

  ensureCommittedMigratedToEpoch();

  const SealOutcome seal = sealCaptureLayer(sealedAtTick);
  if (seal != SealOutcome::Ok) {
    return CommitResult::SealFailed;
  }

  if (!publishPendingEpoch()) {
    return CommitResult::SealFailed;
  }

  syncCommittedEventsFromEpochs();
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

void Loop::rebuildVisualCacheFromCommitted() {
  MidiEventVec flat;
  committedEvents.readStore().flatten(flat);
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
  rebuildVisualCacheFromCommitted();
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

  // Overdub capture is a delta — wrap pairing may span baseline epochs. Finalize on the
  // merged committed store in finalizeLoopAtStop after sync, not on capture alone.
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
