//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "Logger.h"
#include "Utils/MemoryMonitor.h"

EditPassId Loop::saveNoteEditPass(uint8_t editPassIndex, EditPass row, EditPassType passType) {
  if (!canHeapAdmitEditPass(row)) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "saveNoteEditPass rejected: heap below reserve (need=%u free=%u)",
               static_cast<unsigned>(Config::HEAP_RESERVE_BYTES + estimatedEditPassBytes(row)),
               static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
    return kInvalidEditPassId;
  }
  row.id = nextPassId_++;
  row.passType = passType;
  row.editPassIndex = editPassIndex;
  row.state = EditPassState::Active;
  passes.editPasses.push_back(std::move(row));
  ++playbackRevision;
  editStateDirty_ = true;
  markPassDerivedStale();
  return passes.editPasses.back().id;
}

EditPassIdList Loop::replaceNoteEditPass(uint8_t editPassIndex,
                                         const EditPassIdList& staleEditPassIds,
                                         EditPassVec rows) {
  EditPassIdList replacementIds;
  if (staleEditPassIds.empty()) {
    return replacementIds;
  }
  if (rows.empty()) {
    disableEditPasses(staleEditPassIds);
    editStateDirty_ = true;
    return replacementIds;
  }

  for (EditPass& row : rows) {
    const EditPassId id = saveNoteEditPass(editPassIndex, std::move(row));
    if (id != kInvalidEditPassId) {
      replacementIds.push_back(id);
    }
  }
  if (replacementIds.empty()) {
    return replacementIds;
  }
  disableEditPasses(staleEditPassIds);
  return replacementIds;
}

void Loop::disableEditPasses(const EditPassIdList& ids) {
  for (const EditPassId id : ids) {
    for (EditPass& editPass : passes.editPasses) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
      }
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::enableEditPasses(const EditPassIdList& ids) {
  for (const EditPassId id : ids) {
    for (EditPass& editPass : passes.editPasses) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Active;
      }
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                           SessionMidiEventVec& out) const {
  LoopPasses scopedPasses = passes;
  for (EditPass& editPass : scopedPasses.editPasses) {
    for (const EditPassId id : excludeIds) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
        break;
      }
    }
  }
  scopedPasses.materializeToEventVector(out, loopLengthTicks);
}

void Loop::materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                           MidiEventVec& out) const {
  LoopPasses scopedPasses = passes;
  for (EditPass& editPass : scopedPasses.editPasses) {
    for (const EditPassId id : excludeIds) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
        break;
      }
    }
  }
  scopedPasses.materializeToEventVector(out, loopLengthTicks);
}
