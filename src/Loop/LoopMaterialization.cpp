//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "Globals.h"
#include "LoopInternal.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

namespace {

struct CommittedPitchQueryWork {
  uint32_t fullMaterializeCount = 0;
};

}  // namespace

static CommittedPitchQueryWork g_committedPitchQueryWork;

namespace {

template <typename MidiEventVector>
void mergeSortedLoopCaptureLayers(MidiEventVector& base, MidiEventVector&& addition) {
  if (addition.empty()) {
    return;
  }
  if (base.empty()) {
    base = std::move(addition);
    return;
  }
  MidiEventVector merged;
  merged.reserve(base.size() + addition.size());
  std::merge(base.begin(), base.end(), addition.begin(), addition.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  base = std::move(merged);
}

template <typename MidiEventVector>
void mergeActiveCapturePassesInto(const LoopPasses& passes, MidiEventVector& out) {
  out.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, out);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    MidiEventVector layer;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, layer);
    mergeSortedLoopCaptureLayers(out, std::move(layer));
  }
}

template <typename MidiEventVector>
void mergeCaptureStoreIntoMaterializedEvents(const Loop& loop, MidiEventVector& out) {
  if (!loop.captureActive() || loop.capture.store.empty()) {
    return;
  }
  const_cast<Loop&>(loop).ensureCaptureEventsSorted();

  MidiEventVector captureFlat;
  loop.capture.store.copyEventsTo(captureFlat);
  if (out.empty()) {
    out = std::move(captureFlat);
    return;
  }
  if (captureFlat.empty()) {
    return;
  }

  MidiEventVector merged;
  merged.reserve(out.size() + captureFlat.size());
  std::merge(out.begin(), out.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

bool hasActiveEditPasses(const LoopPasses& passes) {
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.state == EditPassState::Active) {
      return true;
    }
  }
  return false;
}

void collectActiveCommittedChunkLists(const LoopPasses& passes,
                                      std::vector<const CommittedChunkIdList*>& lists) {
  lists.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    lists.push_back(&passes.recordPass.committedChunkIds);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    lists.push_back(&pass->committedChunkIds);
  }
}

template <typename MidiEventVector>
void sortMidiEventsByTick(MidiEventVector& events) {
  std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

}  // namespace

