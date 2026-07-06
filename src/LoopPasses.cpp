//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopPasses.h"

#include "EditApply.h"
#include "MidiEvent.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

#include <algorithm>
#include <vector>

namespace {

void mergeSortedMidiVectors(
    MidiEventVec& base,
    std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>&& addition) {
  if (addition.empty()) {
    return;
  }
  if (base.empty()) {
    base.assign(addition.begin(), addition.end());
    return;
  }
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> merged;
  merged.reserve(base.size() + addition.size());
  std::merge(base.begin(), base.end(), addition.begin(), addition.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  base.assign(merged.begin(), merged.end());
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
    LoopEventStore::appendChunkRefEvents(passes.recordPass.chunkRefs, out);
  }

  std::vector<const OverdubPass*> activeOverdubs;
  collectActiveOverdubPassesSorted(passes.overdubPasses, activeOverdubs);
  for (const OverdubPass* pass : activeOverdubs) {
    std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> layer;
    LoopEventStore::appendChunkRefEvents(pass->chunkRefs, layer);
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

void applyActiveEditPasses(MidiEventVec& events, const EditPassVec& editPasses,
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
  auto applyControlChangeEditPass = [&](const EditPass& /*editPass*/) {
    // Explicit scoped dispatch placeholder for ControlChange edit rows.
    // Intentionally no-op until ControlChange edit apply behavior ships.
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
        applyControlChangeEditPass(editPass);
        break;
      case EditPassType::Audio:
        break;
    }
  }
}

}  // namespace

void LoopPasses::materializeToEventVector(MidiEventVec& out, uint32_t loopLengthTicks) const {
  out.clear();
  appendActiveCapturePassesToFlat(*this, out);
  applyActiveEditPasses(out, editPasses, loopLengthTicks);
}

void LoopPasses::materializeToEventVector(SessionMidiEventVec& out, uint32_t loopLengthTicks) const {
  MidiEventVec temp;
  materializeToEventVector(temp, loopLengthTicks);
  out.assign(temp.begin(), temp.end());
}

void LoopPasses::materialize(LoopEventStore& out, uint32_t loopLengthTicks) const {
  MidiEventVec flat;
  materializeToEventVector(flat, loopLengthTicks);
  out.clear();
  if (!flat.empty()) {
    out.loadFromFlat(flat);
  }
}
