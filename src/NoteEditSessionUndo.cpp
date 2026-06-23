//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditSessionUndo.h"

#include "EditApply.h"
#include "Globals.h"
#include "Loop.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

size_t estimatedSessionUndoEntryBytes(const SessionUndoEntry& entry) {
  size_t bytes = sizeof(SessionUndoEntry);
  bytes += entry.changes.size() * sizeof(EditChange);
  for (const EditChange& change : entry.changes) {
    bytes += change.addedEvents.size() * sizeof(MidiEvent);
  }
  bytes += entry.focus.baselineMap.size() * (sizeof(NoteRef) + sizeof(NoteBaseline));
  bytes += entry.focus.overlapNotes.size() * (sizeof(NoteRef) + sizeof(OverlapNote));
  bytes += entry.editPassIdsAtPush.size() * sizeof(EditPassId);
  if (entry.hasRedoPayload) {
    bytes += entry.redoChanges.size() * sizeof(EditChange);
    for (const EditChange& change : entry.redoChanges) {
      bytes += change.addedEvents.size() * sizeof(MidiEvent);
    }
    bytes += entry.redoFocus.baselineMap.size() * (sizeof(NoteRef) + sizeof(NoteBaseline));
    bytes += entry.redoFocus.overlapNotes.size() * (sizeof(NoteRef) + sizeof(OverlapNote));
    bytes += entry.redoEditPassIds.size() * sizeof(EditPassId);
  }
  return bytes;
}

bool canHeapAdmitSessionUndoEntry(const SessionUndoEntry& entry) {
  const size_t needed = Config::HEAP_RESERVE_BYTES + estimatedSessionUndoEntryBytes(entry);
  return MemoryMonitor::getInternalHeapFreeBytes() >= needed;
}

SessionUndoEntry buildSessionUndoEntry(const NoteEditFocus& focus, NoteEditSelection selection,
                                       const MidiEventVec& sessionFlat, uint8_t channel,
                                       uint32_t loopLength,
                                       const EditPassIdList& editPassIdsAtPush) {
  SessionUndoEntry entry;
  entry.selection = selection;
  entry.focus = focus;
  entry.editPassIdsAtPush = editPassIdsAtPush;
  if (!focus.active || loopLength == 0) {
    return entry;
  }

  MidiEventVec resolvedFlat = sessionFlat;
  NoteEditFocus focusCopy = focus;
  resolveOverlapNotesForPreCommit(resolvedFlat, focusCopy, channel, loopLength);
  entry.changes = buildPreCommitEditChanges(focusCopy, channel);
  return entry;
}

EditChangeList buildSessionStoreEditChanges(const MidiEventVec& baselineStoreEvents,
                                            const MidiEventVec& sessionStoreEvents,
                                            uint8_t channel, uint32_t loopLength) {
  EditChangeList changes;
  const std::vector<NoteUtils::DisplayNote> baselineNotes =
      NoteUtils::reconstructNotes(baselineStoreEvents, loopLength, false);
  const std::vector<NoteUtils::DisplayNote> sessionNotes =
      NoteUtils::reconstructNotes(sessionStoreEvents, loopLength, false);

  auto sameNote = [](const NoteUtils::DisplayNote& a, const NoteUtils::DisplayNote& b) {
    return a.note == b.note && a.velocity == b.velocity && a.startTick == b.startTick &&
           a.endTick == b.endTick;
  };

  std::vector<bool> matchedSession(sessionNotes.size(), false);
  for (const NoteUtils::DisplayNote& baseline : baselineNotes) {
    bool matched = false;
    for (size_t i = 0; i < sessionNotes.size(); ++i) {
      if (matchedSession[i] || !sameNote(baseline, sessionNotes[i])) {
        continue;
      }
      matchedSession[i] = true;
      matched = true;
      break;
    }
    if (matched) {
      continue;
    }

    EditChange del;
    del.type = EditChangeType::DeleteNote;
    del.target = {channel, baseline.note, baseline.startTick, baseline.endTick};
    changes.push_back(std::move(del));
  }

  std::vector<bool> matchedBaseline(baselineNotes.size(), false);
  for (const NoteUtils::DisplayNote& session : sessionNotes) {
    bool matched = false;
    for (size_t i = 0; i < baselineNotes.size(); ++i) {
      if (matchedBaseline[i] || !sameNote(session, baselineNotes[i])) {
        continue;
      }
      matchedBaseline[i] = true;
      matched = true;
      break;
    }
    if (matched) {
      continue;
    }

    EditChange add;
    add.type = EditChangeType::AddNote;
    add.addedEvents.push_back(MidiEvent::NoteOn(session.startTick, channel, session.note,
                                                session.velocity));
    add.addedEvents.push_back(MidiEvent::NoteOff(session.endTick, channel, session.note, 0));
    changes.push_back(std::move(add));
  }

  return changes;
}

namespace {

bool editPassIdListContains(const EditPassIdList& ids, EditPassId id) {
  for (const EditPassId existing : ids) {
    if (existing == id) {
      return true;
    }
  }
  return false;
}

EditPassIdList editPassIdsCommittedAfterBaseline(const EditPassIdList& currentIds,
                                                 const EditPassIdList& baselineIds) {
  EditPassIdList exclude;
  for (const EditPassId id : currentIds) {
    if (!editPassIdListContains(baselineIds, id)) {
      exclude.push_back(id);
    }
  }
  return exclude;
}

void applySessionEditChanges(Loop& loop, CowLoopEventStore& store, const EditChangeList& changes,
                             uint32_t loopLength, const EditPassIdList& currentEditPassIds,
                             const EditPassIdList& baselineEditPassIds) {
  const EditPassIdList passesToExclude =
      editPassIdsCommittedAfterBaseline(currentEditPassIds, baselineEditPassIds);
  MidiEventVec flat;
  loop.materializeExcludingEditPassIds(passesToExclude, flat);
  store.mutStore().loadFromFlat(flat);
  store.discardFlatCache();
  applyEditChangeList(store.mutFlat(), changes, loopLength);
  store.syncFlatToStore();
}

}  // namespace

void applySessionUndoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, const EditPassIdList& currentEditPassIds) {
  applySessionEditChanges(loop, store, entry.changes, loopLength, currentEditPassIds,
                          entry.editPassIdsAtPush);
}

void applySessionRedoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, const EditPassIdList& currentEditPassIds) {
  if (!entry.hasRedoPayload) {
    return;
  }
  applySessionEditChanges(loop, store, entry.redoChanges, loopLength, currentEditPassIds,
                          entry.redoEditPassIds);
}

bool sessionUndoStoresMatch(const LoopEventStore& a, const LoopEventStore& b) {
  if (a.size() != b.size()) {
    return false;
  }
  MidiEventVec flatA;
  MidiEventVec flatB;
  a.flatten(flatA);
  b.flatten(flatB);
  if (flatA.size() != flatB.size()) {
    return false;
  }
  for (size_t i = 0; i < flatA.size(); ++i) {
    if (flatA[i].tick != flatB[i].tick || flatA[i].channel != flatB[i].channel ||
        flatA[i].type != flatB[i].type) {
      return false;
    }
    if (flatA[i].isNoteOn() || flatA[i].isNoteOff()) {
      if (flatA[i].data.noteData.note != flatB[i].data.noteData.note ||
          flatA[i].data.noteData.velocity != flatB[i].data.noteData.velocity) {
        return false;
      }
    }
  }
  return true;
}
