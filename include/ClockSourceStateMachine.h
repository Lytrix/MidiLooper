//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "ClockManager.h"

/**
 * @namespace ClockSourceStateMachine
 * @brief Validates and describes transitions between ClockSource states.
 *
 * Follows the same pattern as TrackStateMachine. Provides:
 *  - isValidTransition(current, next): check if moving from current to next is allowed.
 *  - toString(source): convert ClockSource enum to human-readable string for logging.
 */
namespace ClockSourceStateMachine {

    bool isValidTransition(ClockSource current, ClockSource next);
    const char* toString(ClockSource source);

}
