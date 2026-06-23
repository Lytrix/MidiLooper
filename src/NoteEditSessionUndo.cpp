//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditSessionUndo.h"

#include "EditApply.h"
#include "Globals.h"
#include "Loop.h"
#include "Utils/MemoryMonitor.h"

size_t estimatedSessionUndoEntryBytes(const SessionUndoEntry& entry) {
  size_t bytes = sizeof(SessionUndoEntry);
  bytes += entry.changes.size() * sizeof(EditChange);
  for (const EditChange& change : entry.changes) {
    bytes += change.addedEvents.size() * sizeof(MidiEvent);
  }
  bytes += entry.focus.baselineMap.size() * (sizeof(NoteRef) + sizeof(NoteBaseline));
  bytes += entry.focus.overlapNotes.size() * (sizeof(NoteRef) + sizeof(OverlapNote));
  return bytes;
}

bool canHeapAdmitSessionUndoEntry(const SessionUndoEntry& entry) {
  const size_t needed = Config::HEAP_RESERVE_BYTES + estimatedSessionUndoEntryBytes(entry);
  return MemoryMonitor::getInternalHeapFreeBytes() >= needed;
}

SessionUndoEntry buildSessionUndoEntry(const NoteEditFocus& focus, NoteEditSelection selection,
                                       const MidiEventVec& sessionFlat, uint8_t channel,
                                       uint32_t loopLength) {
  SessionUndoEntry entry;
  entry.selection = selection;
  entry.focus = focus;
  if (!focus.active || loopLength == 0) {
    return entry;
  }

  MidiEventVec resolvedFlat = sessionFlat;
  NoteEditFocus focusCopy = focus;
  resolveOverlapNotesForPreCommit(resolvedFlat, focusCopy, channel, loopLength);
  entry.changes = buildPreCommitEditChanges(focusCopy, channel);
  return entry;
}

void applySessionUndoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength) {
  loop.rematerializeEditView(store.mutStore());
  store.discardFlatCache();
  MidiEventVec& flat = store.mutFlat();
  flat.clear();
  store.readStore().flatten(flat);
  applyEditChangeList(flat, entry.changes, loopLength);
  store.syncFlatToStore();
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