void Loop::mergeActiveCapturePasses(MidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::mergeActiveCapturePasses(SessionMidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::materializeEditViewFromPasses() const {
  ensureEffectiveEventStoreCurrent();
}

void Loop::rebuildEffectiveEventStore() const {
  Loop* self = const_cast<Loop*>(this);
  SessionMidiEventVec flat;
  passes.materializeToEventVector(flat, self->loopLengthTicks);
  self->passesMaterializedStore_.mutStore().clear();
  self->passesMaterializedStore_.discardEventsCache();
  SessionMidiEventVec& events = self->passesMaterializedStore_.mutEvents();
  events = std::move(flat);
  self->passesMaterializedStoreStale_ = false;
  ++self->effectiveEventStoreRevision_;
}

void Loop::ensureEffectiveEventStoreCurrent() const {
  if (!passesMaterializedStoreStale_) {
    if (!hasCommittedPasses()) {
      return;
    }
    if (!passesMaterializedStore_.readEvents().empty()) {
      return;
    }
  }
  rebuildEffectiveEventStore();
}

void Loop::copyEffectiveCommittedEvents(SessionMidiEventVec& out) const {
  ensureEffectiveEventStoreCurrent();
  const SessionMidiEventVec& events = passesMaterializedStore_.readEvents();
  out.assign(events.begin(), events.end());
}

void Loop::copyEffectiveCommittedEventsInRange(SessionMidiEventVec& out, uint32_t windowStart,
                                               uint32_t windowLength) const {
  if (loopLengthTicks == 0 || windowLength == 0) {
    out.clear();
    return;
  }
  ensureEffectiveEventStoreCurrent();
  const SessionMidiEventVec& events = passesMaterializedStore_.readEvents();
  DisplayWindowUtils::filterMidiEventsToWindow(events, out, windowStart, windowLength,
                                               loopLengthTicks);
}

void Loop::rematerializeEditView(LoopEventStore& store) const {
  LoopEventStore::enterEphemeralSeal();
  passes.materialize(store, loopLengthTicks);
  LoopEventStore::leaveEphemeralSeal();
}

SessionMidiEventVec& Loop::midiEvents() {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.mutEvents();
}

const SessionMidiEventVec& Loop::midiEvents() const {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.readEvents();
}

void Loop::discardPassesMaterializedCache() {
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardEventsCache();
  passesMaterializedStoreStale_ = true;
}

LOOP_COLD_MEM bool Loop::tryDiscardPassesMaterializedCache() {
  if (!passesMaterializedStoreStale_) {
    return false;
  }
  if (captureActive()) {
    return false;
  }
  const bool hadStore = !passesMaterializedStore_.empty();
  discardPassesMaterializedCache();
  return hadStore;
}

void Loop::mergeMaterializedPassesWithCapture(MidiEventVec& out) const {
  gatherCommittedEventsWithCapture(out);
}

void Loop::mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const {
  gatherCommittedEventsWithCapture(out);
}

LOOP_COLD_MEM void Loop::gatherCommittedEvents(SessionMidiEventVec& out) const {
  copyEffectiveCommittedEvents(out);
}

LOOP_COLD_MEM void Loop::resetCommittedPitchQueryWork() {
  g_committedPitchQueryWork = CommittedPitchQueryWork{};
}

LOOP_COLD_MEM uint32_t Loop::committedEventsFullMaterializeCount() {
  return g_committedPitchQueryWork.fullMaterializeCount;
}

LOOP_COLD_MEM void Loop::gatherCommittedEvents(MidiEventVec& out) const {
  SessionMidiEventVec extmemFlat;
  gatherCommittedEvents(extmemFlat);
  out.assign(extmemFlat.begin(), extmemFlat.end());
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                                       uint32_t windowLength) const {
  if (loopLengthTicks == 0 || windowLength == 0) {
    out.clear();
    return;
  }
  if (hasActiveEditPasses(passes) && !shouldAvoidFullVisualRebuild(loopLengthTicks)) {
    copyEffectiveCommittedEventsInRange(out, windowStart, windowLength);
    return;
  }
  std::vector<const CommittedChunkIdList*> lists;
  collectActiveCommittedChunkLists(passes, lists);
  if (lists.empty()) {
    out.clear();
    return;
  }
  if (isPassesMaterializedStoreFresh()) {
    copyEffectiveCommittedEventsInRange(out, windowStart, windowLength);
    return;
  }
  CommittedEventRange::inWindow(lists.data(), lists.size(), loopLengthTicks, windowStart,
                                windowLength)
      .appendTo(out);
  sortMidiEventsByTick(out);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindow(MidiEventVec& out, uint32_t windowStart,
                                                       uint32_t windowLength) const {
  SessionMidiEventVec extmemFlat;
  gatherCommittedEventsInWindow(extmemFlat, windowStart, windowLength);
  out.assign(extmemFlat.begin(), extmemFlat.end());
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindowWithCapture(SessionMidiEventVec& out,
                                                                  uint32_t windowStart,
                                                                  uint32_t windowLength) const {
  gatherCommittedEventsInWindow(out, windowStart, windowLength);
  if (!captureActive() || capture.store.empty()) {
    return;
  }
  SessionMidiEventVec captureFlat;
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();
  capture.store.copyEventsTo(captureFlat);
  SessionMidiEventVec captureWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(captureFlat, captureWindow, windowStart, windowLength,
                                               loopLengthTicks);
  if (captureWindow.empty()) {
    return;
  }
  if (out.empty()) {
    out = std::move(captureWindow);
    return;
  }
  SessionMidiEventVec merged;
  merged.reserve(out.size() + captureWindow.size());
  std::merge(out.begin(), out.end(), captureWindow.begin(), captureWindow.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

LOOP_COLD_MEM bool Loop::shouldAvoidFullVisualRebuild(uint32_t loopLength) const {
  const uint32_t boundedThreshold =
      DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
  return loopLength > boundedThreshold;
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsForDerivedView(SessionMidiEventVec& flat) const {
  gatherCommittedEvents(flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsForDerivedView(MidiEventVec& flat) const {
  gatherCommittedEvents(flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsWithCapture(SessionMidiEventVec& flat) const {
  gatherCommittedEvents(flat);
  mergeCaptureStoreIntoMaterializedEvents(*this, flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsWithCapture(MidiEventVec& flat) const {
  gatherCommittedEvents(flat);
  mergeCaptureStoreIntoMaterializedEvents(*this, flat);
}
