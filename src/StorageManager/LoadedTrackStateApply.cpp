//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "Track.h"
#include "TrackDisplayState.h"

namespace StorageManagerInternal {

void applyLoadedTrackStateAfterLoopSlots(Track& track, TrackState loadedTrackState,
                                         bool anySlotHasEvents, bool muted) {
    track.forceSetState(normalizeLoadedTrackState(loadedTrackState, anySlotHasEvents));
    if (muted != track.isMuted()) {
        track.toggleMuteTrack();
    }
}

}  // namespace StorageManagerInternal
