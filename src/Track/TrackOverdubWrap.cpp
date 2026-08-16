//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "Logger.h"
#include "LoopContentResolution.h"
#include "Utils/TrackMem.h"

void Track::maybeCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.loopLengthTicks == 0) {
    return;
  }
  if (loop.consumeSuppressedOverdubWrapCrossing(prevPhase, currentPhase)) {
    return;
  }
  loop.armOverdubWrapAfterLeavingStart(currentPhase);
  if (!loop.shouldCommitOverdubWrap(prevPhase, currentPhase)) {
    return;
  }
  commitOverdubWrapAtSessionStart();
}

TRACK_COLD_MEM void Track::commitOverdubWrapAtSessionStart() {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.capture.phase != CapturePhase::Overdub) {
    return;
  }
  SessionMidiEventVec heldOns;
  loop.extractOpenCaptureNoteOns(heldOns);
  if (loop.capture.store.empty()) {
    for (const MidiEvent& evt : heldOns) {
      loop.appendCaptureEvent(evt);
    }
    loop.noteOverdubWrapCommitted();
    return;
  }
  const uint32_t sealedAtTick = loop.playheadPhaseTick;
  const CommitResult result = loop.commitCapturePass(CommitReason::OverdubWrap, sealedAtTick);
  if (result == CommitResult::Committed) {
    const PassId passId = loop.lastCommittedPassId();
    loop.applyPendingNoteChangesToOverdubSourceView();
    EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
      if (pass.id == passId) {
        LoopContentResolution::publishPreparedOverdubPass(pass, loop.playbackRevision);
        break;
      }
    }
    loop.pushOverdubSessionPass(passId, companionIds);
    loop.markAffectedDisplayCacheRanges(passId, companionIds);
    invalidateCaches();
  }
  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  for (const MidiEvent& evt : heldOns) {
    loop.appendCaptureEvent(evt);
  }
  loop.noteOverdubWrapCommitted();
  logger.logTrackEvent("Overdub wrap committed", clockManager.getCurrentTick());
}
