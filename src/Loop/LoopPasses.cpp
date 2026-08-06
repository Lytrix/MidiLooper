//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopPasses.h"

#include "EditApply.h"
#include "MidiEvent.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

#include <algorithm>
#include <vector>

namespace {

template <typename MidiEventVector>
void mergeSortedMidiVectors(MidiEventVector& base, MidiEventVector&& addition) {
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

void collectActiveOverdubPassesSorted(const CommittedOverdubPassVec& overdubPasses,
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

void mergeOverdubPassLayer(SessionMidiEventVec& out, const OverdubPass& pass) {
  SessionMidiEventVec layer;
  LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, layer);
  mergeSortedMidiVectors(out, std::move(layer));
}

void mergeOverdubPassLayer(MidiEventVec& out, const OverdubPass& pass) {
  SessionMidiEventVec extmemLayer;
  LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, extmemLayer);
  MidiEventVec layer(extmemLayer.begin(), extmemLayer.end());
  mergeSortedMidiVectors(out, std::move(layer));
}

template <typename MidiEventVector>
void appendActiveCapturePassesToFlat(const LoopPasses& passes, MidiEventVector& out) {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, out);
  }

  std::vector<const OverdubPass*> activeOverdubs;
  collectActiveOverdubPassesSorted(passes.overdubPasses, activeOverdubs);
  for (const OverdubPass* pass : activeOverdubs) {
    mergeOverdubPassLayer(out, *pass);
  }
}

template <typename MidiEventVector>
void applyActiveEditPassesMidi(MidiEventVector& events, const EditPassVec& editPasses,
                               uint32_t loopLengthTicks) {
  NoteId trackedNoteId = kInvalidNoteId;
  uint32_t trackedStart = 0;
  uint32_t trackedEnd = 0;
  bool tracked = false;

  auto applyNoteRow = [&](const EditPass& editPass) {
    EditPass resolved = editPass;
    if (tracked && resolved.targetNoteId == trackedNoteId) {
      resolved.startTick = trackedStart;
      resolved.endTick = trackedEnd;
    }
    applyNoteEditPass(events, resolved, loopLengthTicks);
    if (resolved.actionType == EditActionType::Update &&
        resolved.propertyType == EditPropertyType::NoteRange) {
      trackedNoteId = editPass.targetNoteId;
      trackedStart = editPass.startTick;
      trackedEnd = editPass.endTick;
      tracked = true;
    } else if (resolved.actionType == EditActionType::Update &&
               resolved.propertyType == EditPropertyType::Length) {
      trackedNoteId = editPass.targetNoteId;
      trackedStart = resolved.startTick;
      trackedEnd = editPass.endTick;
      tracked = true;
    }
  };

  for (const EditPass& editPass : editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    switch (editPass.passType) {
      case EditPassType::Note:
        applyNoteRow(editPass);
        break;
      case EditPassType::ControlChange:
      case EditPassType::Audio:
        break;
    }
  }
}

void applyActiveEditPasses(SessionMidiEventVec& events, const EditPassVec& editPasses,
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

void LoopPasses::materializeToEventVector(MidiEventVec& out, uint32_t loopLengthTicks) const {
  out.clear();
  appendActiveCapturePassesToFlat(*this, out);
  applyActiveEditPassesMidi(out, editPasses, loopLengthTicks);
}

void LoopPasses::materializeToEventVector(SessionMidiEventVec& out, uint32_t loopLengthTicks) const {
  out.clear();
  appendActiveCapturePassesToFlat(*this, out);
  applyActiveEditPasses(out, editPasses, loopLengthTicks);
}

void LoopPasses::materialize(LoopEventStore& out, uint32_t loopLengthTicks) const {
  SessionMidiEventVec flat;
  materializeToEventVector(flat, loopLengthTicks);
  out.clear();
  if (!flat.empty()) {
    out.loadFromEvents(flat);
  }
}
