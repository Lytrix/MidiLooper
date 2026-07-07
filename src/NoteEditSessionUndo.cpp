//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditSessionUndo.h"

#include "EditApply.h"
#include "Globals.h"
#include "Loop.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

namespace {

constexpr size_t kBaselineMapEntryOverheadBytes = 40;
constexpr size_t kOverlapNoteMapEntryOverheadBytes = 48;

EditPass makeSessionStoreRow(EditActionType actionType, EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

}  // namespace

NoteEditFocus snapshotFocusForSessionUndo(const NoteEditFocus& focus) {
  NoteEditFocus snap = focus;
  if (!focus.active) {
    snap.baselineMap.clear();
    return snap;
  }

  BaselineMap trimmed;
  const auto keepBaseline = [&](NoteId noteId) {
    if (noteId == kInvalidNoteId) {
      return;
    }
    const auto it = focus.baselineMap.find(noteId);
    if (it != focus.baselineMap.end()) {
      trimmed[noteId] = it->second;
    }
  };

  keepBaseline(focus.movingNoteId);
  for (const auto& [noteId, overlap] : focus.overlapNotes) {
    const auto it = focus.baselineMap.find(noteId);
    if (it != focus.baselineMap.end()) {
      trimmed[noteId] = it->second;
    } else if (noteId != kInvalidNoteId) {
      trimmed[noteId] = overlap.baseline;
    }
  }
  snap.baselineMap = std::move(trimmed);
  return snap;
}

size_t estimatedSessionUndoInternalBytes(const SessionUndoEntry& entry) {
  size_t bytes = sizeof(SessionUndoEntry);
  bytes += entry.editRows.size() * sizeof(EditPass);
  for (const EditPass& row : entry.editRows) {
    bytes += row.addedEvents.size() * sizeof(MidiEvent);
  }
  bytes += entry.editPassIdsAtPush.size() * sizeof(EditPassId);
  if (entry.hasRedoPayload) {
    bytes += entry.redoEditRows.size() * sizeof(EditPass);
    for (const EditPass& row : entry.redoEditRows) {
      bytes += row.addedEvents.size() * sizeof(MidiEvent);
    }
    bytes += entry.redoEditPassIds.size() * sizeof(EditPassId);
  }
  return bytes;
}

size_t estimatedSessionUndoExternalBytes(const SessionUndoEntry& entry) {
  size_t bytes = entry.focus.baselineMap.size() *
                 (sizeof(NoteId) + sizeof(NoteBaseline) + kBaselineMapEntryOverheadBytes);
  bytes += entry.focus.overlapNotes.size() *
           (sizeof(NoteId) + sizeof(OverlapNote) + kOverlapNoteMapEntryOverheadBytes);
  if (entry.hasRedoPayload) {
    bytes += entry.redoFocus.baselineMap.size() *
             (sizeof(NoteId) + sizeof(NoteBaseline) + kBaselineMapEntryOverheadBytes);
    bytes += entry.redoFocus.overlapNotes.size() *
             (sizeof(NoteId) + sizeof(OverlapNote) + kOverlapNoteMapEntryOverheadBytes);
  }
  return bytes;
}

size_t estimatedSessionUndoEntryBytes(const SessionUndoEntry& entry) {
  return estimatedSessionUndoInternalBytes(entry) + estimatedSessionUndoExternalBytes(entry);
}

bool canHeapAdmitSessionUndoEntry(const SessionUndoEntry& entry) {
  const size_t internalBytes = estimatedSessionUndoInternalBytes(entry);
  const size_t externalBytes = estimatedSessionUndoExternalBytes(entry);
  size_t internalNeeded = Config::HEAP_RESERVE_BYTES + internalBytes;
  if (!MemoryMonitor::isExternalMemoryPoolAvailable()) {
    internalNeeded += externalBytes;
  } else if (externalBytes > 0 &&
             MemoryMonitor::getExternalMemoryPoolFreeBytes() < externalBytes) {
    return false;
  }
  return MemoryMonitor::getInternalHeapFreeBytes() >= internalNeeded;
}

template <typename Alloc>
SessionUndoEntry buildSessionUndoEntry(const NoteEditFocus& focus, EditorSelection selection,
                                       const std::vector<MidiEvent, Alloc>& sessionFlat,
                                       uint8_t channel, uint32_t loopLength,
                                       const EditPassIdList& editPassIdsAtPush) {
  SessionUndoEntry entry;
  entry.selection = selection;
  entry.focus = snapshotFocusForSessionUndo(focus);
  entry.editPassIdsAtPush = editPassIdsAtPush;
  if (!focus.active || loopLength == 0) {
    return entry;
  }

  std::vector<MidiEvent, Alloc> resolvedFlat = sessionFlat;
  NoteEditFocus focusCopy = focus;
  resolveOverlapNotesForPreCommit(resolvedFlat, focusCopy, channel, loopLength);
  entry.editRows = buildPreCommitEditPasses(focusCopy, channel);
  return entry;
}

template SessionUndoEntry buildSessionUndoEntry<InternalHeapFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, EditorSelection, const MidiEventVec&, uint8_t, uint32_t,
    const EditPassIdList&);
