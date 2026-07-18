//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "TrackState.h"

enum class SlotOpState : uint8_t {
  SLOT_OP_IDLE = 0,
  SLOT_OP_RECORDING = 1,
  SLOT_OP_OVERDUBBING = 2,
};

TrackState normalizeLoadedTrackState(TrackState loadedTrackState, bool anySlotHasEvents);

TrackState resolveDisplayTrackState(TrackState transportState, SlotOpState slotOpState,
                                    bool selectedSlotHasData,
                                    bool selectedSlotHasCommittedPasses,
                                    bool pendingRecordOnTrack,
                                    bool recordQueuedOnSelectedSlot);
