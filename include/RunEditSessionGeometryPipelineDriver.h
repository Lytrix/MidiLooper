//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <optional>

#include "EditSessionAction.h"

#if defined(PIO_UNIT_TEST_NATIVE)
class Track;
class EditManager;
#else
class Track;
class EditManager;
#endif

/// Thin driver — avoids pulling Track.h / EditManager.h into NoteMovementUtils (RAM1 ITCM).
bool runEditSessionGeometryPipelineForCausingNote(
    Track& track, EditManager& manager, NoteId causingNoteId, const NoteBaseline& editedSpan,
    const NoteBaseline& priorLatch, std::optional<uint32_t> selectionTick = std::nullopt,
    std::optional<uint8_t> overlapPitchLane = std::nullopt,
    bool refreshPlaybackPreview = true);
