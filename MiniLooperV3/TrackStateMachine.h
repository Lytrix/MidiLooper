// TrackStateMachine.h
#pragma once

#include "Track.h"

namespace TrackStateMachine {

    // Check if a transition between two TrackState values is allowed
    bool isValidTransition(TrackState current, TrackState next);

    // Convert a TrackState enum to a human-readable string
    const char* toString(TrackState state);

}