//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditSessionAction.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"

/// Edit session action builder — observes constrained geometry + edited geometry + baseline +
/// live store; does not mutate live store.
EditSessionActions buildEditSessionActions(
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>&
        constrainedGeometry,
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline,
    MidiEventVec& liveStore, uint8_t channel, const NoteEditFocus& focus,
    uint32_t loopLength);

#if defined(SESSION_CAPTURE)
void logEditSessionActions(const EditSessionActions& actions);
#endif
