//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoadLoopSelectionPolicy.h"

namespace LoadLoopSelectionPolicy {

ParkedIdleAction resolveParkedIdleAction(bool parkedIsFocus, bool focusQueued,
                                         bool backgroundAllowed) {
    if (parkedIsFocus) {
        return ParkedIdleAction::ResumeParkedFocus;
    }
    if (focusQueued) {
        return ParkedIdleAction::BeginFocusFromQueue;
    }
    if (backgroundAllowed) {
        return ParkedIdleAction::PromoteParkedLow;
    }
    return ParkedIdleAction::SkipKeepParked;
}

EmptyIdleAction resolveEmptyIdleAction(bool focusQueued, bool backgroundQueued,
                                       bool backgroundAllowed) {
    if (focusQueued) {
        return EmptyIdleAction::BeginFocusFromQueue;
    }
    if (backgroundAllowed && backgroundQueued) {
        return EmptyIdleAction::BeginBackgroundFromQueue;
    }
    return EmptyIdleAction::Skip;
}

bool shouldStepLoadLoopJob(bool jobIsFocus, bool backgroundAllowed) {
    return jobIsFocus || backgroundAllowed;
}

}  // namespace LoadLoopSelectionPolicy
