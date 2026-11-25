//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "Loop.h"
#include "GlobalUndoStack.h"

// Arduino-independent implementation of TrackUndo's private-entry preparation.
// The global stack and Track's before-snapshot ownership change only at publication.
namespace TrackPreparedUndo {
bool prepareOverdubSessionEntry(Loop& loop, Loop::SessionUndoCollection& collection,
    uint8_t slotIndex, const ImmutableLoopSnapshotRef& beforePublished, UndoEntry& entry,
    uint32_t beforePublishedCoverage = kSnapshotCoverageUnset);
}
