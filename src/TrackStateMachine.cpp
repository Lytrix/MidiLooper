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
            return next == TRACK_RECORDING || next == TRACK_EMPTY;

        case TRACK_RECORDING:
            return next == TRACK_STOPPED_RECORDING || next == TRACK_EMPTY;

        case TRACK_STOPPED_RECORDING:
            return next == TRACK_PLAYING || next == TRACK_OVERDUBBING || next == TRACK_EMPTY;

        case TRACK_PLAYING:
            return next == TRACK_OVERDUBBING || next == TRACK_STOPPED || next == TRACK_EMPTY;

        case TRACK_OVERDUBBING:
            return next == TRACK_PLAYING || next == TRACK_STOPPED || next == TRACK_OVERDUBBING || next == TRACK_EMPTY;

        case TRACK_STOPPED:
            return next == TRACK_PLAYING || next == TRACK_OVERDUBBING || next == TRACK_ARMED || next == TRACK_EMPTY;

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
        default:                       return "UNKNOWN";
    }
}

}  // namespace TrackStateMachine
