//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopPasses.h"

#include "EditApply.h"

#include <algorithm>
#include <vector>

namespace {

void mergeSortedMidiVectors(MidiEventVec& base, MidiEventVec&& addition) {
  if (addition.empty()) {
    return;
  }
  if (base.empty()) {
    base = std::move(addition);
    return;
  }
  MidiEventVec merged;
  merged.reserve(base.size() + addition.size());
  std::merge(base.begin(), base.end(), addition.begin(), addition.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  base = std::move(merged);
}

void collectActiveOverdubPassesSorted(const OverdubPassVec& overdubPasses,
                                      std::vector<const OverdubPass*>& out) {
  out.clear();
  out.reserve(overdubPasses.size());
  for (const OverdubPass& pass : overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.chunkRefs.empty()) {
      out.push_back(&pass);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
}

void appendActiveCapturePassesToFlat(const LoopPasses& passes, MidiEventVec& out) {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.chunkRefs.empty()) {
    LoopEventStore::appendFlattenedChunkIds(passes.recordPass.chunkRefs, out);
  }

  std::vector<const OverdubPass*> activeOverdubs;
  collectActiveOverdubPassesSorted(passes.overdubPasses, activeOverdubs);
  for (const OverdubPass* pass : activeOverdubs) {
    MidiEventVec layer;
    LoopEventStore::appendFlattenedChunkIds(pass->chunkRefs, layer);
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

void applyActiveEditPasses(MidiEventVec& events, const EditPassVec& editPasses,
                           uint32_t loopLengthTicks) {
  for (const EditPass& editPass : editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    switch (editPass.kind) {
      case EditPassKind::NoteEdit:
        applyEditChangeList(events, editPass.changes, loopLengthTicks);
        break;
      case EditPassKind::ControlChange:
        break;
    }
  }
}

}  // namespace

void LoopPasses::materializeToFlat(MidiEventVec& out, uint32_t loopLengthTicks) const {
  out.clear();
  appendActiveCapturePassesToFlat(*this, out);
  applyActiveEditPasses(out, editPasses, loopLengthTicks);
}

void LoopPasses::materialize(LoopEventStore& out, uint32_t loopLengthTicks) const {
  MidiEventVec flat;
  materializeToFlat(flat, loopLengthTicks);
  out.clear();
  if (!flat.empty()) {
    out.loadFromFlat(flat);
  }
}
