//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ClockSourceStateMachine.h"

namespace ClockSourceStateMachine {

bool isValidTransition(ClockSource current, ClockSource next) {
    switch (current) {
        case CLOCK_INTERNAL:
            return next == CLOCK_EXTERNAL;
        case CLOCK_EXTERNAL:
            return next == CLOCK_INTERNAL;
        default:
            return false;
    }
}

const char* toString(ClockSource source) {
    switch (source) {
        case CLOCK_INTERNAL:  return "INTERNAL";
        case CLOCK_EXTERNAL:  return "EXTERNAL";
        default:              return "UNKNOWN";
    }
}

}  // namespace ClockSourceStateMachine
