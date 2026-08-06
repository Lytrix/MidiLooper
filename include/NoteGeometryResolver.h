//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "EditSessionAction.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditMem.h"

#if defined(PIO_UNIT_TEST_NATIVE)
class Track;
class EditManager;
#else
#include "EditManager.h"
#include "Track.h"
#endif

/// Resolves note geometry into valid note mutations (analyze → constrain → build → apply).
class NoteGeometryResolver {
public:
    /// @param overlapPitchLane When set, only baseline notes on this MIDI pitch are overlap targets.
    NOTE_EDIT_MEM static bool resolve(
        Track& track, EditManager& manager, const EditedGeometry& editedGeometry,
        const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId,
        std::optional<uint8_t> overlapPitchLane = std::nullopt,
        bool refreshPlaybackPreview = true);

    /// Single causing-note entry — latch map built here (keeps std::unordered_map out of NoteMovementUtils ITCM).
    NOTE_EDIT_MEM static bool resolveForCausingNote(
        Track& track, EditManager& manager, NoteId causingNoteId, const NoteBaseline& editedSpan,
        const NoteBaseline& priorLatch, std::optional<uint32_t> selectionTick = std::nullopt,
        std::optional<uint8_t> overlapPitchLane = std::nullopt,
        bool refreshPlaybackPreview = true);
};
