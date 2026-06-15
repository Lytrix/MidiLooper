//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
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

  const size_t captureCount = loop.captureStore.size();
  for (size_t i = captureCount; i > 0; --i) {
    const MidiEvent& evt = loop.captureStore.at(i - 1);
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

  if (loop.capturePhase != CapturePhase::Overdub) {
    return false;
  }

  const size_t committedCount = loop.committedEvents.size();
  for (size_t i = 0; i < committedCount; ++i) {
    const MidiEvent& baseline = loop.committedEvents.at(i);
    if (baseline.tick < lo) {
      continue;
    }
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
  capturePhase = phase;
  captureStore.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
}

void Loop::discardCapture() {
  captureStore.clear();
  capturePhase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
}

bool Loop::appendCaptureEvent(const MidiEvent& evt) {
  if (capturePhase == CapturePhase::None) {
    return false;
  }
  if (isDuplicateCaptureEvent(*this, evt)) {
    return false;
  }
  if (!captureStore.append(evt)) {
    return false;
  }
  captureEventsSortDirty = true;
  return true;
}

size_t Loop::liveEventCount() const {
  if (capturePhase == CapturePhase::None) {
    return committedEvents.size();
  }
  return committedEvents.size() + captureStore.size();
}

bool Loop::captureActive() const {
  return capturePhase != CapturePhase::None;
}

bool Loop::ensureCaptureEventsSorted() {
  if (!captureEventsSortDirty) {
    return false;
  }
  sortCaptureStoreByTick(captureStore);
  captureEventsSortDirty = false;
  return true;
}

void Loop::buildLiveEventView(MidiEventVec& out) const {
  if (!captureActive() || captureStore.empty()) {
    committedEvents.readStore().flatten(out);
    return;
  }
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();

  MidiEventVec committedFlat;
  committedEvents.readStore().flatten(committedFlat);
  if (committedFlat.empty()) {
    captureStore.flatten(out);
    return;
  }

  MidiEventVec captureFlat;
  captureStore.flatten(captureFlat);
  out.clear();
  out.reserve(committedFlat.size() + captureFlat.size());
  std::merge(committedFlat.begin(), committedFlat.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(out),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

void Loop::commitCapture() {
  if (captureStore.empty()) {
    capturePhase = CapturePhase::None;
    captureNextEventIndex = 0;
    captureEventsSortDirty = false;
    return;
  }

  ensureCaptureEventsSorted();

  LoopEventStore& committed = committedEvents.mutStore();
  if (committed.empty()) {
    committed.adoptAll(captureStore);
  } else {
    committed.mergeFrom(captureStore);
  }

  capturePhase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  playbackOrderDirty = true;
  invalidateCaches();
}
