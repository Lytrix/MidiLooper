//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

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

}  // namespace

void Loop::mergeActiveCapturePasses(MidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::mergeActiveCapturePasses(SessionMidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::materializeEditViewFromPasses() const {
  Loop* self = const_cast<Loop*>(this);
  const bool storeEmptyMaterialized =
      self->hasCommittedPasses() && self->passesMaterializedStore_.readStore().empty() &&
      self->passes.editPasses.empty();
  if (!passesMaterializedStoreStale_ && !storeEmptyMaterialized) {
    return;
  }
  LoopEventStore::enterEphemeralSeal();
  passes.materialize(self->passesMaterializedStore_.mutStore(), self->loopLengthTicks);
  LoopEventStore::leaveEphemeralSeal();
  self->passesMaterializedStore_.discardEventsCache();
  self->passesMaterializedStoreStale_ = false;
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
