//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"

#include <utility>
#include <vector>

namespace {

bool isRecordSessionExit(CommitReason reason) {
  return reason == CommitReason::RecordStop ||
         reason == CommitReason::RecordStopToStopped;
}

bool isOverdubSessionExit(CommitReason reason) {
  return reason == CommitReason::OverdubStop ||
         reason == CommitReason::OverdubStopToStopped;
}

bool isSessionExit(CommitReason reason) {
  return isRecordSessionExit(reason) || isOverdubSessionExit(reason);
}

const CommittedChunkIdList* committedChunksForPass(const Loop& loop, PassId passId,
                                                   bool& isOverdubPass) {
  isOverdubPass = false;
  if (passId == kInvalidPassId) {
    return nullptr;
  }
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.id == passId) {
    return &loop.passes.recordPass.committedChunkIds;
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == passId) {
      isOverdubPass = true;
      return &pass.committedChunkIds;
    }
  }
  return nullptr;
}

const EditPass* findActiveEditPass(const Loop& loop, EditPassId id) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.id == id && row.state == EditPassState::Active) {
      return &row;
    }
  }
  return nullptr;
}

LOOP_COLD_MEM void appendMergedPassNotes(const NoteUtils::DisplayNoteVec& notes,
                                         LoopId loopId, uint32_t loopLengthTicks,
                                         LoopSnapshotChangeSet& changes) {
  struct MergedNote {
    NoteId noteId = kInvalidNoteId;
    LoopSnapshotNote value{};
  };
  std::vector<MergedNote> merged;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    MergedNote* entry = nullptr;
    for (MergedNote& candidate : merged) {
      if (candidate.noteId == note.noteId) {
        entry = &candidate;
        break;
      }
    }
    if (entry == nullptr) {
      MergedNote created{};
      created.noteId = note.noteId;
      created.value.loopId = loopId;
      created.value.noteId = note.noteId;
      created.value.pitch = note.note;
      created.value.velocity = note.velocity;
      created.value.startTick = note.startTick;
      created.value.endTick = note.endTick;
      created.value.loopLengthTicks = loopLengthTicks;
      merged.push_back(created);
      continue;
    }
    if (note.startTick >= entry->value.startTick) {
      entry->value.startTick = note.startTick;
    } else {
      entry->value.endTick = note.endTick;
    }
  }

  for (const MergedNote& entry : merged) {
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next = entry.value;
    changes.notes.push_back(change);
  }
}

LOOP_COLD_MEM void appendPassEvents(const MidiEventVec& events, LoopId loopId,
                                    uint32_t loopLengthTicks, EventId& nextEventId,
                                    uint32_t& nextCanonicalOrder,
                                    LoopSnapshotChangeSet& changes) {
  for (const MidiEvent& event : events) {
    if (loopLengthTicks == 0 || event.tick >= loopLengthTicks) {
      continue;
    }
    SnapshotEventChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = loopId;
    change.next.eventId = nextEventId++;
    change.next.canonicalOrder = nextCanonicalOrder++;
    change.next.value = event;
    changes.events.push_back(change);
  }
}

LOOP_COLD_MEM bool appendEditRowChange(const LoopSnapshot& parent, const EditPass& row,
                                       LoopId loopId, uint32_t loopLengthTicks,
                                       LoopSnapshotChangeSet& changes) {
  if (row.passType != EditPassType::Note) {
    return true;
  }
  if (row.actionType == EditActionType::Delete) {
    const LoopSnapshotNoteRef previous = parent.findNote(row.targetNoteId, row.pitch);
    if (!previous) {
      return false;
    }
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Remove;
    change.previous = previous;
    changes.notes.push_back(change);
    return true;
  }

  LoopSnapshotNote next{};
  next.loopId = loopId;
  next.noteId = row.targetNoteId;
  next.pitch = row.pitch;
  next.velocity = row.velocity;
  next.startTick = row.startTick;
  next.endTick = row.endTick;
  next.loopLengthTicks = loopLengthTicks;

  const LoopSnapshotNoteRef previous = parent.findNote(row.targetNoteId, row.pitch);
  if (row.actionType == EditActionType::Create && !previous) {
    if (next.noteId == kInvalidNoteId) {
      return false;
    }
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next = next;
    changes.notes.push_back(change);
    return true;
  }
  if (!previous) {
    return false;
  }
  SnapshotNoteChange change{};
  change.kind = SnapshotChangeKind::Replace;
  change.previous = previous;
  change.next = *previous;
  change.next.loopId = loopId;
  change.next.loopLengthTicks = loopLengthTicks;
  if (row.propertyType == EditPropertyType::Pitch ||
      row.propertyType == EditPropertyType::None ||
      row.propertyType == EditPropertyType::NoteRange ||
      row.actionType == EditActionType::Create) {
    change.next.pitch = row.pitch;
  }
  if (row.propertyType == EditPropertyType::Length ||
      row.propertyType == EditPropertyType::NoteRange ||
      row.propertyType == EditPropertyType::Tick ||
      row.propertyType == EditPropertyType::None ||
      row.actionType == EditActionType::Create) {
    change.next.startTick = row.startTick;
    change.next.endTick = row.endTick;
  }
  if (row.velocity != 0 &&
      (row.propertyType == EditPropertyType::Velocity ||
       row.propertyType == EditPropertyType::None ||
       row.actionType == EditActionType::Create)) {
    change.next.velocity = row.velocity;
  }
  changes.notes.push_back(change);
  return true;
}

