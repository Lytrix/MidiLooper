//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditSessionUndo.h"

#include <algorithm>

#include "EditApply.h"
#include "Globals.h"
#include "Loop.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#include "Logger.h"
#endif

#if defined(SESSION_CAPTURE)
void logUndoPushPhase(const char* phase, uint32_t phaseStartUs, size_t stackSize, size_t cursor,
                      size_t entryBaselineCount) {
  const uint32_t nowUs = micros();
  logger.info("#CAP,%lu,UNDO_PUSH,%s,%lu,%zu,%zu,%zu", static_cast<unsigned long>(nowUs), phase,
              static_cast<unsigned long>(nowUs - phaseStartUs), stackSize, cursor,
              entryBaselineCount);
}
#endif

namespace {

#if defined(SESSION_CAPTURE)
void logUndoWarmPhase(const char* phase, uint32_t phaseStartUs, size_t baselineCount,
                      size_t sessionEventCount, uint8_t extraFlags = 0) {
  const uint32_t nowUs = micros();
  logger.info("#CAP,%lu,UNDO_WARM,phase,%s,%lu,%zu,%zu,%u", static_cast<unsigned long>(nowUs),
              phase, static_cast<unsigned long>(nowUs - phaseStartUs), baselineCount,
              sessionEventCount, static_cast<unsigned>(extraFlags));
}

void logUndoWarmSummary(const char* kind, uint32_t totalStartUs, size_t baselineCount,
                        size_t sessionEventCount, size_t editRowCount) {
  const uint32_t nowUs = micros();
  logger.info("#CAP,%lu,UNDO_WARM,%s,total,%lu,%zu,%zu,%zu", static_cast<unsigned long>(nowUs),
              kind, static_cast<unsigned long>(nowUs - totalStartUs), baselineCount,
              sessionEventCount, editRowCount);
}
#endif

constexpr size_t kBaselineMapEntryOverheadBytes = 40;
constexpr size_t kOverlapNoteMapEntryOverheadBytes = 48;
constexpr size_t kCurrentStateRowOverheadBytes = 48;

size_t estimatedNoteEditCurrentStateBytes(const NoteEditCurrentState& state) {
  return state.size() * (sizeof(NoteId) + sizeof(NoteEditCurrentNoteState) +
                         kCurrentStateRowOverheadBytes);
}

EditPass makeSessionStoreRow(EditActionType actionType, EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

}  // namespace

NOTE_EDIT_MEM NoteEditFocus snapshotFocusForSessionUndo(const NoteEditFocus& focus) {
  NoteEditFocus snap;
  snap.active = focus.active;
  snap.movingNoteId = focus.movingNoteId;
  snap.commitBaseline = focus.commitBaseline;
  snap.movingNoteRange = focus.movingNoteRange;
  snap.last = focus.last;
  snap.overlapNotes = focus.overlapNotes;
  if (!focus.active) {
    return snap;
  }

  const auto keepBaseline = [&](NoteId noteId) {
    if (noteId == kInvalidNoteId) {
      return;
    }
    const auto it = focus.baselineMap.find(noteId);
    if (it != focus.baselineMap.end()) {
      snap.baselineMap[noteId] = it->second;
    }
  };

  keepBaseline(focus.movingNoteId);
  for (const auto& [noteId, overlap] : focus.overlapNotes) {
    const auto it = focus.baselineMap.find(noteId);
    if (it != focus.baselineMap.end()) {
      snap.baselineMap[noteId] = it->second;
    } else if (noteId != kInvalidNoteId) {
      snap.baselineMap[noteId] = overlap.baseline;
    }
  }
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
  if (entry.hasUndoCurrentState) {
    bytes += estimatedNoteEditCurrentStateBytes(entry.undoCurrentState);
  }
  if (entry.hasRedoPayload) {
    bytes += entry.redoFocus.baselineMap.size() *
             (sizeof(NoteId) + sizeof(NoteBaseline) + kBaselineMapEntryOverheadBytes);
    bytes += entry.redoFocus.overlapNotes.size() *
             (sizeof(NoteId) + sizeof(OverlapNote) + kOverlapNoteMapEntryOverheadBytes);
    if (entry.hasRedoCurrentState) {
      bytes += estimatedNoteEditCurrentStateBytes(entry.redoCurrentState);
    }
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
  }
  // When PSRAM is available, external payload (baselineMap / overlapNotes) lives in the
  // extmem pool. Do not call getExternalMemoryPoolFreeBytes() here — sm_malloc_stats_pool
  // walks the entire pool (~300ms on 8MB; see LoopEventStore::hasHeadroomForCommittedChunkIdList
  // and main.cpp idle-only logStatus). push_back is the real alloc gate; failed push runs
  // reclaim + discardFlatCache + one retry on idle-adjacent paths.
  return MemoryMonitor::getInternalHeapFreeBytes() >= internalNeeded;
}

template <typename Alloc>
NOTE_EDIT_MEM SessionUndoEntry buildSessionUndoEntry(const NoteEditFocus& focus,
                                                     EditorSelection selection,
                                       const std::vector<MidiEvent, Alloc>& sessionFlat,
                                       uint8_t channel, uint32_t loopLength,
                                       const EditPassIdList& editPassIdsAtPush,
                                       const NoteEditCurrentState* currentStateAtPush) {
#if defined(SESSION_CAPTURE)
  const uint32_t totalStartUs = micros();
  const size_t baselineCount = focus.baselineMap.size();
  const size_t sessionEventCount = sessionFlat.size();
#endif

  uint32_t phaseStartUs = 0;
#if defined(SESSION_CAPTURE)
  phaseStartUs = micros();
#endif
  SessionUndoEntry entry;
  entry.selection = selection;
  entry.focus = snapshotFocusForSessionUndo(focus);
  entry.editPassIdsAtPush = editPassIdsAtPush;
  if (currentStateAtPush != nullptr && !currentStateAtPush->empty()) {
    entry.undoCurrentState = currentStateAtPush->clone();
    entry.hasUndoCurrentState = true;
  }
#if defined(SESSION_CAPTURE)
  logUndoWarmPhase("focus_snap", phaseStartUs, baselineCount, sessionEventCount);
#endif
  if (!focus.active || loopLength == 0) {
#if defined(SESSION_CAPTURE)
    logUndoWarmSummary("build", totalStartUs, baselineCount, sessionEventCount, 0);
#endif
    return entry;
  }

#if defined(SESSION_CAPTURE)
  phaseStartUs = micros();
#endif
  const bool needsBaselineMapDiff = noteEditFocusHasPendingBaselineMapDiff(
      focus, sessionFlat, channel, loopLength, currentStateAtPush);
  const bool needsOverlapResolve = needsBaselineMapDiff && !focus.overlapNotes.empty();
#if defined(SESSION_CAPTURE)
  logUndoWarmPhase("baseline_probe", phaseStartUs, baselineCount, sessionEventCount,
                   needsBaselineMapDiff ? 1U : 0U);
#endif

  // A0: resolvedFlat / focusCopy are build intermediates, not SessionUndoEntry payload.
  // Simple select (both flags false) has no consumer — do not copy PSRAM flats or the
  // full focus. currentState->clone() above is the restore representation; keep it.
  const NoteEditFocus* focusForRows = &focus;
  std::vector<MidiEvent, Alloc> resolvedFlat;
  NoteEditFocus focusCopy;
  if (needsOverlapResolve) {
#if defined(SESSION_CAPTURE)
    phaseStartUs = micros();
#endif
    resolvedFlat = sessionFlat;
    focusCopy = focus;
    resolveOverlapNotesForPreCommit(resolvedFlat, focusCopy, channel, loopLength);
    focusForRows = &focusCopy;
#if defined(SESSION_CAPTURE)
    logUndoWarmPhase("overlap_resolve", phaseStartUs, baselineCount, sessionEventCount);
#endif
  }
#if defined(SESSION_CAPTURE)
  if (!needsOverlapResolve && !needsBaselineMapDiff) {
    logUndoWarmPhase("intermediates", micros(), baselineCount, sessionEventCount, 0);
  }
#endif

  const MidiEventVec* baselineDiffSource = nullptr;
  MidiEventVec flatForBaselineDiff;
  if (needsBaselineMapDiff) {
#if defined(SESSION_CAPTURE)
    phaseStartUs = micros();
#endif
    if (needsOverlapResolve) {
      flatForBaselineDiff.assign(resolvedFlat.begin(), resolvedFlat.end());
    } else {
      flatForBaselineDiff.assign(sessionFlat.begin(), sessionFlat.end());
    }
#if defined(SESSION_CAPTURE)
    logUndoWarmPhase("flat_copy", phaseStartUs, baselineCount, sessionEventCount);
#endif
    baselineDiffSource = &flatForBaselineDiff;
  }
#if defined(SESSION_CAPTURE)
  phaseStartUs = micros();
#endif
  entry.editRows = buildPreCommitEditPasses(*focusForRows, channel, baselineDiffSource, loopLength,
                                            currentStateAtPush);
#if defined(SESSION_CAPTURE)
  logUndoWarmPhase("edit_rows", phaseStartUs, baselineCount, sessionEventCount);
  logUndoWarmSummary("build", totalStartUs, baselineCount, sessionEventCount, entry.editRows.size());
#endif
  return entry;
}

template SessionUndoEntry buildSessionUndoEntry<InternalHeapFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, EditorSelection, const MidiEventVec&, uint8_t, uint32_t,
    const EditPassIdList&, const NoteEditCurrentState*);
