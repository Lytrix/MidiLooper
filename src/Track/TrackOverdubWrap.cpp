//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "DisplayManager.h"
#include "Logger.h"
#include "LoopContentResolution.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/TrackMem.h"

bool Track::maybeCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.loopLengthTicks == 0) {
    return false;
  }
  if (loop.consumeSuppressedOverdubWrapCrossing(prevPhase, currentPhase)) {
    return false;
  }
  loop.armOverdubWrapAfterLeavingStart(currentPhase);
  if (!loop.shouldCommitOverdubWrap(prevPhase, currentPhase)) {
    return false;
  }
  return commitOverdubWrapAtSessionStart();
}

TRACK_COLD_MEM bool Track::commitOverdubWrapAtSessionStart() {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.capture.phase != CapturePhase::Overdub) {
    return false;
  }
  SessionMidiEventVec heldOns;
  loop.extractOpenCaptureNoteOns(heldOns);
  if (loop.capture.store.empty()) {
    for (const MidiEvent& evt : heldOns) {
      loop.appendCaptureEvent(evt);
    }
    loop.noteOverdubWrapCommitted();
    return false;
  }
  const uint32_t sealedAtTick = loop.playheadPhaseTick;
  const CommitResult result = loop.commitCapturePass(CommitReason::OverdubWrap, sealedAtTick);
  const bool wrapped = result == CommitResult::Committed;
  if (wrapped) {
    const PassId passId = loop.lastCommittedPassId();
    EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
      if (pass.id == passId) {
        LoopContentResolution::publishPreparedOverdubPass(pass, loop.playbackRevision,
                                                          loop.loopLengthTicks,
                                                          loop.passes.editPasses, companionIds);
        break;
      }
    }
    loop.rebuildOverdubSourceView(loop.playheadPhaseTick);
    loop.pushOverdubSessionPass(passId, companionIds);
    loop.markAffectedDisplayCacheRanges(passId, companionIds);
    invalidateCaches();
  }
  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  for (const MidiEvent& evt : heldOns) {
    loop.appendCaptureEvent(evt);
  }
  loop.noteOverdubWrapCommitted();
  if (wrapped) {
    // RC-W1: reset live compose bookkeeping so the next OVERDUBBING frame rebuilds
    // the committed prefix from overdubSourceViewNotes. Keep the composed notes —
    // a stop before that frame must still have a holdable frame (`011829`).
    displayManager.invalidateLiveDisplayCache(
        DisplayWindowUtils::preserveComposedNotesOnOverdubWrapInvalidate());
  }
  logger.logTrackEvent("Overdub wrap committed", clockManager.getCurrentTick());
  return wrapped;
}