LOOP_COLD_MEM void appendCompanionChanges(const Loop& loop, const LoopSnapshot& parent,
                                          const EditPassIdList& companionIds,
                                          LoopSnapshotChangeSet& changes) {
  for (const EditPassId id : companionIds) {
    const EditPass* row = findActiveEditPass(loop, id);
    if (row == nullptr) {
      continue;
    }
    appendEditRowChange(parent, *row, loop.loopId, loop.loopLengthTicks, changes);
  }
}

LOOP_COLD_MEM void appendCommittedPassChanges(const Loop& loop, PassId passId,
                                              EventId& nextEventId,
                                              uint32_t& nextCanonicalOrder,
                                              LoopSnapshotChangeSet& changes) {
  bool isOverdubPass = false;
  const CommittedChunkIdList* chunks = committedChunksForPass(loop, passId, isOverdubPass);
  if (chunks == nullptr) {
    return;
  }
  MidiEventVec events;
  LoopEventStore::appendChunkRefEvents(*chunks, events);
  const NoteUtils::DisplayNoteVec notes = NoteUtils::reconstructDisplayNotes(
      events, loop.loopLengthTicks, false, true, isOverdubPass);
  appendMergedPassNotes(notes, loop.loopId, loop.loopLengthTicks, changes);
  appendPassEvents(events, loop.loopId, loop.loopLengthTicks, nextEventId, nextCanonicalOrder,
                   changes);
}

bool passIdInList(const std::vector<PassId>& passIds, PassId passId) {
  for (const PassId id : passIds) {
    if (id == passId) {
      return true;
    }
  }
  return false;
}

bool isCommittedOverdubPass(const Loop& loop, PassId passId) {
  bool isOverdubPass = false;
  return committedChunksForPass(loop, passId, isOverdubPass) != nullptr && isOverdubPass;
}

bool passAlreadyTracked(const PendingSnapshotPublishBox& box, PassId passId) {
  return box.constructionPassId == passId || passIdInList(box.queuedPassIds, passId) ||
         passIdInList(box.appliedPassIds, passId);
}

LOOP_COLD_MEM ImmutableLoopSnapshotRef resolvePendingParent(const Loop& loop,
                                                            const PendingSnapshotPublishBox& box) {
  if (box.unpublishedCandidate) {
    return box.unpublishedCandidate;
  }
  ImmutableLoopSnapshotRef parent = loop.publishedSnapshot();
  if (parent) {
    return parent;
  }
  if (isRecordSessionExit(box.sessionExitReason)) {
    return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                     loop.nextNoteId_, 1);
  }
  if (loop.passes.hasRecordPass()) {
    for (const PassId passId : box.queuedPassIds) {
      if (passId == loop.passes.recordPass.id) {
        return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                         loop.nextNoteId_, 1);
      }
    }
    for (const PassId passId : box.appliedPassIds) {
      if (passId == loop.passes.recordPass.id) {
        return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                         loop.nextNoteId_, 1);
      }
    }
    if (box.constructionPassId == loop.passes.recordPass.id) {
      return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                       loop.nextNoteId_, 1);
    }
  }
  return {};
}