template SessionUndoEntry buildSessionUndoEntry<ExternalMemoryFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, EditorSelection, const SessionMidiEventVec&, uint8_t, uint32_t,
    const EditPassIdList&);

template <typename Alloc>
SessionUndoEntry buildSessionUndoEntryAfterLiveCaptureDuringNoteEdit(
    const NoteEditFocus& focus, EditorSelection selection,
    const std::vector<MidiEvent, Alloc>& baselineStoreEvents,
    const std::vector<MidiEvent, Alloc>& sessionStoreEvents, uint8_t channel,
    uint32_t loopLength, const EditPassIdList& editPassIdsAtPush) {
  SessionUndoEntry entry;
  entry.selection = selection;
  entry.focus = snapshotFocusForSessionUndo(focus);
  entry.editPassIdsAtPush = editPassIdsAtPush;
  if (loopLength == 0) {
    return entry;
  }
  entry.editRows =
      buildSessionStoreEditPasses(baselineStoreEvents, sessionStoreEvents, channel, loopLength);
  return entry;
}

template SessionUndoEntry buildSessionUndoEntryAfterLiveCaptureDuringNoteEdit<
    ExternalMemoryFirstAllocator<MidiEvent>>(const NoteEditFocus&, EditorSelection,
                                             const SessionMidiEventVec&,
                                             const SessionMidiEventVec&, uint8_t, uint32_t,
                                             const EditPassIdList&);

template <typename AllocA, typename AllocB>
EditPassVec buildSessionStoreEditPasses(const std::vector<MidiEvent, AllocA>& baselineStoreEvents,
                                        const std::vector<MidiEvent, AllocB>& sessionStoreEvents,
                                        uint8_t channel, uint32_t loopLength) {
  EditPassVec rows;
  const NoteUtils::DisplayNoteVec baselineNotes =
      NoteUtils::reconstructDisplayNotes(baselineStoreEvents, loopLength, false);
  const NoteUtils::DisplayNoteVec sessionNotes =
      NoteUtils::reconstructDisplayNotes(sessionStoreEvents, loopLength, false);

  auto sameNote = [](const NoteUtils::DisplayNote& a, const NoteUtils::DisplayNote& b) {
    return a.noteId == b.noteId && a.note == b.note && a.velocity == b.velocity &&
           a.startTick == b.startTick && a.endTick == b.endTick;
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
    row.targetNoteId = baseline.noteId;
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
    MidiEvent noteOn =
        MidiEvent::NoteOn(session.startTick, channel, session.note, session.velocity);
    noteOn.noteId = session.noteId;
    row.addedEvents.push_back(noteOn);
    row.addedEvents.push_back(MidiEvent::NoteOff(session.endTick, channel, session.note, 0));
    rows.push_back(std::move(row));
  }

  return rows;
}

template EditPassVec
buildSessionStoreEditPasses<ExternalMemoryFirstAllocator<MidiEvent>,
                            ExternalMemoryFirstAllocator<MidiEvent>>(const SessionMidiEventVec&,
                                                                   const SessionMidiEventVec&,
                                                                   uint8_t, uint32_t);
template EditPassVec
buildSessionStoreEditPasses<ExternalMemoryFirstAllocator<MidiEvent>,
                            InternalHeapFirstAllocator<MidiEvent>>(const SessionMidiEventVec&,
                                                                   const MidiEventVec&, uint8_t,
                                                                   uint32_t);
template EditPassVec
buildSessionStoreEditPasses<InternalHeapFirstAllocator<MidiEvent>,
                            ExternalMemoryFirstAllocator<MidiEvent>>(const MidiEventVec&,
                                                                   const SessionMidiEventVec&,
                                                                   uint8_t, uint32_t);
template EditPassVec
buildSessionStoreEditPasses<InternalHeapFirstAllocator<MidiEvent>,
                            InternalHeapFirstAllocator<MidiEvent>>(const MidiEventVec&,
                                                                   const MidiEventVec&, uint8_t,
                                                                   uint32_t);

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
