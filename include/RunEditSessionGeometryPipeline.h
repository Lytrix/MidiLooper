//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "EditSessionAction.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"

#if defined(PIO_UNIT_TEST_NATIVE)
class Track;
class EditManager;
#else
#include "Track.h"
#include "EditManager.h"
#endif

/// Run analyze → resolve → build → apply for one geometry tick.
/// @param overlapPitchLane Reserved for future pitch-lane scoping; candidates use full baselineMap.
bool runEditSessionGeometryPipeline(
    Track& track, EditManager& manager, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId,
    std::optional<uint8_t> overlapPitchLane = std::nullopt,
    bool refreshPlaybackPreview = true);