LOOP_COLD_MEM bool startNextPendingConstruction(Loop& loop, PendingSnapshotPublishBox& box) {
  const ImmutableLoopSnapshotRef parent = resolvePendingParent(loop, box);
  if (!parent) {
    return false;
  }

  while (!box.queuedPassIds.empty()) {
    const PassId passId = box.queuedPassIds.front();
    box.queuedPassIds.erase(box.queuedPassIds.begin());
    LoopSnapshotChangeSet changes;
    EventId nextEventId = parent->nextEventId();
    uint32_t nextCanonicalOrder = parent->nextEventId();
    appendCommittedPassChanges(loop, passId, nextEventId, nextCanonicalOrder, changes);
    if (changes.notes.empty() && changes.events.empty()) {
      box.appliedPassIds.push_back(passId);
      continue;
    }
    box.construction = std::make_unique<LoopSnapshotConstruction>(parent, changes);
    box.constructionPassId = passId;
    box.applyingCompanions = false;
    return true;
  }

  if (box.sessionExitQueued && !box.companionsApplied) {
    box.companionsApplied = true;
    LoopSnapshotChangeSet changes;
    appendCompanionChanges(loop, *parent, box.companionIds, changes);
    if (changes.notes.empty() && changes.events.empty()) {
      return false;
    }
    box.construction = std::make_unique<LoopSnapshotConstruction>(parent, changes);
    box.constructionPassId = kInvalidPassId;
    box.applyingCompanions = true;
    return true;
  }
  return false;
}

LOOP_COLD_MEM bool finishPendingConstruction(PendingSnapshotPublishBox& box) {
  ImmutableLoopSnapshotRef frozen = box.construction->freeze();
  if (!frozen) {
    return false;
  }
  box.unpublishedCandidate = std::move(frozen);
  if (box.constructionPassId != kInvalidPassId) {
    box.appliedPassIds.push_back(box.constructionPassId);
  }
  box.construction.reset();
  box.constructionPassId = kInvalidPassId;
  box.applyingCompanions = false;
  return true;
}

}  // namespace

