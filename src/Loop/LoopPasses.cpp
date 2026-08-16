//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopPasses.h"

#include "EditApply.h"
#include "MidiEvent.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

namespace {

template <typename MidiEventVector>
LOOP_COLD_MEM void mergeSortedMidiVectors(MidiEventVector& base, MidiEventVector&& addition) {
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

LOOP_COLD_MEM void collectActiveOverdubPassesSorted(const CommittedOverdubPassVec& overdubPasses,
                                                    std::vector<const OverdubPass*>& out) {
  out.clear();
  out.reserve(overdubPasses.size());
  for (const OverdubPass& pass : overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      out.push_back(&pass);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
}

template <typename MidiEventVector>
LOOP_COLD_MEM void applyActiveEditPassesMidi(MidiEventVector& events, const EditPassVec& editPasses,
                                             uint32_t loopLengthTicks) {
  // Rows locate their note by targetNoteId; startTick / endTick are payload only. See
  // applyNoteEditPassSequence for why no span rewrite happens between rows.
  for (const EditPass& editPass : editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    switch (editPass.passType) {
      case EditPassType::Note:
        applyNoteEditPass(events, editPass, loopLengthTicks);
        break;
      case EditPassType::ControlChange:
      case EditPassType::Audio:
        break;
    }
  }
}

LOOP_COLD_MEM void applyActiveEditPasses(SessionMidiEventVec& events, const EditPassVec& editPasses,
                                         uint32_t loopLengthTicks) {
  EditPassVec activeRows;
  activeRows.reserve(editPasses.size());
  for (const EditPass& editPass : editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
    }
  }
  if (!activeRows.empty()) {
    applyNoteEditPassSequence(events, activeRows, loopLengthTicks);
  }
}

}  // namespace

LOOP_COLD_MEM __attribute__((noinline)) void LoopPasses::materializeToEventVector(
    MidiEventVec& out, uint32_t loopLengthTicks) const {
  out.clear();
  if (hasRecordPass() && recordPass.state == CapturePassState::Active &&
      !recordPass.committedChunkIds.empty()) {
    SessionMidiEventVec extmemLayer;
    LoopEventStore::appendChunkRefEvents(recordPass.committedChunkIds, extmemLayer);
    MidiEventVec layer(extmemLayer.begin(), extmemLayer.end());
    applyActiveEditPassesMidi(layer, editPasses, loopLengthTicks);
    mergeSortedMidiVectors(out, std::move(layer));
  }
  std::vector<const OverdubPass*> activeOverdubs;
  collectActiveOverdubPassesSorted(overdubPasses, activeOverdubs);
  for (const OverdubPass* pass : activeOverdubs) {
    SessionMidiEventVec extmemLayer;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, extmemLayer);
    MidiEventVec layer(extmemLayer.begin(), extmemLayer.end());
    applyActiveEditPassesMidi(layer, editPasses, loopLengthTicks);
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

LOOP_COLD_MEM __attribute__((noinline)) void LoopPasses::materializeToEventVector(
    SessionMidiEventVec& out, uint32_t loopLengthTicks) const {
  out.clear();
  if (hasRecordPass() && recordPass.state == CapturePassState::Active &&
      !recordPass.committedChunkIds.empty()) {
    SessionMidiEventVec layer;
    LoopEventStore::appendChunkRefEvents(recordPass.committedChunkIds, layer);
    applyActiveEditPasses(layer, editPasses, loopLengthTicks);
    mergeSortedMidiVectors(out, std::move(layer));
  }
  std::vector<const OverdubPass*> activeOverdubs;
  collectActiveOverdubPassesSorted(overdubPasses, activeOverdubs);
  for (const OverdubPass* pass : activeOverdubs) {
    SessionMidiEventVec layer;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, layer);
    applyActiveEditPasses(layer, editPasses, loopLengthTicks);
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

LOOP_COLD_MEM __attribute__((noinline)) void LoopPasses::materialize(
    LoopEventStore& out, uint32_t loopLengthTicks) const {
  SessionMidiEventVec flat;
  materializeToEventVector(flat, loopLengthTicks);
  out.clear();
  if (!flat.empty()) {
    out.loadFromEvents(flat);
  }
}
