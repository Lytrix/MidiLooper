//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditSessionAction.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"

std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineConstrainedGeometryTargetNoteIds(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength);

ConstrainedNoteGeometry resolveConstrainedGeometry(
    NoteId targetNoteId, const NoteBaseline& baseline,
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        incomingInteractionsForTarget,
    uint32_t loopLength, uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled);

std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>
resolveAllConstrainedGeometry(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled);