LOOP_COLD_MEM void Loop::enqueueLiveOverdubPasses() {
  if (!pendingSnapshotPublishBox_ || !hasOverdubSession() ||
      pendingSnapshotPublishBox_->sessionExitQueued) {
    return;
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  for (const PassId passId : overdubSessionPassIds_) {
    if (passId != kInvalidPassId && isCommittedOverdubPass(*this, passId) &&
        !passAlreadyTracked(box, passId)) {
      box.queuedPassIds.push_back(passId);
    }
  }
  if (lastCommittedPassId_ != kInvalidPassId &&
      isCommittedOverdubPass(*this, lastCommittedPassId_) &&
      !passAlreadyTracked(box, lastCommittedPassId_)) {
    box.queuedPassIds.push_back(lastCommittedPassId_);
  }
}

LOOP_COLD_MEM void Loop::discardUnpublishedSnapshotIfNotSessionExit() {
  if (pendingSnapshotPublishBox_ && !pendingSnapshotPublishBox_->sessionExitQueued) {
    clearPendingSnapshotPublish();
  }
}

LOOP_COLD_MEM bool Loop::hasPendingSnapshotPublish() const {
  if (pendingSnapshotPublishBox_) {
    const PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
    if (box.construction || !box.queuedPassIds.empty() || box.sessionExitQueued) {
      return true;
    }
  }
  if (!hasOverdubSession()) {
    return false;
  }
  const PendingSnapshotPublishBox* box = pendingSnapshotPublishBox_.get();
  for (const PassId passId : overdubSessionPassIds_) {
    if (passId != kInvalidPassId && isCommittedOverdubPass(*this, passId) &&
        (box == nullptr || !passAlreadyTracked(*box, passId))) {
      return true;
    }
  }
  return lastCommittedPassId_ != kInvalidPassId &&
         isCommittedOverdubPass(*this, lastCommittedPassId_) &&
         (box == nullptr || !passAlreadyTracked(*box, lastCommittedPassId_));
}

LOOP_COLD_MEM bool Loop::hasInFlightSnapshotConstruction() const {
  return pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->construction;
}

LOOP_COLD_MEM bool Loop::queueSessionExitSnapshot(CommitReason reason,
                                                  const EditPassIdList& companionIds) {
  if (!isSessionExit(reason) || loopLengthTicks == 0) {
    return false;
  }
  if (isOverdubSessionExit(reason) && !publishedSnapshot()) {
    const bool recordPublishInFlight =
        pendingSnapshotPublishBox_ &&
        (pendingSnapshotPublishBox_->construction ||
         !pendingSnapshotPublishBox_->queuedPassIds.empty() ||
         pendingSnapshotPublishBox_->unpublishedCandidate);
    if (!recordPublishInFlight) {
      return false;
    }
  }

  if (!pendingSnapshotPublishBox_) {
    pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  box.sessionExitQueued = true;
  box.sessionExitReason = reason;
  box.companionIds = companionIds;

  if (isOverdubSessionExit(reason)) {
    for (const PassId passId : overdubSessionPassIds_) {
      if (passId != kInvalidPassId && !passAlreadyTracked(box, passId)) {
        box.queuedPassIds.push_back(passId);
      }
    }
    for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionCompanionIds_.size();
         ++i) {
      for (const EditPassId companionId : overdubSessionCompanionIds_[i]) {
        if (companionId == kInvalidEditPassId) {
          continue;
        }
        bool alreadyQueued = false;
        for (const EditPassId existing : box.companionIds) {
          if (existing == companionId) {
            alreadyQueued = true;
            break;
          }
        }
        if (!alreadyQueued) {
          box.companionIds.push_back(companionId);
        }
      }
    }
  }
  if (lastCommittedPassId_ != kInvalidPassId && !passAlreadyTracked(box, lastCommittedPassId_)) {
    box.queuedPassIds.push_back(lastCommittedPassId_);
  }
  box.companionsApplied = box.companionIds.empty();

  if (box.construction || !box.queuedPassIds.empty() || box.unpublishedCandidate ||
      !box.companionIds.empty()) {
    return true;
  }
  clearPendingSnapshotPublish();
  return false;
}

LOOP_COLD_MEM bool Loop::tryPublishSessionExitSnapshot(CommitReason reason,
                                                       const EditPassIdList& companionIds) {
  if (!queueSessionExitSnapshot(reason, companionIds)) {
    return false;
  }
  while (stepPendingSnapshotPublish(64)) {
  }
  return publishedSnapshotIsCurrent();
}

LOOP_COLD_MEM bool Loop::stepPendingSnapshotPublish(size_t maximumChanges,
                                                    bool allowStartConstruction,
                                                    uint32_t playheadPhaseTick) {
  if (maximumChanges == 0) {
    return hasPendingSnapshotPublish();
  }
  if (!pendingSnapshotPublishBox_) {
    if (!hasPendingSnapshotPublish()) {
      return false;
    }
    pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  if (allowStartConstruction) {
    enqueueLiveOverdubPasses();
  }

  size_t remaining = maximumChanges;
  while (remaining > 0) {
    if (!box.construction) {
      if (!allowStartConstruction || !startNextPendingConstruction(*this, box)) {
        break;
      }
      if (maximumChanges <= 1) {
        break;
      }
    }
    if (!box.construction) {
      break;
    }
    const size_t before = box.construction->completedUnitCount();
    box.construction->step(remaining, playheadPhaseTick);
    if (box.construction->failed()) {
      clearPendingSnapshotPublish();
      return false;
    }
    const size_t after = box.construction->completedUnitCount();
    if (after <= before) {
      break;
    }
    remaining -= (after - before);
    if (!box.construction->complete()) {
      break;
    }
    if (!finishPendingConstruction(box)) {
      clearPendingSnapshotPublish();
      return false;
    }
  }

  if (box.sessionExitQueued && !box.construction && box.queuedPassIds.empty() &&
      box.companionsApplied) {
    if (!box.unpublishedCandidate) {
      clearPendingSnapshotPublish();
      return false;
    }
    storePublishedSnapshot(box.unpublishedCandidate);
    clearPendingSnapshotPublish();
    return false;
  }
  return hasPendingSnapshotPublish();
}

LOOP_COLD_MEM bool Loop::tryPublishNoteEditSnapshot(const EditPassIdList& editPassIds) {
  const ImmutableLoopSnapshotRef parent = publishedSnapshot();
  if (!parent || editPassIds.empty() || loopLengthTicks == 0) {
    return false;
  }

  LoopSnapshotChangeSet changes;
  for (const EditPassId id : editPassIds) {
    const EditPass* row = findActiveEditPass(*this, id);
    if (row == nullptr) {
      return false;
    }
    if (!appendEditRowChange(*parent, *row, loopId, loopLengthTicks, changes)) {
      return false;
    }
  }
  if (changes.notes.empty() && changes.events.empty()) {
    return false;
  }

  LoopSnapshotConstruction construction(parent, changes);
  while (!construction.complete() && !construction.failed()) {
    if (!construction.step(64)) {
      break;
    }
  }
  if (!construction.complete() || construction.failed()) {
    return false;
  }
  ImmutableLoopSnapshotRef frozen = construction.freeze();
  if (!frozen) {
    return false;
  }
  storePublishedSnapshot(std::move(frozen));
  return true;
}
