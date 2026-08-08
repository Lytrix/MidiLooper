//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditSessionAction.h"
#include "EditSessionInteraction.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"

class NoteEditCurrentState;

std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineConstrainedGeometryTargetNoteIds(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const NoteEditFocus& focus, const NoteEditCurrentState* currentState = nullptr);

ConstrainedNoteGeometry resolveConstrainedGeometry(
    NoteId targetNoteId, const NoteBaseline& baseline,
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        incomingInteractionsForTarget,
    uint32_t loopLength, uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled);

std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>
resolveAllConstrainedGeometry(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& projectedTransactionBaseline,
    const BaselineMap& storageTransactionBaseline, const MidiEventVec& liveStore, uint8_t channel,
    uint32_t loopLength, uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled,
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const NoteEditFocus& focus, NoteIdList& leaveRestoreTargetNoteIds,
    const NoteEditCurrentState* currentState = nullptr);
