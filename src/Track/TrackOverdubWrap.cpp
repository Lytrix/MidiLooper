//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "Logger.h"
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
  const uint32_t priorPlaybackRevision = loop.playbackRevision;
  const CommitResult result = loop.commitCapturePass(CommitReason::OverdubWrap, sealedAtTick);
  const bool wrapped = result == CommitResult::Committed;
  if (wrapped) {
    const PassId passId = loop.lastCommittedPassId();
    loop.queueOverdubWrapSnapshotJournal(passId);
    EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
    loop.pushOverdubSessionPass(passId, companionIds);
    loop.recordLastOverdubSeal(passId, companionIds, priorPlaybackRevision);
    loop.markAffectedDisplayCacheRanges(passId, companionIds);
  }
  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  for (const MidiEvent& evt : heldOns) {
    loop.appendCaptureEvent(evt);
  }
  loop.noteOverdubWrapCommitted();
  logger.logTrackEvent("Overdub wrap committed", clockManager.getCurrentTick());
  return wrapped;
}
