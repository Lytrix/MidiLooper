//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "Logger.h"
#include "Utils/MemoryMonitor.h"

#include <memory>

LoopSnapshotRef Loop::sharePassesSnapshot() const {
  auto snapshot = std::make_shared<PersistedLoopSnapshot>();
  snapshot->loopId = loopId;
  snapshot->startLoopTick = startLoopTick;
  snapshot->loopLengthTicks = loopLengthTicks;
  snapshot->loopStartTick = loopStartTick;
  snapshot->nextPassId = nextPassId_;
  snapshot->nextNoteId = nextNoteId_;
  snapshot->nextMergeSequence = nextMergeSequence_;
  snapshot->lastCommittedPassId = lastCommittedPassId_;
  snapshot->passes = deepClonePasses(passes);
  return snapshot;
}

void Loop::adoptPersistedSnapshot(PersistedLoopSnapshot& snapshot) {
  discardPendingCapturePass();
  discardCapture();
  resetPassTimeline();
  loopId = snapshot.loopId;
  startLoopTick = snapshot.startLoopTick;
  loopLengthTicks = snapshot.loopLengthTicks;
  loopStartTick = snapshot.loopStartTick;
  nextPassId_ = snapshot.nextPassId == 0 ? 1 : snapshot.nextPassId;
  nextNoteId_ = snapshot.nextNoteId == 0 ? 1 : snapshot.nextNoteId;
  nextMergeSequence_ = snapshot.nextMergeSequence;
  lastCommittedPassId_ = snapshot.lastCommittedPassId;
  lastTickInLoop = 0;
  nextEventIndex = 0;
  playbackOrderDirty = true;
  passes = std::move(snapshot.passes);
  snapshot.passes = LoopPasses{};
  loopLengthTicks = reconcileLoopLengthWithCommittedPasses(loopLengthTicks);
  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
}

void Loop::restorePassesSnapshot(const PersistedLoopSnapshot& snapshot) {
  discardPendingCapturePass();
  discardCapture();
  resetPassTimeline();
  loopId = snapshot.loopId;
  startLoopTick = snapshot.startLoopTick;
  loopLengthTicks = snapshot.loopLengthTicks;
  loopStartTick = snapshot.loopStartTick;
  nextPassId_ = snapshot.nextPassId == 0 ? 1 : snapshot.nextPassId;
  nextNoteId_ = snapshot.nextNoteId == 0 ? 1 : snapshot.nextNoteId;
  nextMergeSequence_ = snapshot.nextMergeSequence;
  lastCommittedPassId_ = snapshot.lastCommittedPassId;
  lastTickInLoop = 0;
  nextEventIndex = 0;
  playbackOrderDirty = true;
  passes = deepClonePasses(snapshot.passes);
  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
}

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