template SessionUndoEntry buildSessionUndoEntry<ExternalMemoryFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, EditorSelection, const SessionMidiEventVec&, uint8_t, uint32_t,
    const EditPassIdList&, const NoteEditCurrentState*);

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

void dropUnrequestedSessionStoreDeletes(EditPassVec& rows,
                                        const NoteEditCurrentState& currentState) {
  if (currentState.empty() || rows.empty()) {
    return;
  }
  rows.erase(std::remove_if(rows.begin(), rows.end(),
                            [&currentState](const EditPass& row) {
                              return row.actionType == EditActionType::Delete &&
                                     !currentState.isRowHiddenOrDeleted(row.targetNoteId);
                            }),
             rows.end());
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
  store.mutStore().loadFromEvents(flat);
  store.discardEventsCache();
  applyNoteEditPassSequence(store.mutEvents(), rows, loopLength);
  store.syncEventsToStore();
}

}  // namespace

void restoreSessionStoreFromCurrentState(CowLoopEventStore& store,
                                         const NoteEditCurrentState& currentState,
                                         uint8_t channel) {
  MidiEventVec& flat = store.mutEvents();
  currentState.projectToSessionStore(flat, channel);
  store.syncEventsToStore();
}

void applySessionUndoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, uint8_t channel,
                           const EditPassIdList& currentEditPassIds) {
  if (entry.hasUndoCurrentState) {
    restoreSessionStoreFromCurrentState(store, entry.undoCurrentState, channel);
    return;
  }
  applySessionEditRows(loop, store, entry.editRows, loopLength, currentEditPassIds,
                       entry.editPassIdsAtPush);
}

void applySessionRedoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, uint8_t channel,
                           const EditPassIdList& currentEditPassIds) {
  if (!entry.hasRedoPayload) {
    return;
  }
  if (entry.hasRedoCurrentState) {
    restoreSessionStoreFromCurrentState(store, entry.redoCurrentState, channel);
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
  a.copyEventsTo(flatA);
  b.copyEventsTo(flatB);
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
