//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "Logger.h"
#include "LoopContentResolution.h"
#include "Utils/MemoryMonitor.h"

#include <algorithm>
#include <memory>

void Loop::assignMissingNoteIds(SessionMidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIds(MidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

uint16_t Loop::reclaimUnreferencedDisabledEditPasses(const SlotPassReferences& refs) {
  const size_t before = passes.editPasses.size();
  passes.editPasses.erase(
      std::remove_if(passes.editPasses.begin(), passes.editPasses.end(),
                     [&](const EditPass& editPass) {
                       const bool remove = editPass.state == EditPassState::Disabled &&
                                           !refs.referencesEditPass(editPass.id);
                       // remove_if may move retained rows after this predicate.
                       // Audible content/revision stays unchanged by reclamation.
                       if (remove) ++canonicalStorageGeneration;
                       return remove;
                     }),
      passes.editPasses.end());
  const size_t after = passes.editPasses.size();
  return static_cast<uint16_t>(before - after);
}

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
  syncNextOverdubSessionIndexFromPasses();
  loopLengthTicks = reconcileLoopLengthWithCommittedPasses(loopLengthTicks);
  ++playbackRevision;
  markDisplayCachesStale();
  notifyCommittedContentChanged();
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
  syncNextOverdubSessionIndexFromPasses();
  ++playbackRevision;
  markDisplayCachesStale();
  notifyCommittedContentChanged();
}

EditPassId Loop::saveNoteEditPass(uint8_t editPassIndex, EditPass row, EditPassType passType,
                                  bool deferDerivedInvalidate, bool skipHeapReserveCheck) {
  if (!skipHeapReserveCheck && !canHeapAdmitEditPass(row)) {
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
  ++canonicalStorageGeneration; // Append may relocate history even with deferred revision.
  passes.editPasses.push_back(std::move(row));
  const EditPassId savedId = passes.editPasses.back().id;
  // DEC-056: already-stale published root — admit into the existing companion
  // set before this save makes playbackRevision move further ahead.
  if (savedId != kInvalidEditPassId && !publishedSnapshotIsCurrent()) {
    appendPendingSnapshotCompanionId(savedId);
  }
  if (!deferDerivedInvalidate) {
    ++playbackRevision;
    editStateDirty_ = true;
    notifyCommittedContentChanged();
  }
  return savedId;
}

PassId Loop::saveLoopGeometry(LoopGeometry row) {
  row.id = nextPassId_++;
  row.state = LoopGeometryState::Active;
  passes.loopGeometries.push_back(row);
  editStateDirty_ = true;
  return passes.loopGeometries.back().id;
}

bool Loop::setLoopGeometryState(PassId id, LoopGeometryState state) {
  if (id == kInvalidPassId) {
    return false;
  }
  for (LoopGeometry& geometry : passes.loopGeometries) {
    if (geometry.id != id) {
      continue;
    }
    if (geometry.state != state) {
      geometry.state = state;
      editStateDirty_ = true;
    }
    return true;
  }
  return false;
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
    LoopContentResolution::setPreparedEditPassState(id, EditPassState::Disabled);
  }
  ++playbackRevision;
  LoopContentResolution::restampPreparedPlaybackRevision(playbackRevision);
  notifyCommittedContentChanged();
}

void Loop::enableEditPasses(const EditPassIdList& ids) {
  for (const EditPassId id : ids) {
    for (EditPass& editPass : passes.editPasses) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Active;
      }
    }
    LoopContentResolution::setPreparedEditPassState(id, EditPassState::Active);
  }
  ++playbackRevision;
  LoopContentResolution::restampPreparedPlaybackRevision(playbackRevision);
  notifyCommittedContentChanged();
}

void Loop::materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                           SessionMidiEventVec& out) const {
  if (tryCopyCurrentSnapshotEditViewEvents(out)) {
    return;
  }
  passes.materializeExcludingEditPassIds(excludeIds, out, loopLengthTicks);
}

void Loop::materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                           MidiEventVec& out) const {
  SessionMidiEventVec sessionEvents;
  materializeExcludingEditPassIds(excludeIds, sessionEvents);
  out.assign(sessionEvents.begin(), sessionEvents.end());
}
