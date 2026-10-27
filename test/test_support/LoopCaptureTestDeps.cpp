//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Include once in native tests that link Loop.cpp (capture incremental sanity symbols).

#pragma once

#include "Globals.h"

uint32_t noteMinLengthTicks = Config::DEFAULT_NOTE_MIN_LENGTH_TICKS;
bool noteMinLengthRemoveEnabled = Config::DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED;

#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"
#include "../../src/CommittedEventRange.cpp"
#include "../../src/Utils/CaptureIncrementalSanity.cpp"
#include "../../src/Loop/LoopInternalColdHelpers.cpp"
#include "../../src/Loop/LoopMaterialization.cpp"
#include "../../src/Loop/LoopEditPasses.cpp"
// CONTRACTS.md C3: the private-predecessor source cursor is its own translation unit (the owner's
// bounded step); it defines members `LoopCapture.cpp` and `LoopPendingNoteChange.cpp` call.
#include "../../src/Loop/LoopPrivateSource.cpp"
#include "../../src/Loop/LoopCapture.cpp"
#include "../../src/Loop/LoopSnapshot.cpp"
#include "../../src/Loop/LoopPendingSnapshotRetirement.cpp"
#include "../../src/Loop/LoopSnapshotLifetime.cpp"
#include "../../src/Loop/LoopSnapshotPublish.cpp"
#include "../../src/Loop/LoopSnapshotInventoryPreparation.cpp"
#include "../../src/Loop/LoopSnapshotInventoryAdoption.cpp"
#include "../../src/Loop/LoopSnapshotPlayback.cpp"
#include "../../src/Loop/LoopOverdubSession.cpp"
#include "../../src/LoopContentResolution.cpp"
#include "../../src/Loop/LoopVisualCache.cpp"

#if defined(PIO_UNIT_TEST_NATIVE) && !defined(LOOP_PENDING_NOTE_CHANGE_PROVIDES_DISPLAY_APPLY)
void Loop::applyPendingNoteChangesToDisplayNotes(NoteUtils::DisplayNoteVec& notes,
                                                 size_t liveCaptureBegin) const {
  (void)notes;
  (void)liveCaptureBegin;
}

bool Loop::hasSealedCompanionOverlay() const {
  return false;
}

bool Loop::sealedCompanionOverlayRequired() const {
  return false;
}

/// CONTRACTS.md C3.4's materialization phase belongs to the pending-change owner: it re-derives the
/// participant set from the session view and upserts that owner's set. A native suite that links
/// `Loop` without that owner can never open an overdub session, so no private cursor exists here and
/// the phase fails closed instead of resolving against a source this build does not own.
Loop::PrivateSourceStep Loop::stepPrivatePredecessorHolds(size_t quantum) {
  (void)quantum;
  return PrivateSourceStep::Idle;
}
#endif
