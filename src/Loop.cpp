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

  if (loopLengthTicks > 0) {
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

  pendingEpoch_ = Epoch{};
  hasPendingEpoch_ = false;

  ++playbackRevision;
  pendingVisualDelta.applyTo(visualCache);
  pendingVisualDelta.clear();

  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();

  return true;
}
