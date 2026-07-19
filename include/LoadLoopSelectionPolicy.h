//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Pure admission decisions for LoadLoopJob active/parked selection (Phase B.3/B.4).
/// StorageManager owns begin/park/swap; DeferredJobScheduler owns when selection runs.
namespace LoadLoopSelectionPolicy {

/// Active slot empty; a Low (or other) job is parked.
enum class ParkedIdleAction : uint8_t {
    /// Parked job is the focus slot — promote it to active.
    ResumeParkedFocus = 0,
    /// Focus High is queued — begin focus even while Low stays parked (001237).
    BeginFocusFromQueue,
    /// Transport idle — promote parked Low to active.
    PromoteParkedLow,
    /// PLAYING / capture — leave parked Low; do not promote (223713 / canRunBackgroundLoadLoopNow).
    SkipKeepParked,
};

/// Active and parked empty.
enum class EmptyIdleAction : uint8_t {
    BeginFocusFromQueue = 0,
    BeginBackgroundFromQueue,
    Skip,
};

ParkedIdleAction resolveParkedIdleAction(bool parkedIsFocus, bool focusQueued,
                                         bool backgroundAllowed);

EmptyIdleAction resolveEmptyIdleAction(bool focusQueued, bool backgroundQueued,
                                       bool backgroundAllowed);

}  // namespace LoadLoopSelectionPolicy
