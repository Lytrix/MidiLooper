//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "EditPass.h"
#include "LoopEventBuffer.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"

class Loop;

struct SessionUndoEntry {
  EditPassVec editRows;
  NoteEditFocus focus;
  NoteEditSelection selection;
  EditPassIdList editPassIdsAtPush;
  EditPassVec redoEditRows;
  NoteEditFocus redoFocus;
  NoteEditSelection redoSelection;
  EditPassIdList redoEditPassIds;
  bool hasRedoPayload = false;
};

size_t estimatedSessionUndoEntryBytes(const SessionUndoEntry& entry);
bool canHeapAdmitSessionUndoEntry(const SessionUndoEntry& entry);

SessionUndoEntry buildSessionUndoEntry(const NoteEditFocus& focus, NoteEditSelection selection,
                                       const MidiEventVec& sessionFlat, uint8_t channel,
                                       uint32_t loopLength,
                                       const EditPassIdList& editPassIdsAtPush);
EditPassVec buildSessionStoreEditPasses(const MidiEventVec& baselineStoreEvents,
                                        const MidiEventVec& sessionStoreEvents, uint8_t channel,
                                        uint32_t loopLength);

void applySessionUndoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, const EditPassIdList& currentEditPassIds);
void applySessionRedoEntry(Loop& loop, CowLoopEventStore& store, const SessionUndoEntry& entry,
                           uint32_t loopLength, const EditPassIdList& currentEditPassIds);

bool sessionUndoStoresMatch(const LoopEventStore& a, const LoopEventStore& b);
