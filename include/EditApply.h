//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "Edit.h"
#include "LoopEventStore.h"
#include "Take.h"

struct Loop;

/// Materialize Active takes plus Active edits into out (chunk-backed store).
void applyEdits(const TakeVec& takes, const EditVec& edits, LoopEventStore& out,
                uint32_t loopLengthTicks = 0);

/// Flatten materialized view for display / hash / legacy callers.
void applyEditsToFlat(const TakeVec& takes, const EditVec& edits, MidiEventVec& out,
                      uint32_t loopLengthTicks = 0);

/// Apply one EditChange to a flat MIDI list (used by applyEdits and tests).
void applyEditChange(MidiEventVec& events, const EditChange& change, uint32_t loopLengthTicks);
