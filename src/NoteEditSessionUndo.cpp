//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditSessionUndo.h"

#include "EditApply.h"
#include "Globals.h"
#include "Loop.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

namespace {

EditPass makeSessionStoreRow(EditActionType actionType, EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

}  // namespace

size_t estimatedSessionUndoEntryBytes(const SessionUndoEntry& entry) {
  size_t bytes = sizeof(SessionUndoEntry);
  bytes += entry.editRows.size() * sizeof(EditPass);
  for (const EditPass& row : entry.editRows) {
    bytes += row.addedEvents.size() * sizeof(MidiEvent);
  }
  bytes += entry.focus.baselineMap.size() * (sizeof(NoteRef) + sizeof(NoteBaseline));
  bytes += entry.focus.overlapNotes.size() * (sizeof(NoteRef) + sizeof(OverlapNote));
  bytes += entry.editPassIdsAtPush.size() * sizeof(EditPassId);
  if (entry.hasRedoPayload) {
    bytes += entry.redoEditRows.size() * sizeof(EditPass);
    for (const EditPass& row : entry.redoEditRows) {
      bytes += row.addedEvents.size() * sizeof(MidiEvent);
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
  entry.editRows = buildPreCommitEditPasses(focusCopy, channel);
  return entry;
}

EditPassVec buildSessionStoreEditPasses(const MidiEventVec& baselineStoreEvents,
                                        const MidiEventVec& sessionStoreEvents, uint8_t channel,
                                        uint32_t loopLength) {
  EditPassVec rows;
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

    EditPass row = makeSessionStoreRow(EditActionType::Delete, EditPropertyType::None);
    row.target = {channel, baseline.note, baseline.startTick, baseline.endTick};
    rows.push_back(std::move(row));
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

    EditPass row = makeSessionStoreRow(EditActionType::Create, EditPropertyType::None);
    row.addedEvents.push_back(MidiEvent::NoteOn(session.startTick, channel, session.note,
                                                session.velocity));
    row.addedEvents.push_back(MidiEvent::NoteOff(session.endTick, channel, session.note, 0));
    rows.push_back(std::move(row));
  }

  return rows;
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

void applySessionEditRows(Loop& loop, CowLoopEventStore& store, const EditPassVec& rows,
                        uint32_t loopLength, const EditPassIdList& currentEditPassIds,
                        const EditPassIdList& baselineEditPassIds) {
  const EditPassIdList passesToExclude =
      editPassIdsCommittedAfterBaseline(currentEditPassIds, baselineEditPassIds);
  MidiEventVec flat;
  loop.materializeExcludingEditPassIds(passesToExclude, flat);
  store.mutStore().loadFromFlat(flat);
  store.discardFlatCache();
  applyNoteEditPassSequence(store.mutFlat(), rows, loopLength);
  store.syncFlatToStore();
}

}  // namespace

void applySessionUndoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, const EditPassIdList& currentEditPassIds) {
  applySessionEditRows(loop, store, entry.editRows, loopLength, currentEditPassIds,
                       entry.editPassIdsAtPush);
}

void applySessionRedoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, const EditPassIdList& currentEditPassIds) {
  if (!entry.hasRedoPayload) {
    return;
  }
  applySessionEditRows(loop, store, entry.redoEditRows, loopLength, currentEditPassIds,
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
