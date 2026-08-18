//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"
#include "Track.h"
#include "TrackManager.h"

#if defined(__IMXRT1062__)
#define TRACK_MANAGER_INTERNAL_MEM FLASHMEM
#else
#define TRACK_MANAGER_INTERNAL_MEM
#endif

/// Single-slot mode: replace the lone enabled slot with target when switching capture slot.
/// `replaceLayeredSet` also collapses an existing multi-slot enabled set to `targetSlot`.
TRACK_MANAGER_INTERNAL_MEM void replaceSingleEnabledSlotWithTarget(
    bool slotEnabled[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK],
    bool slotMuted[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK], uint8_t trackIndex,
    uint8_t targetSlot, bool replaceLayeredSet = false);

/// Lower priority value = reclaim earlier under memory pressure.
TRACK_MANAGER_INTERNAL_MEM uint8_t reclaimTrackPriority(uint8_t trackIndex,
                                                        const TrackManager& manager,
                                                        const Track& track);

/// Resolve track index from Track reference; falls back to selected track when unknown.
TRACK_MANAGER_INTERNAL_MEM uint8_t resolveTrackIndex(const Track& track);
