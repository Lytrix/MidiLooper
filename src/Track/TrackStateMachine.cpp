//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// TrackStateMachine.cpp
#include "TrackStateMachine.h"

namespace TrackStateMachine {

bool isValidTransition(TrackState current, TrackState next) {
    switch (current) {
        case TRACK_EMPTY:
            return next == TRACK_ARMED || next == TRACK_RECORDING;

        case TRACK_ARMED:
            return next == TRACK_RECORDING || next == TRACK_EMPTY || next == TRACK_STOPPED;

        case TRACK_RECORDING:
            return next == TRACK_STOPPED_RECORDING || next == TRACK_EMPTY;

        case TRACK_STOPPED_RECORDING:
            return next == TRACK_PLAYING || next == TRACK_OVERDUBBING || next == TRACK_STOPPED || next == TRACK_EMPTY;

        case TRACK_PLAYING:
            return next == TRACK_RECORDING || next == TRACK_OVERDUBBING || next == TRACK_STOPPED || next == TRACK_EMPTY;

        case TRACK_OVERDUBBING:
            return next == TRACK_OVERDUB_STOPPING || next == TRACK_PLAYING ||
                   next == TRACK_STOPPED || next == TRACK_OVERDUBBING || next == TRACK_EMPTY;

        // The requested destination is resolved by the finalization owner:
        // ordinary overdub stop returns to PLAYING; transport stop wins and
        // enters STOPPED. New capture is deliberately not admitted here.
        case TRACK_OVERDUB_STOPPING:
            return next == TRACK_PLAYING || next == TRACK_STOPPED || next == TRACK_EMPTY;

        case TRACK_STOPPED:
            return next == TRACK_PLAYING || next == TRACK_OVERDUBBING || next == TRACK_ARMED ||
                   next == TRACK_EMPTY || next == TRACK_RECORDING;

        default:
            return false;
    }
}

const char* toString(TrackState state) {
    switch (state) {
        case TRACK_EMPTY:              return "EMPTY";
        case TRACK_STOPPED:            return "STOPPED";
        case TRACK_ARMED:              return "ARMED";
        case TRACK_RECORDING:          return "RECORDING";
        case TRACK_STOPPED_RECORDING:  return "STOPPED_RECORDING";
        case TRACK_PLAYING:            return "PLAYING";
        case TRACK_OVERDUBBING:        return "OVERDUBBING";
        case TRACK_OVERDUB_STOPPING:   return "OVERDUB_STOPPING";
        default:                       return "UNKNOWN";
    }
}

}  // namespace TrackStateMachine
