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

  for (auto it = loop.captureEvents.rbegin();
       it != loop.captureEvents.rend() && it->tick >= lo; ++it) {
    if (it->tick > hi) {
      continue;
    }
    if (eventsEquivalent(*it, candidate)) {
      return true;
    }
  }

  if (loop.capturePhase != CapturePhase::Overdub) {
    return false;
  }

  for (const MidiEvent& baseline : loop.midiEvents()) {
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

void sortCaptureEventsByTick(MidiEventVec& events) {
  std::stable_sort(events.begin(), events.end(),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

}  // namespace

void Loop::beginCapture(CapturePhase phase) {
  capturePhase = phase;
  captureEvents.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
}

void Loop::discardCapture() {
  captureEvents.clear();
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
  captureEvents.push_back(evt);
  captureEventsSortDirty = true;
  return true;
}

size_t Loop::liveEventCount() const {
  if (capturePhase == CapturePhase::None) {
    return committedEvents.size();
  }
  return committedEvents.size() + captureEvents.size();
}

bool Loop::captureActive() const {
  return capturePhase != CapturePhase::None;
}

bool Loop::ensureCaptureEventsSorted() {
  if (!captureEventsSortDirty) {
    return false;
  }
  sortCaptureEventsByTick(captureEvents);
  captureEventsSortDirty = false;
  return true;
}

void Loop::buildLiveEventView(MidiEventVec& out) const {
  const MidiEventVec& committed = committedEvents.read();
  if (!captureActive() || captureEvents.empty()) {
    out = committed;
    return;
  }
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();
  if (committed.empty()) {
    out = captureEvents;
    return;
  }
  out.clear();
  out.reserve(committed.size() + captureEvents.size());
  std::merge(committed.begin(), committed.end(),
             captureEvents.begin(), captureEvents.end(),
             std::back_inserter(out),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

void Loop::commitCapture() {
  if (captureEvents.empty()) {
    capturePhase = CapturePhase::None;
    captureNextEventIndex = 0;
    captureEventsSortDirty = false;
    return;
  }

  ensureCaptureEventsSorted();

  MidiEventVec& committed = committedEvents.mut();
  if (committed.empty()) {
    committed = std::move(captureEvents);
  } else {
    MidiEventVec merged;
    merged.reserve(committed.size() + captureEvents.size());
    std::merge(committed.begin(), committed.end(),
               captureEvents.begin(), captureEvents.end(),
               std::back_inserter(merged),
               [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    committed.swap(merged);
    captureEvents.clear();
  }

  capturePhase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  playbackOrderDirty = true;
  invalidateCaches();
}
