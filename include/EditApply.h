//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "EditPass.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"

/// Apply one EditChange to a flat MIDI list (used by materialize and tests).
void applyEditChange(MidiEventVec& events, const EditChange& change, uint32_t loopLengthTicks);

/// Apply an ordered EditChange list with move/length identity tracking.
void applyEditChangeList(MidiEventVec& events, const EditChangeList& changes,
                         uint32_t loopLengthTicks);
