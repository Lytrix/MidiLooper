//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "EditPass.h"
#include "LoopEventBuffer.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"

class Loop;

struct SessionUndoEntry {
  EditPassVec editRows;
  NoteEditFocus focus;
  EditorSelection selection;
  EditPassIdList editPassIdsAtPush;
  NoteEditCurrentState undoCurrentState;
  bool hasUndoCurrentState = false;
  EditPassVec redoEditRows;
  NoteEditFocus redoFocus;
  EditorSelection redoSelection;
  EditPassIdList redoEditPassIds;
  NoteEditCurrentState redoCurrentState;
  bool hasRedoCurrentState = false;
  bool hasRedoPayload = false;
};

size_t estimatedSessionUndoEntryBytes(const SessionUndoEntry& entry);
bool canHeapAdmitSessionUndoEntry(const SessionUndoEntry& entry);
NoteEditFocus snapshotFocusForSessionUndo(const NoteEditFocus& focus);

#if defined(SESSION_CAPTURE)
void logUndoPushPhase(const char* phase, uint32_t phaseStartUs, size_t stackSize, size_t cursor,
                      size_t entryBaselineCount = 0);
#endif

template <typename Alloc>
SessionUndoEntry buildSessionUndoEntry(const NoteEditFocus& focus, EditorSelection selection,
                                       const std::vector<MidiEvent, Alloc>& sessionFlat,
                                       uint8_t channel, uint32_t loopLength,
                                       const EditPassIdList& editPassIdsAtPush,
                                       const NoteEditCurrentState* currentStateAtPush = nullptr);
template <typename Alloc>
SessionUndoEntry buildSessionUndoEntryAfterLiveCaptureDuringNoteEdit(
    const NoteEditFocus& focus, EditorSelection selection,
    const std::vector<MidiEvent, Alloc>& baselineStoreEvents,
    const std::vector<MidiEvent, Alloc>& sessionStoreEvents, uint8_t channel,
    uint32_t loopLength, const EditPassIdList& editPassIdsAtPush);
template <typename AllocA, typename AllocB>
EditPassVec buildSessionStoreEditPasses(const std::vector<MidiEvent, AllocA>& baselineStoreEvents,
                                        const std::vector<MidiEvent, AllocB>& sessionStoreEvents,
                                        uint8_t channel, uint32_t loopLength);
/// Drop store-diff Deletes that current state did not Hide/Delete (stale session vs overdub).
void dropUnrequestedSessionStoreDeletes(EditPassVec& rows,
                                        const NoteEditCurrentState& currentState);

void restoreSessionStoreFromCurrentState(CowLoopEventStore& store,
                                         const NoteEditCurrentState& currentState,
                                         uint8_t channel);
void applySessionUndoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, uint8_t channel,
                           const EditPassIdList& currentEditPassIds);
void applySessionRedoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, uint8_t channel,
                           const EditPassIdList& currentEditPassIds);

bool sessionUndoStoresMatch(const LoopEventStore& a, const LoopEventStore& b);
