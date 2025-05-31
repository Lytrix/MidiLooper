//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// TrackStateMachine.h
#pragma once

#include "Track.h"

/**
 * @namespace TrackStateMachine
 * @brief Validates and describes transitions between Track states.
 *
 * This namespace provides utility functions for the Track state machine:
 *  - isValidTransition(current, next): check if moving from 'current' to 'next' state is allowed.
 *  - toString(state): convert a TrackState enum to a human-readable string representation.
 */
namespace TrackStateMachine {

    // Check if a transition between two TrackState values is allowed
    bool isValidTransition(TrackState current, TrackState next);

    // Convert a TrackState enum to a human-readable string
    const char* toString(TrackState state);

}
