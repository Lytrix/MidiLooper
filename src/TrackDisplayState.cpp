//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackDisplayState.h"

TrackState normalizeLoadedTrackState(TrackState loadedTrackState, bool anySlotHasEvents) {
    if (loadedTrackState == TRACK_RECORDING || loadedTrackState == TRACK_ARMED ||
        loadedTrackState == TRACK_STOPPED_RECORDING || loadedTrackState == TRACK_PLAYING ||
        loadedTrackState == TRACK_OVERDUBBING) {
        loadedTrackState = anySlotHasEvents ? TRACK_STOPPED : TRACK_EMPTY;
    }
    if (loadedTrackState == TRACK_EMPTY && anySlotHasEvents) {
        loadedTrackState = TRACK_STOPPED;
    }
    if (!anySlotHasEvents && loadedTrackState == TRACK_STOPPED) {
        loadedTrackState = TRACK_EMPTY;
    }
    return loadedTrackState;
}

TrackState resolveDisplayTrackState(TrackState transportState, SlotOpState slotOpState,
                                    bool selectedSlotHasData, bool pendingRecordOnTrack,
                                    bool recordQueuedOnSelectedSlot) {
    if (slotOpState == SlotOpState::SLOT_OP_RECORDING) return TRACK_RECORDING;
    if (slotOpState == SlotOpState::SLOT_OP_OVERDUBBING) return TRACK_OVERDUBBING;
    if (pendingRecordOnTrack &&
        (transportState == TRACK_PLAYING || transportState == TRACK_OVERDUBBING)) {
        return TRACK_ARMED;
    }
    if (recordQueuedOnSelectedSlot) {
        return TRACK_ARMED;
    }
    if (!selectedSlotHasData) {
        return TRACK_EMPTY;
    }
    return transportState;
}
