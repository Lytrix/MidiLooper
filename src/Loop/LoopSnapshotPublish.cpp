//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

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

}  // namespace

LOOP_COLD_MEM bool Loop::tryPublishSessionExitSnapshot(CommitReason reason,
                                                       const EditPassIdList& companionIds) {
  if (!isSessionExit(reason) || loopLengthTicks == 0) {
    return false;
  }
  if (isOverdubSessionExit(reason) && !publishedSnapshot()) {
    return false;
  }

  ImmutableLoopSnapshotRef parent = publishedSnapshot();
  if (!parent) {
    parent = LoopSnapshot::createEmpty(loopId, loopLengthTicks, loopStartTick, nextNoteId_, 1);
  }
  if (!parent) {
    return false;
  }

  LoopSnapshotChangeSet changes;
  std::vector<PassId> passIds;
  if (isOverdubSessionExit(reason)) {
    for (const PassId passId : overdubSessionPassIds_) {
      if (passId != kInvalidPassId) {
        passIds.push_back(passId);
      }
    }
  }
  if (lastCommittedPassId_ != kInvalidPassId) {
    bool alreadyPresent = false;
    for (const PassId passId : passIds) {
      if (passId == lastCommittedPassId_) {
        alreadyPresent = true;
        break;
      }
    }
    if (!alreadyPresent) {
      passIds.push_back(lastCommittedPassId_);
    }
  }
  EventId nextEventId = parent->nextEventId();
  uint32_t nextCanonicalOrder = parent->nextEventId();
  for (const PassId passId : passIds) {
    appendCommittedPassChanges(*this, passId, nextEventId, nextCanonicalOrder, changes);
  }
  if (isOverdubSessionExit(reason)) {
    appendCompanionChanges(*this, *parent, companionIds, changes);
  }
  if (changes.notes.empty() && changes.events.empty()) {
    return false;
  }

  LoopSnapshotConstruction construction(parent, changes);
  if (!construction.step(construction.totalChangeCount()) || construction.failed()) {
    return false;
  }
  ImmutableLoopSnapshotRef frozen = construction.freeze();
  if (!frozen) {
    return false;
  }
  storePublishedSnapshot(std::move(frozen));
  return true;
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
  if (!construction.step(construction.totalChangeCount()) || construction.failed()) {
    return false;
  }
  ImmutableLoopSnapshotRef frozen = construction.freeze();
  if (!frozen) {
    return false;
  }
  storePublishedSnapshot(std::move(frozen));
  return true;
}
